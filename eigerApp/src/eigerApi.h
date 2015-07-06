#ifndef EIGER_API_H
#define EIGER_API_H

#include <epicsMutex.h>
#include <osiSock.h>

#define DEFAULT_TIMEOUT     10      // seconds

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

    SSCount,
} sys_t;

// Filewriter mode
typedef enum
{
    FWDisabled,
    FWEnabled,

    FWModeCount
} fwMode_t;

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

class Eiger
{
private:
    char mHostname[MAX_HOSTNAME];
    struct sockaddr_in mAddress;
    SOCKET mSockFd;
    epicsMutex mSockMutex;
    bool mSockClosed;

    static int breakPattern (char *pattern, char **prefix, int *prefixLen, char **suffix);

    int connect (void);

    int doRequest (const request_t *request, response_t *response, int timeout = DEFAULT_TIMEOUT);
    int get (sys_t sys, const char *param, char *value, size_t len, int timeout = DEFAULT_TIMEOUT);
    int put (sys_t sys, const char *param, const char *value, size_t len, paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);

    int command (const char *param, int *sequenceId, int timeout = DEFAULT_TIMEOUT);

    int parseHeader     (response_t *response);
    int parseParamList  (const response_t *response, paramList_t *paramList);
    int parseSequenceId (const response_t *response, int *sequenceId);

public:
    static const char *sysStr [SSCount];
    static const char *fwModeStr [FWModeCount];
    static const char *triggerModeStr [TMCount];

    static int init    (void);
    static void deinit (void);
    static int buildMasterName (char *pattern, int seqId, char *buf, size_t bufSize);
    static int buildDataName   (int n, char *pattern, int seqId, char *buf, size_t bufSize);

    Eiger(const char *hostname);

    int initialize (void);
    int arm        (int *sequenceId);
    int trigger    (int timeout);
    int disarm     (void);

    int getString (sys_t sys, const char *param, char *value, size_t len, int timeout = DEFAULT_TIMEOUT);
    int getInt    (sys_t sys, const char *param, int *value,              int timeout = DEFAULT_TIMEOUT);
    int getDouble (sys_t sys, const char *param, double *value,           int timeout = DEFAULT_TIMEOUT);
    int getBool   (sys_t sys, const char *param, bool *value,             int timeout = DEFAULT_TIMEOUT);

    int putString (sys_t sys, const char *param, const char *value, paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);
    int putInt    (sys_t sys, const char *param, int value,         paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);
    int putDouble (sys_t sys, const char *param, double value,      paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);
    int putBool   (sys_t sys, const char *param, bool value,        paramList_t *paramList, int timeout = DEFAULT_TIMEOUT);

    int getFileSize (const char *filename, size_t *size);
    int getFile     (const char *filename, char **buf, size_t *bufSize);
};

#endif
