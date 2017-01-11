#ifndef REST_API_H
#define REST_API_H

#include <epicsMutex.h>
#include <osiSock.h>

#define DEFAULT_TIMEOUT     20      // seconds

#define MAX_HOSTNAME        256
#define MAX_CHANGED_PARAMS  32
#define MAX_PARAM_NAME      64

// Subsystems
typedef enum
{
    SSAPIVersion,
    SSDetConfig,
    SSDetStatus,
    SSFWConfig,
    SSFWStatus,
    SSCommand,
    SSData,
    SSMonConfig,
    SSMonStatus,
    SSMonImages,
    SSStreamConfig,
    SSStreamStatus,
    SSSysCommand,

    SSCount,
} sys_t;

// Trigger mode
typedef enum {
    TMInternalSeries,   // INTS
    TMInternalEnable,   // INTE
    TMExternalSeries,   // EXTS
    TMExternalEnable,   // EXTE

    TMCount
} triggerMode_t;

// Modified parameters list
typedef struct
{
    int nparams;
    char params[MAX_CHANGED_PARAMS][MAX_PARAM_NAME];
} paramList_t;

// Forward declarations
typedef struct request  request_t;
typedef struct response response_t;
typedef struct socket   socket_t;

class RestAPI
{
private:
    char mHostname[MAX_HOSTNAME];
    struct sockaddr_in mAddress;
    SOCKET mSockFd;
    epicsMutex mSockMutex;
    bool mSockClosed;
    size_t mSockRetries;

    int connect (void);
    int setNonBlock (bool nonBlock);

    int doRequest (const request_t *request, response_t *response, int timeout = DEFAULT_TIMEOUT);
    int get (sys_t sys, const char *param, char *value, size_t len, int timeout = DEFAULT_TIMEOUT);
    int put (sys_t sys, const char *param, const char *value, size_t len, paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);

    int getBlob (sys_t sys, const char *name, char **buf, size_t *bufSize, const char *accept);

    int parseHeader     (response_t *response);
    int parseParamList  (const response_t *response, paramList_t *paramList);
    int parseSequenceId (const response_t *response, int *sequenceId);

public:
    static const char *sysStr [SSCount];
    static const char *triggerModeStr [TMCount];

    static int init    (void);
    static void deinit (void);
    static int buildMasterName (const char *pattern, int seqId, char *buf, size_t bufSize);
    static int buildDataName   (int n, const char *pattern, int seqId, char *buf, size_t bufSize);

    RestAPI (const char *hostname);

    int initialize (void);
    int arm        (int *sequenceId);
    int trigger    (int timeout, double exposure = 0.0);
    int disarm     (void);
    int cancel     (void);
    int abort      (void);
    int statusUpdate (void);

    int getString   (sys_t sys, const char *param, char *value, size_t len,           int timeout = DEFAULT_TIMEOUT);
    int getInt      (sys_t sys, const char *param, int *value,                        int timeout = DEFAULT_TIMEOUT);
    int getDouble   (sys_t sys, const char *param, double *value,                     int timeout = DEFAULT_TIMEOUT);
    int getBinState (sys_t sys, const char *param, bool *value, const char *oneState, int timeout = DEFAULT_TIMEOUT);
    int getBool     (sys_t sys, const char *param, bool *value,                       int timeout = DEFAULT_TIMEOUT);

    int putString (sys_t sys, const char *param, const char *value, paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);
    int putInt    (sys_t sys, const char *param, int value,         paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);
    int putDouble (sys_t sys, const char *param, double value,      paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);
    int putBool   (sys_t sys, const char *param, bool value,        paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);

    int getFileSize (const char *filename, size_t *size);
    int waitFile    (const char *filename, double timeout = DEFAULT_TIMEOUT);
    int getFile     (const char *filename, char **buf, size_t *bufSize);
    int deleteFile  (const char *filename);

    int getMonitorImage  (char **buf, size_t *bufSize, size_t timeout = 500);
};

#endif
