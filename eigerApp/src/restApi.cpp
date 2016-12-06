#include "restApi.h"

#include <stdexcept>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <frozen.h>     // JSON parser

#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsTime.h>

#include <fcntl.h>

#define API_VERSION             "1.5.0"
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
#define MAX_HTTP_RETRIES        1
#define MAX_MESSAGE_SIZE        512
#define MAX_BUF_SIZE            256
#define MAX_JSON_TOKENS         100

#define DEFAULT_TIMEOUT_INIT    240
#define DEFAULT_TIMEOUT_ARM     120
#define DEFAULT_TIMEOUT_CONNECT 1

#define ERR_PREFIX  "RestApi"
#define ERR(msg) fprintf(stderr, ERR_PREFIX "::%s: %s\n", functionName, msg)

#define ERR_ARGS(fmt,...) fprintf(stderr, ERR_PREFIX "::%s: " fmt "\n", \
    functionName, __VA_ARGS__)

// Requests

#define REQUEST_GET\
    "GET %s%s HTTP/1.1" EOL \
    "Host: %s" EOL\
    "Content-Length: 0" EOL \
    "Accept: " DATA_NATIVE EOH

#define REQUEST_GET_FILE\
    "GET %s%s HTTP/1.1" EOL \
    "Host: %s" EOL\
    "Content-Length: 0" EOL \
    "Accept: %s" EOH

#define REQUEST_PUT\
    "PUT %s%s HTTP/1.1" EOL \
    "Host: %s" EOL\
    "Accept-Encoding: identity" EOL\
    "Content-Type: " DATA_NATIVE EOL \
    "Content-Length: %lu" EOH

#define REQUEST_HEAD\
    "HEAD %s%s HTTP/1.1" EOL\
    "Host: %s" EOH

#define REQUEST_DELETE\
    "DELETE %s%s HTTP/1.1" EOL\
    "Host: %s" EOH

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

const char *RestAPI::sysStr [SSCount] = {
    "/detector/api/version",
    "/detector/api/"   API_VERSION "/config/",
    "/detector/api/"   API_VERSION "/status/",
    "/filewriter/api/" API_VERSION "/config/",
    "/filewriter/api/" API_VERSION "/status/",
    "/detector/api/"   API_VERSION "/command/",
    "/data/",
    "/monitor/api/"    API_VERSION "/config/",
    "/monitor/api/"    API_VERSION "/status/",
    "/monitor/api/"    API_VERSION "/images/",
    "/stream/api/"     API_VERSION "/config/",
    "/stream/api/"     API_VERSION "/status/",
    "/system/api/"     API_VERSION "/command/",
};

const char *RestAPI::triggerModeStr [TMCount] = {
    "ints", "inte", "exts", "exte"
};

int RestAPI::init (void)
{
    return osiSockAttach();
}

void RestAPI::deinit (void)
{
    osiSockRelease();
}

int RestAPI::buildMasterName (const char *pattern, int seqId, char *buf, size_t bufSize)
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

int RestAPI::buildDataName (int n, const char *pattern, int seqId, char *buf, size_t bufSize)
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

RestAPI::RestAPI (const char *hostname) :
    mSockFd(0), mSockMutex(), mSockClosed(true), mSockRetries(0)
{
    strncpy(mHostname, hostname, sizeof(mHostname));

    memset(&mAddress, 0, sizeof(mAddress));

    if(hostToIPAddr(mHostname, &mAddress.sin_addr))
        throw std::runtime_error("invalid hostname");

    mAddress.sin_family = AF_INET;
    mAddress.sin_port = htons(HTTP_PORT);
}

int RestAPI::initialize (void)
{
    return put(SSCommand, "initialize", "", 0, NULL, DEFAULT_TIMEOUT_INIT);
}

int RestAPI::arm (int *sequenceId)
{
    const char *functionName = "arm";

    request_t request = {};
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_PUT, sysStr[SSCommand], "arm", mHostname, 0lu);

    response_t response = {};
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

int RestAPI::trigger (int timeout, double exposure)
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
        epicsThreadSleep(exposure - diff);

    return EXIT_SUCCESS;
}

int RestAPI::disarm (void)
{
    return put(SSCommand, "disarm", "", 0, NULL);
}

int RestAPI::cancel (void)
{
    return put(SSCommand, "cancel", "", 0, NULL);
}

int RestAPI::abort (void)
{
    return put(SSCommand, "abort", "", 0, NULL);
}

int RestAPI::getString (sys_t sys, const char *param, char *value, size_t len, int timeout)
{
    return get(sys, param, value, len, timeout);
}

int RestAPI::getInt (sys_t sys, const char *param, int *value, int timeout)
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

int RestAPI::getDouble (sys_t sys, const char *param, double *value, int timeout)
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

int RestAPI::getBinState (sys_t sys, const char *param, bool *value,
        const char *oneState, int timeout)
{
    char buf[MAX_BUF_SIZE];

    if(get(sys, param, buf, sizeof(buf), timeout))
        return EXIT_FAILURE;

    *value = !strcmp(buf, oneState);

    return EXIT_SUCCESS;
}

int RestAPI::getBool (sys_t sys, const char *param, bool *value, int timeout)
{
    return getBinState (sys, param, value, "true", timeout);
}

int RestAPI::putString (sys_t sys, const char *param, const char *value,
        paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": \"%s\"}", value);

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int RestAPI::putInt (sys_t sys, const char *param,
        int value, paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": %d}", value);

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int RestAPI::putDouble (sys_t sys, const char *param,
        double value, paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": %lf}", value);

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int RestAPI::putBool (sys_t sys, const char *param,
        bool value, paramList_t *paramList, int timeout)
{
    char buf[MAX_BUF_SIZE];
    size_t bufLen = sprintf(buf, "{\"value\": %s}", value ? "true" : "false");

    return put(sys, param, buf, bufLen, paramList, timeout);
}

int RestAPI::getFileSize (const char *filename, size_t *size)
{
    const char *functionName = "getFileSize";

    request_t request = {};
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_HEAD, sysStr[SSData], filename, mHostname);

    response_t response = {};
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

int RestAPI::waitFile (const char *filename, double timeout)
{
    const char *functionName = "waitFile";

    epicsTimeStamp start, now;

    request_t request = {};
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_HEAD, sysStr[SSData], filename, mHostname);

    response_t response = {};
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

int RestAPI::getFile (const char *filename, char **buf, size_t *bufSize)
{
    return getBlob(SSData, filename, buf, bufSize, DATA_HDF5);
}

int RestAPI::deleteFile (const char *filename)
{
    const char *functionName = "deleteFile";

    request_t request = {};
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_DELETE, sysStr[SSData], filename, mHostname);

    response_t response = {};
    char responseBuf[MAX_MESSAGE_SIZE];
    response.data    = responseBuf;
    response.dataLen = sizeof(responseBuf);

    if(doRequest(&request, &response))
    {
        ERR_ARGS("[file=%s] DELETE request failed", filename);
        return EXIT_FAILURE;
    }

    if(response.code != 204)
    {
        ERR_ARGS("[file=%s] DELETE returned code %d", filename, response.code);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int RestAPI::getMonitorImage (char **buf, size_t *bufSize, size_t timeout)
{
    char param[MAX_BUF_SIZE];
    epicsSnprintf(param, sizeof(param), "monitor?timeout=%lu", timeout);
    return getBlob(SSMonImages, param, buf, bufSize, DATA_TIFF);
}

// Private members

int RestAPI::connect (void)
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

int RestAPI::setNonBlock (bool nonBlock)
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

int RestAPI::doRequest (const request_t *request, response_t *response, int timeout)
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
        if(mSockRetries++ < MAX_HTTP_RETRIES)
            goto retry;
        else
        {
            ERR("failed to send");
            status = EXIT_FAILURE;
            goto end;
        }
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

    if((received = recv(mSockFd, response->data, response->dataLen, 0) <= 0))
    {
        if(mSockRetries++ < MAX_HTTP_RETRIES)
            goto retry;
        else
        {
            ERR("failed to recv");
            status = EXIT_FAILURE;
            goto end;
        }
    }

    response->actualLen = (size_t) received;

    if((status = parseHeader(response)))
    {
        ERR("failed to parseHeader");
        goto end;
    }

    if(response->reconnect)
    {
        close(mSockFd);
        mSockClosed = true;
    }

end:
    mSockRetries = 0;
    mSockMutex.unlock();
    return status;

retry:
    close(mSockFd);
    mSockClosed = true;
    mSockMutex.unlock();
    return doRequest(request, response, timeout);
}

int RestAPI::get (sys_t sys, const char *param, char *value, size_t len,
        int timeout)
{
    const char *functionName = "get";

    request_t request = {};
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_GET, sysStr[sys], param, mHostname);

    response_t response = {};
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

int RestAPI::put (sys_t sys, const char *param, const char *value, size_t len,
        paramList_t *paramList, int timeout)
{
    const char *functionName = "put";

    int headerLen;
    char header[MAX_BUF_SIZE];
    headerLen = epicsSnprintf(header, sizeof(header), REQUEST_PUT, sysStr[sys], param, mHostname, len);

    request_t request = {};
    char requestBuf[headerLen + len];
    request.data      = requestBuf;
    request.dataLen   = headerLen + len;
    request.actualLen = request.dataLen;

    response_t response = {};
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

int RestAPI::getBlob (sys_t sys, const char *name, char **buf, size_t *bufSize,
        const char *accept)
{
    const char *functionName = "getBlob";
    int status = EXIT_SUCCESS;
    int received;
    size_t remaining;
    char *bufp;

    request_t request = {};
    char requestBuf[MAX_MESSAGE_SIZE];
    request.data      = requestBuf;
    request.dataLen   = sizeof(requestBuf);
    request.actualLen = epicsSnprintf(request.data, request.dataLen,
            REQUEST_GET_FILE, sysStr[sys], name, mHostname, accept);

    response_t response = {};
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

    // Send the request
    if(send(mSockFd, request.data, request.actualLen, 0) < 0)
    {
        if(mSockRetries++ < MAX_HTTP_RETRIES)
            goto retry;
        else
        {
            ERR("failed to send");
            status = EXIT_FAILURE;
            goto end;
        }
    }

    // Receive the first part of the response (header and some content)
    if((received = recv(mSockFd, response.data, response.dataLen, 0)) <= 0)
    {
        if(mSockRetries++ < MAX_HTTP_RETRIES)
            goto retry;
        else
        {
            ERR_ARGS("[sys=%d file=%s] failed to receive first part", sys, name);
            status = EXIT_FAILURE;
            goto end;
        }
    }

    if((status = parseHeader(&response)))
    {
        ERR_ARGS("[sys=%d file=%s] underlying parseResponse failed", sys, name);
        goto end;
    }

    if(response.code != 200)
    {
        if(sys != SSMonImages)
            ERR_ARGS("[sys=%d file=%s] file not found", sys, name);
        status = EXIT_FAILURE;
        goto end;
    }

    // Create the receive buffer and copy over what we already received
    *buf = (char*)malloc(response.contentLength);
    if(!*buf)
    {
        ERR_ARGS("[sys=%d file=%s] malloc(%lu) failed", sys, name, response.contentLength);
        status = EXIT_FAILURE;
        goto end;
    }

    // Assume that we got the whole header
    *bufSize = received - response.headerLen;
    memcpy(*buf, response.content, *bufSize);

    // Get the rest of the content (MSG_WAITALL can fail!)
    remaining = response.contentLength - *bufSize;
    bufp = *buf + *bufSize;

    while(remaining)
    {
        received = recv(mSockFd, bufp, remaining, MSG_WAITALL);

        if(received <= 0)
        {
            free(*buf);
            *buf = NULL;
            *bufSize = 0;

            if(mSockRetries++ < MAX_HTTP_RETRIES)
                goto retry;
            else
            {
                ERR_ARGS("[sys=%d file=%s] failed to receive second part", sys, name);
                status = EXIT_FAILURE;
                goto end;
            }
        }

        remaining -= received;
        bufp += received;
    }

    *bufSize = response.contentLength;

    if(response.reconnect)
    {
        close(mSockFd);
        mSockClosed = true;
    }

end:
    mSockRetries = 0;
    mSockMutex.unlock();
    return status;

retry:
    close(mSockFd);
    mSockClosed = true;
    mSockMutex.unlock();
    return getBlob(sys, name, buf, bufSize, accept);
}

int RestAPI::parseHeader (response_t *response)
{
    int scanned;
    char *data = response->data;
    char *eol;

    response->contentLength = 0;
    response->reconnect = false;

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

int RestAPI::parseParamList (const response_t *response, paramList_t *paramList)
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

    paramList->nparams = tokens[0].num_desc;

    for(int i = 1; i <= tokens[0].num_desc; ++i)
    {
        // Assume destination is always big enough
        memcpy(paramList->params[i-1], tokens[i].ptr, tokens[i].len);
        paramList->params[i-1][tokens[i].len] = '\0';
    }

    return EXIT_SUCCESS;
}

int RestAPI::parseSequenceId (const response_t *response, int *sequenceId)
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

