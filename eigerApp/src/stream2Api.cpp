#include "streamApi.h"

#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <string.h>
#include <lz4.h>
#include <bitshuffle.h>

#define ZMQ_PORT        31001

#define ERR_PREFIX  "Stream2Api"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
        functionName, __VA_ARGS__)

using std::string;

static int uncompress (char *pInput, char *dest, char *encoding, size_t uncompressedSize, NDDataType_t dataType)
{
    const char *functionName = "uncompress";

    if (strcmp(encoding, "lz4") == 0) {
        ERR("calling LZ4_decompress_fast");
        int result = LZ4_decompress_fast(pInput, dest, (int)uncompressedSize);
        if (result < 0)
        {
            ERR_ARGS("LZ4_decompress failed, result=%d\n", result);
            return STREAM_ERROR; 
        }
    } 
    else if (strcmp(encoding, "bslz4") == 0)  {
        pInput += 12;   // compressed sdata is 12 bytes into buffer
        size_t elemSize;
        switch (dataType) 
        {
            case NDUInt32: elemSize=4; break;
            case NDUInt16: elemSize=2; break;
            case NDUInt8:  elemSize=1; break;
            default:
                ERR_ARGS("unknown frame type=%d", dataType);
                return STREAM_ERROR;
        }
        size_t numElements = uncompressedSize/elemSize;
        int result = bshuf_decompress_lz4(pInput, dest, numElements, elemSize, 0);
        if (result < 0)
        {
            ERR_ARGS("bshuf_decompress_lz4 failed, result=%d", result);
            return STREAM_ERROR;
        }
    }
    else {
        ERR_ARGS("Unknown encoding=%s", encoding);
        return STREAM_ERROR;
    }
    return STREAM_SUCCESS;
}

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

int Stream2API::waitFrame (int *end, int timeout)
{
    const char *functionName = "waitFrame";
    int err = STREAM_SUCCESS;
    *end = false;

    ERR("entry");
    if(timeout && (err = poll(timeout)))
        return err;

    // Get message
    zmq_msg_init(&mMsg);
    zmq_msg_recv(&mMsg, mSock, 0);
    struct stream2_msg *s2msg;
    if ((err = stream2_parse_msg((const uint8_t *)zmq_msg_data(&mMsg), zmq_msg_size(&mMsg), &s2msg))) {
        fprintf(stderr, "error: error %i parsing message\n", err);
        return err;
    }
    mImageMsg = (stream2_image_msg *)s2msg;

    ERR_ARGS("im->type=%d", mImageMsg->type);
    if (mImageMsg->type == STREAM2_MSG_END)
    {
        *end = true;
    }
    return err;
}

int Stream2API::getFrame (NDArray **pArrayOut, NDArrayPool *pNDArrayPool, int decompress)
{
    const char *functionName = "getFrame";
    int err = STREAM_SUCCESS;
    size_t compressedSize, uncompressedSize;
    size_t dims[2];
    NDArray *pArray;
    char encoding[32];
    NDDataType_t dataType;

    ERR_ARGS("mImageMsg->type=%d", mImageMsg->type);
    switch (mImageMsg->type) {
       case STREAM2_MSG_IMAGE:
            for (size_t i = 0; i < mImageMsg->data.len; i++) {
                struct stream2_image_data *pSID = &mImageMsg->data.ptr[i];
                printf("data: \"%s\" ", pSID->channel);
                struct stream2_multidim_array mda = pSID->data;
                dims[1] = mda.dim[0];
                dims[0] = mda.dim[1];
                struct stream2_typed_array *pS2Array = &mda.array;
                stream2_typed_array_tag s2DataType = (stream2_typed_array_tag)pS2Array->tag;
                struct stream2_bytes *pSB = &pS2Array->data;
                compressedSize = pSB->len;
                struct stream2_compression *pCompression = &pSB->compression;
                uncompressedSize = pCompression->orig_size;
                if (pCompression->algorithm != NULL) strcpy(encoding, pCompression->algorithm);
                switch (s2DataType) {
                    case STREAM2_TYPED_ARRAY_UINT8:
                        dataType = NDUInt8;
                        break;
                    case STREAM2_TYPED_ARRAY_UINT16_LITTLE_ENDIAN:
                        dataType = NDUInt16;
                        break;
                    case STREAM2_TYPED_ARRAY_UINT32_LITTLE_ENDIAN:
                        dataType = NDUInt32;
                        break;
                    default:
                        ERR_ARGS("unknown dataType %d", s2DataType);
                        err = STREAM_ERROR;
                        goto error;
                }
                
                if(!(pArray = pNDArrayPool->alloc(2, dims, dataType, 0, NULL)))
                {
                    ERR("failed to allocate NDArray for frame");
                    err = STREAM_ERROR;
                    goto error;
                }
                // Get frame data
                // If data is uncompressed we can copy directly into NDArray
                if (pCompression->algorithm == NULL)
                {
                    memcpy(pArray->pData, pSB->ptr, compressedSize);
                }
                else 
                {
                    char *temp = (char *)malloc(compressedSize);
                    if (decompress) 
                    {
                        uncompress(temp, (char *)pArray->pData, encoding, uncompressedSize, dataType);
                    }
                    else
                    {        
                        char *pInput = temp;
                        if (strcmp(encoding, "lz4") == 0) 
                        {
                            pArray->codec.name = "lz4";
                        }
                        else if (strcmp(encoding, "bslz4") == 0) 
                        {
                            pArray->codec.name = "bslz4";
                            pInput += 12;
                            compressedSize -= 12;
                        }
                        else {
                            ERR_ARGS("unknown encoding %s", encoding);
                        }
                        pArray->compressedSize = compressedSize;
                        memcpy(pArray->pData, pInput, compressedSize);
                    }
                    free(temp);
                }
                *pArrayOut = pArray;
            }
            break;
        default:
            ERR_ARGS("unknown unexpected message types %d", mImageMsg->type);
            break;
    }
    error:
    zmq_msg_close(&mMsg);
    return err;
}

