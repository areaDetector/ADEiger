#include "asynDriver.h"
#include "epicsTime.h"
#include "epicsTypes.h"
#include "stream2.h"
#include "streamApi.h"
#include "rfc3339.h"

#include <cstdlib>
#include <ctime>
#include <stdexcept>
#include <stdlib.h>
#include <stdint.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <string.h>
#include <lz4.h>
#include <bitshuffle.h>
#include <compression.h>

#define ZMQ_PORT        31001

#define ERR_PREFIX  "Stream2Api"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
        functionName, __VA_ARGS__)

using std::string;

static int uncompress (const unsigned char *pInput, char *dest, char *encoding, 
                       size_t compressedSize, size_t uncompressedSize, NDDataType_t dataType)
{
    const char *functionName = "uncompress";
    size_t elemSize;
    uint64_t orig_size = *(uint64_t *)pInput;
    orig_size = be64toh(orig_size);
    uint32_t block_size = *(uint32_t *)(pInput + 8);
    block_size = be32toh(block_size);
    switch (dataType) 
    {
        case NDUInt32: elemSize=4; break;
        case NDUInt16: elemSize=2; break;
        case NDUInt8:  elemSize=1; break;
        default:
            ERR_ARGS("unknown dataType=%d", dataType);
            return STREAM_ERROR;
    }
    if (strcmp(encoding, "lz4") == 0) {
//        ERR("calling LZ4_decompress_fast");
//        size_t result = LZ4_decompress_fast((const char *)pInput+16, dest, (int)uncompressedSize);
        size_t result = compression_decompress_buffer(COMPRESSION_LZ4,
                                                      dest,
                                                      uncompressedSize,
                                                      (const char *)pInput,
                                                      compressedSize,
                                                      elemSize);        
        if (result < 0)
        {
            ERR_ARGS("LZ4_decompress failed, result=%lu\n", result);
            return STREAM_ERROR; 
        }
    } 
    else if (strcmp(encoding, "bslz4") == 0)  {
        pInput += 12;   // compressed sdata is 12 bytes into buffer

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

epicsTimeStamp Stream2API::extractTimeStampFromMessage(stream2_image_msg *msg) {
    static const char *functionName = "extractTimeStampFromMessage";

    // Update cache if needed (series_date does not change for
    // the entirety of one acquisition)
    if (mCachedTs.tsStr != mImageMsg->series_date) {
        mCachedTs.tsStr = mImageMsg->series_date;
        gm_tm_nano_sec parsedTs = rfc3339::parseRfc3339Timestamp(mImageMsg->series_date);

        if (rfc3339::equals(parsedTs, rfc3339::ZERO))
            ERR_ARGS("Failed to parse timestamp '%s' as an RFC3339-compliant timestamp", mImageMsg->series_date);

        epicsTime ts = parsedTs;    // Convert gm_tm_nano_sec to epicsTime
        mCachedTs.ts = ts;          // Convert epicsTime to epicsTimeStamp
    }

    static const epicsUInt64 NSEC_PER_SEC = 1000000000u;
    epicsTimeStamp series_ts = mCachedTs.ts;

    // Calculate the frame start time relative to the series start time, in ns
    epicsUInt64 ticks = mImageMsg->start_time[0];       // Ticks of the clock
    epicsUInt64 time_base = mImageMsg->start_time[1];   // Clock freq (e.g 50000000 for 50 MHz)
    epicsUInt64 elapsed_ns = NSEC_PER_SEC * ticks / time_base;

    epicsUInt64 frame_sec = series_ts.secPastEpoch;
    epicsUInt64 frame_nsec = series_ts.nsec;
    frame_nsec += elapsed_ns;

    // Normalize
    frame_sec += frame_nsec / NSEC_PER_SEC;
    frame_nsec = frame_nsec % NSEC_PER_SEC;

    return epicsTimeStamp {
        .secPastEpoch = static_cast<epicsUInt32>(frame_sec),
        .nsec = static_cast<epicsUInt32>(frame_nsec),
    };
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

    if (sm->type != STREAM2_MSG_START) {
        ERR_ARGS("unexpected message type, should be STREAM2_MSG_START (%d), actual=%d", STREAM2_MSG_START, sm->type);
        return STREAM_ERROR;
    }
    mSeries_id = sm->series_id;
    mImage_dtype = sm->image_dtype;
    mImage_size_x = sm->image_size_x;
    mImage_size_y = sm->image_size_y;
    mNumber_of_images = sm->number_of_images;
    mThresholdEnergy.clear();
    for (int i=0; i<(int)sm->threshold_energy.len; i++) {
        mThresholdEnergy.push_back(sm->threshold_energy.ptr[i]);
    }
    return STREAM_SUCCESS;
}

int Stream2API::waitFrame (int *end, int *numThresholds, int timeout)
{
    //const char *functionName = "waitFrame";
    int err = STREAM_SUCCESS;
    *end = false;

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
    mNumThresholds = (int)mImageMsg->data.len;
    *numThresholds = mNumThresholds;

    if (mImageMsg->type == STREAM2_MSG_END)
    {
        *end = true;
    }
    return err;
}

int Stream2API::getFrame (NDArray **pArrayOut, NDArrayPool *pNDArrayPool, int thresh, int decompress, bool extractTimeStamp)
{
    const char *functionName = "getFrame";
    int err = STREAM_SUCCESS;
    size_t compressedSize, uncompressedSize;
    size_t dims[3];
    int numDims;
    NDArray *pArray;
    char encoding[32];
    NDDataType_t dataType;

    switch (mImageMsg->type) {
       case STREAM2_MSG_IMAGE: {
            struct stream2_image_data *pSID = &mImageMsg->data.ptr[thresh];
            struct stream2_multidim_array mda = pSID->data;
            dims[0] = mda.dim[1];
            dims[1] = mda.dim[0];
            numDims = 2;
            struct stream2_typed_array *pS2Array = &mda.array;
            stream2_typed_array_tag s2DataType = (stream2_typed_array_tag)pS2Array->tag;
            struct stream2_bytes *pSB = &pS2Array->data;
            compressedSize = pSB->len;
            uncompressedSize = pSB->len;
            struct stream2_compression *pCompression = &pSB->compression;
            if (pCompression->algorithm != NULL) {
                uncompressedSize = pCompression->orig_size;
                strcpy(encoding, pCompression->algorithm);
            }
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
            
            if(!(pArray = pNDArrayPool->alloc(numDims, dims, dataType, 0, NULL)))
            {
                ERR("failed to allocate NDArray for frame");
                err = STREAM_ERROR;
                goto error;
            }

            // Get frame data
            // If data is uncompressed we can copy directly into NDArray
            if (pCompression->algorithm == NULL)
            {
                memcpy((char *)pArray->pData, pSB->ptr, uncompressedSize);
            }
            else 
            {
                if (decompress) 
                {
                    uncompress(pSB->ptr, (char *)pArray->pData, encoding, compressedSize, uncompressedSize, dataType);
                }
                else
                {        
                    const unsigned char *pInput = pSB->ptr;
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

                    if (extractTimeStamp) {
                        epicsTimeStamp ts = extractTimeStampFromMessage(mImageMsg);
                        pArray->epicsTS = ts;
                        pArray->timeStamp = ts.secPastEpoch + ts.nsec/1.e9;
                    }
                }
            }
            pArray->pAttributeList->add("ThresholdName", "Threshold name", NDAttrString, mThresholdEnergy[thresh].channel);
            pArray->pAttributeList->add("ThresholdEnergy", "Threshold energy (eV)", NDAttrFloat64, (void *)&(mThresholdEnergy[thresh].energy));
            *pArrayOut = pArray;
            break;
        }
        default:
        ERR_ARGS("unexpected message type %d", mImageMsg->type);
    }
    error:
    if (thresh == mNumThresholds-1) {
        zmq_msg_close(&mMsg);
    }
    return err;
}
