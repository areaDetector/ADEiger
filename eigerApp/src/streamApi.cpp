#include "streamApi.h"

#include <stdexcept>
#include <stdlib.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <frozen.h>
#include <zmq.h>
#include <string.h>
#include <lz4.h>

#define ZMQ_PORT        9999
#define MAX_JSON_TOKENS 512

#define ERR_PREFIX  "StreamApi"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: "fmt"\n", \
        functionName, __VA_ARGS__)

static int readToken (struct json_token *tokens, const char *name, size_t *value)
{
    const char *functionName = "readToken";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return EXIT_FAILURE;
    }

    if(sscanf(t->ptr, "%lu", value) != 1)
    {
        ERR_ARGS("unable to parse '%s' token", name);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int readToken (struct json_token *tokens, const char *name, char *value, size_t size)
{
    const char *functionName = "readToken";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return EXIT_FAILURE;
    }

    if(size < (size_t)t->len)
    {
        ERR_ARGS("destination buffer for '%s' not big enough", name);
        return EXIT_FAILURE;
    }

    memcpy(value, t->ptr, t->len);

    return EXIT_SUCCESS;
}

static int readToken (struct json_token *tokens, const char *name, size_t *values, size_t size)
{
    const char *functionName = "readToken";

    struct json_token *t = find_json_token(tokens, name);
    if(!t)
    {
        ERR_ARGS("unable to find '%s' token", name);
        return EXIT_FAILURE;
    }

    if(size < (size_t)t->num_desc)
    {
        ERR_ARGS("destination buffer for '%s' not big enough", name);
        return EXIT_FAILURE;
    }

    for(size_t i = 0; i < (size_t)t->num_desc; ++i)
    {
        if(sscanf((t+i+1)->ptr, "%lu", values+i) != 1)
        {
            ERR_ARGS("unable to parse '%s[%lu]' token", name, i);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
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

int StreamAPI::getHeader (stream_header_t *header)
{
    const char *functionName = "getHeader";
    int err = EXIT_SUCCESS;

    zmq_msg_t header_msg;
    zmq_msg_init(&header_msg);
    zmq_msg_recv(&header_msg, mSock, 0);

    if(header)
    {
        size_t size = zmq_msg_size(&header_msg);
        const char *data = (const char*) zmq_msg_data(&header_msg);

        struct json_token tokens[MAX_JSON_TOKENS];

        if(parse_json(data, size, tokens, MAX_JSON_TOKENS) < 0)
        {
            ERR("failed to parse JSON");
            err = EXIT_FAILURE;
            goto exit;
        }

        err = readToken(tokens, "series", &header->series);
    }

exit:
    zmq_msg_close(&header_msg);
    return err;
}

int StreamAPI::getFrame (stream_frame_t *frame)
{
    const char *functionName = "getImage";
    int err = EXIT_SUCCESS;

    zmq_msg_t header, shape, timestamp;

    // Get Header
    zmq_msg_init(&header);
    zmq_msg_recv(&header, mSock, 0);

    if(frame)
    {
        struct json_token tokens[MAX_JSON_TOKENS];
        size_t size = zmq_msg_size(&header);
        const char *data = (const char*) zmq_msg_data(&header);

        if(parse_json(data, size, tokens, MAX_JSON_TOKENS) < 0)
        {
            ERR("failed to parse image header JSON");
            err = EXIT_FAILURE;
            goto closeHeader;
        }

        char htype[64] = "";
        err = readToken(tokens, "htype",  htype, sizeof(htype));

        if(!strncmp(htype, "dseries_end", 11))
        {
            frame->end = true;
            goto closeHeader;
        }
        else
        {
            err |= readToken(tokens, "series", &frame->series);
            err |= readToken(tokens, "frame",  &frame->frame);

            if(err)
            {
                ERR("failed to read token from header message");
                goto closeHeader;
            }
        }
    }

    // Get Shape
    zmq_msg_init(&shape);
    zmq_msg_recv(&shape, mSock, 0);

    if(frame)
    {
        struct json_token tokens[MAX_JSON_TOKENS];
        size_t size = zmq_msg_size(&shape);
        const char *data = (const char*) zmq_msg_data(&shape);

        if(parse_json(data, size, tokens, MAX_JSON_TOKENS) < 0)
        {
            ERR("failed to parse image shape JSON");
            err = EXIT_FAILURE;
            goto closeShape;
        }

        char dataType[8] = "";
        err |= readToken(tokens, "shape",    frame->shape, 3);
        err |= readToken(tokens, "type",     dataType, sizeof(dataType));
        err |= readToken(tokens, "encoding", frame->encoding, sizeof(frame->encoding));
        err |= readToken(tokens, "size",     &frame->compressedSize);

        if(err)
        {
            ERR("failed to read token from shape message");
            goto closeShape;
        }

        // Calculate uncompressed size
        frame->uncompressedSize = frame->shape[0]*frame->shape[1];

        if(!strncmp(dataType, "uint16", 6))
        {
            frame->type = stream_frame_t::UINT16;
            frame->uncompressedSize *= 2;
        }
        else
        {
            frame->type = stream_frame_t::UINT32;
            frame->uncompressedSize *= 4;
        }

        frame->data = malloc(frame->compressedSize);
        if(!frame->data)
        {
            ERR("failed to allocate data");
            err = EXIT_FAILURE;
            goto closeShape;
        }
    }

    // Get frame data
    zmq_recv(mSock, frame->data, frame->compressedSize, 0);

    // Get timestamp
    zmq_msg_init(&timestamp);
    zmq_msg_recv(&timestamp, mSock, 0);

    // Deallocate everything
    zmq_msg_close(&timestamp);

closeShape:
    zmq_msg_close(&shape);

closeHeader:
    zmq_msg_close(&header);

    return err;
}

int StreamAPI::getMessage (void)
{
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_msg_recv(&msg, mSock, 0);
    size_t len = zmq_msg_size(&msg);

    printf("MESSAGE [len=%lu]\n", len);
    if(len < 3000)
        printf("%.*s\n", (int)len, (const char *)zmq_msg_data(&msg));
    else
        printf("BLOB\n");

    zmq_msg_close(&msg);
    return EXIT_SUCCESS;
}

int StreamAPI::uncompress (stream_frame_t *frame, char *dest)
{
    const char *functionName = "uncompress";

    if(LZ4_decompress_fast((const char *)frame->data, dest, (int)frame->uncompressedSize) <0)
    {
        ERR("LZ4_decompress failed");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
