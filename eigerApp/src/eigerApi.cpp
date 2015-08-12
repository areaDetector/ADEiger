#include "eigerApi.h"

#include <stdexcept>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <frozen.h>     // JSON parser

#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsTime.h>

#include <fcntl.h>

#define API_VERSION             "1.0.4"
#define EOL                     "\r\n"      // End of Line
#define EOL_LEN                 2           // End of Line Length
#define EOH                     EOL EOL     // End of Header
#define EOH_LEN                 (EOL_LEN*2) // End of Header Length

#define ID_STR                  "$id"
#define ID_LEN                  3

#define DATA_NATIVE             "application/json; charset=utf-8"
#define DATA_TIFF               "application/tiff"
#define DATA_HDF5               "application/hdf5"
#define DATA_HTML               "text/html"

#define HTTP_PORT               80
#define MAX_MESSAGE_SIZE        512
#define MAX_BUF_SIZE            256
#define MAX_JSON_TOKENS         100

#define DEFAULT_TIMEOUT_INIT    30
#define DEFAULT_TIMEOUT_ARM     55
#define DEFAULT_TIMEOUT_CONNECT 1

#define ERR_PREFIX  "EigerApi"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
    functionName, __VA_ARGS__)

// Requests

#define REQUEST_GET\
    "GET %s%s HTTP/1.0" EOL \
    "Content-Length: 0" EOL \
    "Accept: " DATA_NATIVE EOH

#define REQUEST_PUT\
    "PUT %s%s HTTP/1.0" EOL \
    "Accept-Encoding: identity" EOL\
    "Content-Type: " DATA_NATIVE EOL \
    "Content-Length: %lu" EOH

#define REQUEST_HEAD\
    "HEAD %s%s HTTP/1.0" EOH

// Structure definitions

typedef struct socket
{
    char host[256];
    struct sockaddr_in addr;
    SOCKET fd;
    epicsMutex mutex;
    bool closed;
} socket_t;

typedef struct request
{
    char *data;
    size_t dataLen, actualLen;
} request_t;

typedef struct response
{
    char *data;
    size_t dataLen, actualLen, headerLen;
    bool reconnect;
    char *content;
    size_t contentLength;
    int code;
} response_t;

// Static public members

const char *Eiger::sysStr [SSCount] = {
    "/detector/api/version",
    "/detector/api/"   API_VERSION "/config/",
    "/detector/api/"   API_VERSION "/status/",
    "/filewriter/api/" API_VERSION "/config/",
    "/filewriter/api/" API_VERSION "/status/",
    "/detector/api/"   API_VERSION "/command/",
    "/data/",
};

const char *Eiger::triggerModeStr [TMCount] = {
    "ints", "inte", "exts", "exte"
};

int Eiger::init (void)
{
    return osiSockAttach();
}

void Eiger::deinit (void)
{
    osiSockRelease();
}

int Eiger::buildMasterName (const char *pattern, int seqId, char *buf, size_t bufSize)
{
    const char *idStr = strstr(pattern, ID_STR);

    if(idStr)
    {
        int prefixLen = idStr - pattern;
        epicsSnprintf(buf, bufSize, "%.*s%d%s_master.h5", prefixLen, pattern, seqId,
                pattern+prefixLen+ID_LEN);
    }
    else
        epicsSnprintf(buf, bufSize, "%s_master.h5", pattern);

    return EXIT_SUCCESS;
}

int Eiger::buildDataName (int n, const char *pattern, int seqId, char *buf, size_t bufSize)
{
    const char *idStr = strstr(pattern, ID_STR);

    if(idStr)
    {
        int prefixLen = idStr - pattern;
        epicsSnprintf(buf, bufSize, "%.*s%d%s_data_%06d.h5", prefixLen, pattern, seqId,
                pattern+prefixLen+ID_LEN, n);
    }
    else
        epicsSnprintf(buf, bufSize, "%s_data_%06d.h5", pattern, n);

    return EXIT_SUCCESS;
}

// Public members

Eiger::Eiger (const char *hostname) :
    mSockFd(0), mSockMutex(), mSockClosed(true)
{
    strncpy(mHostname, hostname, sizeof(mHostname));
    memset(&mAddress, 0, sizeof(mAddress));

    if(hostToIPAddr(mHostname, &mAddress.sin_addr))
        throw std::runtime_error("invalid hostname");

    mAddress.sin_family = AF_INET;
    mAddress.sin_port = htons(HTTP_PORT);
}

int Eiger::initialize (void)
{
    return put(SSCommand, "initialize", "", 0, NULL);
}

int Eiger::arm (int *sequenceId)
{
    const char *functionName = "arm";

    request_t request;
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_PUT, sysStr[SSCommand], "arm", 0lu);

    response_t response;
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    if(doRequest(&request, &response, DEFAULT_TIMEOUT_ARM))
    {
        ERR("[param=arm] request failed");
        return EXIT_FAILURE;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[param=arm] server returned error code %d", response.code);
        return EXIT_FAILURE;
    }

    return sequenceId ? parseSequenceId(&response, sequenceId) : EXIT_SUCCESS;
}

int Eiger::trigger (int timeout, double exposure)
{
    // Trigger for INTS mode
    if(!exposure)
        return put(SSCommand, "trigger", "", 0, NULL, timeout);

    // Tigger for INTE mode
    // putDouble should block for the whole exposure duration, but it doesn't
    // (Eiger's fault)

    epicsTimeStamp start, end;

    epicsTimeGetCurrent(&start);
    if(putDouble(SSCommand, "trigger", exposure, NULL, timeout))
        return EXIT_FAILURE;
    epicsTimeGetCurrent(&end);

    double diff = epicsTimeDiffInSeconds(&end, &start);
    if(diff < exposure)
    {
        printf("sleeping for %.3f\n", exposure-diff);
        epicsThreadSleep(exposure - diff);
    }

    return EXIT_SUCCESS;
}

int Eiger::disarm (void)
{
    return put(SSCommand, "disarm", "", 0, NULL);
}

int Eiger::cancel (void)
{
    return put(SSCommand, "cancel", "", 0, NULL);
}

int Eiger::abort (void)
{
    return put(SSCommand, "abort", "", 0, NULL);
}

int Eiger::getString (sys_t sys, const char *param, char *value, size_t len, int timeout)
{
    return get(sys, param, value, len, timeout);
}

int Eiger::getInt (sys_t sys, const char *param, int *value, int timeout)
{
    const char *functionName = "getInt";
    char buf[MAX_BUF_SIZE];

    if(get(sys, param, buf, sizeof(buf), timeout))
        return EXIT_FAILURE;

    if(sscanf(buf, "%d", value) != 1)
    {
        ERR_ARGS("[param=%s] couldn't parse '%s' as integer", param, buf);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int Eiger::getDouble (sys_t sys, const char *param, double *value, int timeout)
{
    const char *functionName = "getDouble";
    char buf[MAX_BUF_SIZE];

    if(get(sys, param, buf, sizeof(buf), timeout))
        return EXIT_FAILURE;

    if(sscanf(buf, "%lf", value) != 1)
    {
        ERR_ARGS("[param=%s] couldn't parse '%s' as double", param, buf);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int Eiger::getBool (sys_t sys, const char *param, bool *value, int timeout)
{
    char buf[MAX_BUF_SIZE];

    if(get(sys, param, buf, sizeof(buf), timeout))
        return EXIT_FAILURE;

    *value = buf[0] == 't';

    return EXIT_SUCCESS;
}

int Eiger::putString (sys_t sys, const char *param, const char *value,
        paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": \"%s\"}", value);

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int Eiger::putInt (sys_t sys, const char *param,
        int value, paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": %d}", value);

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int Eiger::putDouble (sys_t sys, const char *param,
        double value, paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": %lf}", value);

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int Eiger::putBool (sys_t sys, const char *param,
        bool value, paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": %s}", value ? "true" : "false");

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int Eiger::getFileSize (const char *filename, size_t *size)
{
    const char *functionName = "getFileSize";

    request_t request;
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_HEAD, sysStr[SSData], filename);

    response_t response;
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    if(doRequest(&request, &response))
    {
        ERR_ARGS("[file=%s] HEAD request failed", filename);
        return EXIT_FAILURE;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[file=%s] server returned error code %d", filename,
                response.code);
        return EXIT_FAILURE;
    }

    *size = response.contentLength;
    return EXIT_SUCCESS;
}

int Eiger::waitFile (const char *filename, double timeout)
{
    const char *functionName = "waitFile";

    epicsTimeStamp start, now;

    request_t request;
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_HEAD, sysStr[SSData], filename);

    response_t response;
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    epicsTimeGetCurrent(&start);

    do
    {
        if(doRequest(&request, &response))
        {
            ERR_ARGS("[file=%s] HEAD request failed", filename);
            return EXIT_FAILURE;
        }

        if(response.code == 200)
            return EXIT_SUCCESS;

        if(response.code != 404)
        {
            ERR_ARGS("[file=%s] server returned error code %d", filename,
                    response.code);
            return EXIT_FAILURE;
        }

        epicsTimeGetCurrent(&now);
    }while(epicsTimeDiffInSeconds(&now, &start) < timeout);

    //ERR_ARGS("timeout waiting for file %s", filename);
    return EXIT_FAILURE;
}

int Eiger::getFile (const char *filename, char **buf, size_t *bufSize)
{
    const char *functionName = "getFile";
    int status = EXIT_SUCCESS;
    int received;

    request_t request;
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_GET, sysStr[SSData], filename);

    response_t response;
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    mSockMutex.lock();

    if(mSockClosed)
    {
        if(connect())
        {
            ERR("failed to reconnect socket");
            status = EXIT_FAILURE;
            goto end;
        }
    }

    if(send(mSockFd, request.data, request.actualLen, 0) < 0)
    {
        status = EXIT_FAILURE;
        goto end;
    }

    received = recv(mSockFd, response.data, response.dataLen, 0);

    if(received < 0)
    {
        ERR_ARGS("[file=%s] failed to receive first part", filename);
        status = EXIT_FAILURE;
        goto end;
    }

    if((status = parseHeader(&response)))
    {
        ERR_ARGS("[file=%s] underlying parseResponse failed", filename);
        goto markClosed;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[file=%s] file not found", filename);
        status = EXIT_FAILURE;
        goto markClosed;
    }

    *buf = (char*)malloc(response.contentLength);
    if(!*buf)
    {
        ERR_ARGS("[file=%s] malloc(%lu) failed", filename, response.contentLength);
        status = EXIT_FAILURE;
        goto markClosed;
    }

    *bufSize = received - response.headerLen;
    memcpy(*buf, response.content, *bufSize);

    received = recv(mSockFd, *buf + *bufSize, response.contentLength - *bufSize,
            MSG_WAITALL);

    if(received < 0)
    {
        ERR_ARGS("[file=%s] failed to receive second part", filename);
        status = EXIT_FAILURE;
        free(*buf);
        *buf = NULL;
        *bufSize = 0;
    }

    *bufSize = response.contentLength;

markClosed:
    if(response.reconnect)
    {
        close(mSockFd);
        mSockClosed = true;
    }

end:
    mSockMutex.unlock();
    return status;
}

// Private members

int Eiger::connect (void)
{
    const char *functionName = "connect";

    if(!mSockClosed)
        return EXIT_SUCCESS;

    mSockFd = epicsSocketCreate(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if(mSockFd == INVALID_SOCKET)
    {
        ERR("couldn't create socket");
        return EXIT_FAILURE;
    }

    setNonBlock(true);

    if(::connect(mSockFd, (struct sockaddr*)&mAddress, sizeof(mAddress)) < 0)
    {
        // Connection actually failed
        if(errno != EINPROGRESS)
        {
            char error[MAX_BUF_SIZE];
            epicsSocketConvertErrnoToString(error, sizeof(error));
            ERR_ARGS("failed to connect to %s:%d [%s]", mHostname, HTTP_PORT, error);
            epicsSocketDestroy(mSockFd);
            return EXIT_FAILURE;
        }
        // Server didn't respond immediately, wait a little
        else
        {
            fd_set set;
            struct timeval tv;
            int ret;

            FD_ZERO(&set);
            FD_SET(mSockFd, &set);
            tv.tv_sec  = DEFAULT_TIMEOUT_CONNECT;
            tv.tv_usec = 0;

            ret = select(mSockFd + 1, NULL, &set, NULL, &tv);
            if(ret <= 0)
            {
                const char *error = ret == 0 ? "TIMEOUT" : "select failed";
                ERR_ARGS("failed to connect to %s:%d [%s]", mHostname, HTTP_PORT, error);
                epicsSocketDestroy(mSockFd);
                return EXIT_FAILURE;
            }
        }
    }

    setNonBlock(false);
    mSockClosed = false;
    return EXIT_SUCCESS;
}

int Eiger::setNonBlock (bool nonBlock)
{
#ifdef WIN32
    unsigned long mode = nonBlock ? 1 : 0;
    return ioctlsocket(mSockFd, FIONBIO, &mode) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#else
    int flags = fcntl(mSockFd, F_GETFL, 0);
    if(flags < 0)
        return EXIT_FAILURE;

    flags = nonBlock ? flags | O_NONBLOCK : flags & ~O_NONBLOCK;
    return fcntl(mSockFd, F_SETFL, flags) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
#endif
}

int Eiger::doRequest (const request_t *request, response_t *response, int timeout)
{
    const char *functionName = "doRequest";
    int status = EXIT_SUCCESS;
    int received, ret;
    struct timeval recvTimeout;
    fd_set fds;

    mSockMutex.lock();

    if(mSockClosed)
    {
        if(connect())
        {
            ERR("failed to reconnect socket");
            status = EXIT_FAILURE;
            goto end;
        }
    }

    if(send(mSockFd, request->data, request->actualLen, 0) < 0)
    {
        status = EXIT_FAILURE;
        goto end;
    }

    recvTimeout.tv_sec = timeout;
    recvTimeout.tv_usec = 0;
    FD_ZERO(&fds);
    FD_SET(mSockFd, &fds);

    ret = select(mSockFd+1, &fds, NULL, NULL, &recvTimeout);
    if(ret <= 0)
    {
        ERR(ret ? "select() failed" : "timed out");
        status = EXIT_FAILURE;
        goto end;
    }

    if((received = recv(mSockFd, response->data, response->dataLen, 0) < 0))
    {
        status = EXIT_FAILURE;
        goto end;
    }

    response->actualLen = (size_t) received;

    status = parseHeader(response);

    if(response->reconnect)
    {
        close(mSockFd);
        mSockClosed = true;
    }

end:
    mSockMutex.unlock();
    return status;
}

int Eiger::get (sys_t sys, const char *param, char *value, size_t len,
        int timeout)
{
    const char *functionName = "get";

    request_t request;
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_GET, sysStr[sys], param);

    response_t response;
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    if(doRequest(&request, &response, timeout))
    {
        ERR_ARGS("[param=%s] request failed", param);
        return EXIT_FAILURE;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[param=%s] server returned error code %d", param, response.code);
        return EXIT_FAILURE;
    }

    if(!value)
        return EXIT_SUCCESS;

    struct json_token tokens[MAX_JSON_TOKENS];
    struct json_token *valueToken;

    int err = parse_json(response.content, response.contentLength, tokens, MAX_JSON_TOKENS);
    if(err < 0)
    {
        ERR_ARGS("[param=%s] unable to parse json response\n[%.*s]", param,
                (int)response.contentLength, response.content);
        return EXIT_FAILURE;
    }

    valueToken = find_json_token(tokens, "value");
    if(valueToken == NULL)
    {
        ERR_ARGS("[param=%s] unable to find 'value' json field", param);
        return EXIT_FAILURE;
    }

    if((size_t)valueToken->len > ((size_t)(len + 1)))
    {
        ERR_ARGS("[param=%s] destination buffer is too short", param);
        return EXIT_FAILURE;
    }

    memcpy((void*)value, (void*)valueToken->ptr, valueToken->len);
    value[valueToken->len] = '\0';

    return EXIT_SUCCESS;
}

int Eiger::put (sys_t sys, const char *param, const char *value, size_t len,
        paramList_t *paramList, int timeout)
{
    const char *functionName = "put";

    int headerLen;
    char header[MAX_BUF_SIZE];
    headerLen = epicsSnprintf(header, sizeof(header), REQUEST_PUT, sysStr[sys], param, len);

    request_t request;
    char requestBuf[headerLen + len];
    request.data      = requestBuf;
    request.dataLen   = headerLen + len;
    request.actualLen = request.dataLen;

    response_t response;
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    memcpy(request.data, header, headerLen);
    memcpy(request.data + headerLen, value,  len);

    if(doRequest(&request, &response, timeout))
    {
        ERR_ARGS("[param=%s] request failed", param);
        return EXIT_FAILURE;
    }

    if(response.code != 200)
    {
        ERR_ARGS("[param=%s] server returned error code %d", param, response.code);
        return EXIT_FAILURE;
    }

    return paramList ? parseParamList(&response, paramList) : EXIT_SUCCESS;
}

int Eiger::parseHeader (response_t *response)
{
    int scanned;
    char *data = response->data;
    char *eol;

    scanned = sscanf(data, "%*s %d", &response->code);
    if(scanned != 1)
        return EXIT_FAILURE;

    data = strstr(data, EOL);
    if(!data)
        return EXIT_FAILURE;

    data += EOL_LEN;

    eol = strstr(data, EOL);
    while(eol && data != eol)
    {
        char *key, *colon;

        key   = data;
        colon = strchr(data, ':');

        if(!colon)
            return EXIT_FAILURE;
        *colon = '\0';

        if(!strcasecmp(key, "content-length"))
            sscanf(colon + 1, "%lu", &response->contentLength);
        else if(!strcasecmp(key, "connection"))
        {
            char value[MAX_BUF_SIZE];
            sscanf(colon + 1, "%s", value);
            response->reconnect = !strcasecmp(value, "close");
        }

        data = eol + EOL_LEN;
        eol = strstr(data, EOL);
    }

    if(!eol)
        return EXIT_FAILURE;

    response->content = data + EOL_LEN;
    response->headerLen = response->content - response->data;

    return EXIT_SUCCESS;
}

int Eiger::parseParamList (const response_t *response, paramList_t *paramList)
{
    const char *functionName = "parseParamList";

    if(!response->contentLength)
    {
        paramList->nparams = 0;
        return EXIT_SUCCESS;
    }

    struct json_token tokens[MAX_JSON_TOKENS];
    int err = parse_json(response->content, response->contentLength, tokens,
            MAX_JSON_TOKENS);

    if(err < 0)
    {
        ERR("unable to parse response json");
        return EXIT_FAILURE;
    }

    if(tokens[0].type != JSON_TYPE_ARRAY)
    {
        ERR("unexpected token type");
        return EXIT_FAILURE;
    }

    paramList->nparams = tokens[0].num_desc - 1;

    for(int i = 1; i <= tokens[0].num_desc; ++i)
    {
        // Assume destination is always big enough
        memcpy(paramList->params[i-1], tokens[i].ptr, tokens[i].len);
        paramList->params[i-1][tokens[i].len] = '\0';
    }

    return EXIT_SUCCESS;
}

int Eiger::parseSequenceId (const response_t *response, int *sequenceId)
{
    const char *functionName = "parseParamList";

    if(!response->contentLength)
    {
        ERR("no content to parse");
        return EXIT_FAILURE;
    }

    struct json_token tokens[MAX_JSON_TOKENS];
    int err = parse_json(response->content, response->contentLength, tokens,
            MAX_JSON_TOKENS);

    if(err < 0)
    {
        ERR("unable to parse response json");
        return EXIT_FAILURE;
    }

    if(tokens[0].type != JSON_TYPE_OBJECT)
    {
        ERR("unexpected token type");
        return EXIT_FAILURE;
    }

    struct json_token *seqIdToken = find_json_token(tokens, "sequence id");
    if(!seqIdToken)
    {
        seqIdToken = find_json_token(tokens, "series id");
        if(!seqIdToken)
        {
            ERR("unable to find 'series id' or 'sequence id' token");
            return EXIT_FAILURE;
        }
    }

    if(sscanf(seqIdToken->ptr, "%d", sequenceId) != 1)
    {
        ERR("unable to parse 'sequence_id' token");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

