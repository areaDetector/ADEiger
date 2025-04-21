#include "streamApi.h"

#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <zmq.h>
#include <string.h>
#include <lz4.h>
#include <bitshuffle.h>

#include <stream2.h>

#define ZMQ_PORT        31001

#define ERR_PREFIX  "Stream2Api"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
        functionName, __VA_ARGS__)

using std::string;

int Stream2API::poll (int timeout)
{
    const char *functionName = "poll";
    zmq_pollitem_t item = {};
    item.socket = mSock;
    item.events = ZMQ_POLLIN;

    int rc = zmq_poll(&item, 1, timeout*1000);
    if(rc < 0)
    {
        ERR("failed to poll socket");
        return STREAM_ERROR;
    }

    return rc ? STREAM_SUCCESS : STREAM_TIMEOUT;
}

Stream2API::Stream2API (const char *hostname) : mHostname(epicsStrDup(hostname))
{
    if(!(mCtx = zmq_ctx_new()))
        throw std::runtime_error("unable to create zmq context");

    if(!(mSock = zmq_socket(mCtx, ZMQ_PULL)))
        throw std::runtime_error("unable to create zmq socket");

    char addr[512];
    size_t n;
    n = epicsSnprintf(addr, sizeof(addr), "tcp://%s:%d", mHostname, ZMQ_PORT);
    if(n >= sizeof(addr))
        throw std::runtime_error("address is too long");

    zmq_connect(mSock, addr);
}

Stream2API::~Stream2API (void)
{
    zmq_close(mSock);
    zmq_ctx_destroy(mCtx);
    free(mHostname);
}

int Stream2API::getHeader (stream_header_t *header, int timeout)
{
    const char *functionName = "getHeader";
    int err = STREAM_SUCCESS;

    if(timeout && (err = poll(timeout)))
        return err;

    zmq_msg_t msg;
    // Get message
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, mSock, 0);
    struct stream2_msg *s2msg;
    if ((err = stream2_parse_msg((const uint8_t *)zmq_msg_data(&msg), zmq_msg_size(&msg), &s2msg))) {
        fprintf(stderr, "error: error %i parsing message\n", err);
        return err;
    }
    stream2_start_msg* sm = (stream2_start_msg *)s2msg;

    ERR_ARGS("sm->type=%d", sm->type);
    if (sm->type != STREAM2_MSG_START) {
        ERR_ARGS("unexpected message type, should be STREAM2_MSG_START (%d), actual=%d", STREAM2_MSG_START, sm->type);
        return STREAM_ERROR;
    }
    mSeries_id = sm->series_id;
    mImage_dtype = sm->image_dtype;
    mImage_size_x = sm->image_size_x;
    mImage_size_y = sm->image_size_y;
    mNumber_of_images = sm->number_of_images;
    ERR_ARGS("series_id=%ld, image_dtype=%s, image_size_x=%ld, image_size_y=%ld, number_of_images=%ld",
             sm->series_id, sm->image_dtype, sm->image_size_x, sm->image_size_y, sm->number_of_images); 
    return STREAM_SUCCESS;
}

int Stream2API::getFrame (stream_frame_t *frame, int timeout)
{
    const char *functionName = "getFrame";
    int err = STREAM_SUCCESS;

    ERR("entry");
    if(timeout && (err = poll(timeout)))
        return err;

    zmq_msg_t msg;
    // Get message
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, mSock, 0);
    struct stream2_msg *s2msg;
    if ((err = stream2_parse_msg((const uint8_t *)zmq_msg_data(&msg), zmq_msg_size(&msg), &s2msg))) {
        fprintf(stderr, "error: error %i parsing message\n", err);
        return err;
    }
    stream2_image_msg* im = (stream2_image_msg *)s2msg;

    ERR_ARGS("im->type=%d", im->type);
    switch (im->type) {
       case STREAM2_MSG_IMAGE:
            for (size_t i = 0; i < im->data.len; i++) {
                struct stream2_image_data *pSID = &im->data.ptr[i];
                printf("data: \"%s\" ", pSID->channel);
                struct stream2_multidim_array mda = pSID->data;
                frame->shape[0] = mda.dim[0];
                frame->shape[1] = mda.dim[1];
                struct stream2_typed_array *pArray = &mda.array;
                stream2_typed_array_tag dataType = (stream2_typed_array_tag)pArray->tag;
                struct stream2_bytes *pSB = &pArray->data;
                frame->compressedSize = pSB->len;
                struct stream2_compression *pCompression = &pSB->compression;
                frame->uncompressedSize = pCompression->orig_size;
                if (pCompression->algorithm != NULL) strcpy(frame->encoding, pCompression->algorithm);
                switch (dataType) {
                    case STREAM2_TYPED_ARRAY_UINT8:
                        frame->type = stream_frame_t::UINT8;
                        break;
                    case STREAM2_TYPED_ARRAY_UINT16_LITTLE_ENDIAN:
                        frame->type = stream_frame_t::UINT16;
                        break;
                    case STREAM2_TYPED_ARRAY_UINT32_LITTLE_ENDIAN:
                        frame->type = stream_frame_t::UINT32;
                        break;
                    default:
                        ERR_ARGS("unknown dataType %d", dataType);
                        err = STREAM_ERROR;
                        goto error;
                }
                frame->data = malloc(frame->compressedSize);
                if(!frame->data)
                {
                    ERR("failed to allocate data");
                    err = STREAM_ERROR;
                    goto error;
                }
                memcpy(frame->data, pSB->ptr, frame->compressedSize);
            }
            break;
        case STREAM2_MSG_END:
            frame->end = true;
            break;
        default:
            ERR_ARGS("unknown unexpected message types %d", im->type);
            break;
    }
    error:
    zmq_msg_close(&msg);
    return err;
}

int Stream2API::uncompress (stream_frame_t *frame, char *dest)
{
    const char *functionName = "uncompress";
    char *pInput = (char *)frame->data;

    if (frame->encoding == NULL) {
        memcpy(dest, pInput, frame->uncompressedSize);
    } 
    else if (strcmp(frame->encoding, "lz4") == 0) {
        ERR("calling LZ4_decompress_fast");
        int result = LZ4_decompress_fast(pInput, dest, (int)frame->uncompressedSize);
        if (result < 0)
        {
            ERR_ARGS("LZ4_decompress failed, result=%d\n", result);
            return STREAM_ERROR; 
        }
    } 
    else if (strcmp(frame->encoding, "bslz4") == 0)  {
        pInput += 12;   // compressed sdata is 12 bytes into buffer
        size_t elemSize;
        switch (frame->type) 
        {
            case stream_frame_t::UINT32: elemSize=4; break;
            case stream_frame_t::UINT16: elemSize=2; break;
            case stream_frame_t::UINT8:  elemSize=1; break;
            default:
                ERR_ARGS("unknown frame type=%d", frame->type);
                return STREAM_ERROR;
        }
        size_t numElements = frame->uncompressedSize/elemSize;
        int result = bshuf_decompress_lz4(pInput, dest, numElements, elemSize, 0);
        if (result < 0)
        {
            ERR_ARGS("bshuf_decompress_lz4 failed, result=%d", result);
            return STREAM_ERROR;
        }
    }
    else {
        ERR_ARGS("Unknown encoding=%s", frame->encoding);
        return STREAM_ERROR;
    }

    return STREAM_SUCCESS;
}
