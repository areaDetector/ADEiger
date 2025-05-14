#include "streamApi.h"

#include <stdexcept>
#include <stdlib.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <frozen.h>
#include <zmq.h>
#include <string.h>
#include <lz4.h>
#include <bitshuffle.h>

#define ZMQ_PORT        9999
#define MAX_JSON_TOKENS 512

#define ERR_PREFIX  "StreamApi"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
        functionName, __VA_ARGS__)

using std::string;

static int readToken (struct json_token *tokens, const char *name, size_t *value)
{
    const char *functionName = "readToken";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return STREAM_ERROR;
    }

    if(sscanf(t->ptr, "%lu", value) != 1)
    {
        ERR_ARGS("unable to parse '%s' token", name);
        return STREAM_ERROR;
    }

    return STREAM_SUCCESS;
}

static int readToken (struct json_token *tokens, const char *name, char *value, size_t size)
{
    const char *functionName = "readToken";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return STREAM_ERROR;
    }

    if(size < (size_t)t->len)
    {
        ERR_ARGS("destination buffer for '%s' not big enough", name);
        return STREAM_ERROR;
    }

    memcpy(value, t->ptr, t->len);

    return STREAM_SUCCESS;
}

static int readToken (struct json_token *tokens, const char *name, size_t *values, size_t size)
{
    const char *functionName = "readToken";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return STREAM_ERROR;
    }

    if(size < (size_t)t->num_desc)
    {
        ERR_ARGS("destination buffer for '%s' not big enough", name);
        return STREAM_ERROR;
    }

    for(size_t i = 0; i < (size_t)t->num_desc; ++i)
    {
        if(sscanf((t+i+1)->ptr, "%lu", values+i) != 1)
        {
            ERR_ARGS("unable to parse '%s[%lu]' token", name, i);
            return STREAM_ERROR;
        }
    }

    return STREAM_SUCCESS;
}

static int readToken (struct json_token *tokens, const char *name, string & value)
{
    const char *functionName = "readToken<string>";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return STREAM_ERROR;
    }

    value = string(t->ptr, t->len);
    return STREAM_SUCCESS;
}

static int uncompress (char *pInput, char *dest, char *encoding, size_t uncompressedSize, NDDataType_t dataType)
{
    const char *functionName = "uncompress";

   if (strcmp(encoding, "lz4<") == 0) {
        int result = LZ4_decompress_fast(pInput, dest, (int)uncompressedSize);
        if (result < 0)
        {
            ERR_ARGS("LZ4_decompress failed, result=%d\n", result);
            return STREAM_ERROR; 
        }
    } 
    else if ((strcmp(encoding, "bs32-lz4<") == 0) ||
             (strcmp(encoding, "bs16-lz4<") == 0) ||
             (strcmp(encoding, "bs8-lz4<") == 0)) {
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

int StreamAPI::poll (int timeout)
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

StreamAPI::StreamAPI (const char *hostname) : mHostname(epicsStrDup(hostname))
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

StreamAPI::~StreamAPI (void)
{
    zmq_close(mSock);
    zmq_ctx_destroy(mCtx);
    free(mHostname);
}

int StreamAPI::getHeader (stream_header_t *header, int timeout)
{
    const char *functionName = "getHeader";
    int err = STREAM_SUCCESS;

    if(timeout && (err = poll(timeout)))
        return err;

    zmq_msg_t header_msg;
    zmq_msg_init(&header_msg);
    zmq_msg_recv(&header_msg, mSock, 0);

    if(header)
    {
        string htype, headerDetail;
        size_t skip = 0;
        size_t size = zmq_msg_size(&header_msg);
        const char *data = (const char*) zmq_msg_data(&header_msg);

        struct json_token tokens[MAX_JSON_TOKENS];

        if(parse_json(data, size, tokens, MAX_JSON_TOKENS) < 0)
        {
            ERR_ARGS("failed to parse JSON, data=%s", data);
            err = STREAM_ERROR;
            goto exit;
        }

        if((err = readToken(tokens, "htype", htype)))
            goto exit;

        string expectedHType("dheader");
        if(htype.compare(0, expectedHType.length(), expectedHType))
        {
            ERR_ARGS("wrong header type, htype=%s", htype.c_str());
            err = STREAM_WRONG_HTYPE;
            goto exit;
        }

        if((err = readToken(tokens, "series", &header->series)))
            goto exit;

        if((err = readToken(tokens, "header_detail", headerDetail)))
            goto exit;

        if(headerDetail == "basic")
            skip = 1;
        else if(headerDetail == "all")
            skip = 7;

        for(size_t i = 0; i < skip; ++i)
            getHeader(NULL, timeout);
    }

exit:
    zmq_msg_close(&header_msg);
    return err;
}

int StreamAPI::waitFrame (int *end, int timeout)
{
    const char *functionName = "waitFrame";
    int err = STREAM_SUCCESS;
    *end = false;

    if(timeout && (err = poll(timeout)))
        return err;

    zmq_msg_t header;

    // Get Header
    zmq_msg_init(&header);
    zmq_msg_recv(&header, mSock, 0);

    struct json_token tokens[MAX_JSON_TOKENS];
    size_t size = zmq_msg_size(&header);
    const char *data = (const char*) zmq_msg_data(&header);
    char htype[64] = "";

    if(parse_json(data, size, tokens, MAX_JSON_TOKENS) < 0)
    {
        ERR("failed to parse image header JSON");
        err = STREAM_ERROR;
        goto closeHeader;
    }
    err = readToken(tokens, "htype",  htype, sizeof(htype));
    if(!strncmp(htype, "dseries_end", 11))
    {
        *end = true;
    }
    else
    {
        err |= readToken(tokens, "series", &mSeries);
        err |= readToken(tokens, "frame",  &mFrame);

        if(err)
        {
            ERR("failed to read token from header message");
            goto closeHeader;
        }
    }
closeHeader:
    zmq_msg_close(&header);

    return err;
}

int StreamAPI::getFrame (NDArray **pArrayOut, NDArrayPool *pNDArrayPool, int decompress)
{
    const char *functionName = "getFrame";
    int err = STREAM_SUCCESS;

    zmq_msg_t shape, timestamp;
    char dataType[8] = "";
    char encoding[32] = {0};

    // Get Shape
    zmq_msg_init(&shape);
    zmq_msg_recv(&shape, mSock, 0);

    struct json_token tokens[MAX_JSON_TOKENS];
    size_t size = zmq_msg_size(&shape);
    const char *data = (const char*) zmq_msg_data(&shape);

    if(parse_json(data, size, tokens, MAX_JSON_TOKENS) < 0)
    {
        ERR("failed to parse image shape JSON");
        err = STREAM_ERROR;
        goto closeShape;
    }

    size_t frameShape[2];
    size_t compressedSize;
    size_t uncompressedSize;
    NDDataType_t frameType;

    err |= readToken(tokens, "shape",    frameShape, 3);
    err |= readToken(tokens, "type",     dataType, sizeof(dataType));
    err |= readToken(tokens, "encoding", encoding, sizeof(encoding));
    err |= readToken(tokens, "size",     &compressedSize);

    if(err)
    {
        ERR("failed to read token from shape message");
        goto closeShape;
    }

    // Calculate uncompressed size
    uncompressedSize = frameShape[0]*frameShape[1];

    if(!strncmp(dataType, "uint32", 6))
    {
        frameType = NDUInt32;
        uncompressedSize *= 4;
    }
    else if (!strncmp(dataType, "uint16", 6))
    {
        frameType = NDUInt16;
        uncompressedSize *= 2;
    }
    else if (!strncmp(dataType, "uint8", 5))
    {
        frameType = NDUInt8;
        uncompressedSize *= 1;
    }
    else
    {
        frameType = NDUInt32;
        uncompressedSize *= 4;
        ERR_ARGS("unknown dataType %s", dataType);
        err = STREAM_ERROR;
        goto closeShape;
    }

    NDArray *pArray;
    if(!(pArray = pNDArrayPool->alloc(2, frameShape, frameType, 0, NULL)))
    {
        ERR_ARGS("failed to allocate NDArray for frame %lu", mFrame);
        err = STREAM_ERROR;
        goto closeShape;
    }

    // Get frame data
    // If data is uncompressed we can copy directly into NDArray
    if (strcmp(encoding, "<") == 0)
    {
        zmq_recv(mSock, pArray->pData, uncompressedSize, 0);
    }
    else 
    {
        char *temp = (char *)malloc(compressedSize);
        zmq_recv(mSock, temp, compressedSize, 0);
        if (decompress) 
        {
            uncompress(temp, (char *)pArray->pData, encoding, uncompressedSize, frameType);
        }
        else
        {        
            char *pInput = temp;        
            if (strcmp(encoding, "lz4<") == 0) {
                pArray->codec.name = "lz4";
            }
            else if ((strcmp(encoding, "bs32-lz4<") == 0) ||
                     (strcmp(encoding, "bs16-lz4<") == 0) ||
                     (strcmp(encoding, "bs8-lz4<") == 0)) {
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
    // Get timestamp
    zmq_msg_init(&timestamp);
    zmq_msg_recv(&timestamp, mSock, 0);

    // Deallocate everything
    zmq_msg_close(&timestamp);

closeShape:
    zmq_msg_close(&shape);

    return err;
}
