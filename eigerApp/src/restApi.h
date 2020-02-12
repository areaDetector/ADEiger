#ifndef REST_API_H
#define REST_API_H

#include <string>
#include <epicsMutex.h>
#include <osiSock.h>

#define DEFAULT_TIMEOUT     20      // seconds

#define MAX_CHANGED_PARAMS  32
#define MAX_PARAM_NAME      64

#define EIGER1 1
#define EIGER2 2

// Subsystems
typedef enum
{
    SSAPIVersion,
    SSDetConfig,
    SSDetStatus,
    SSFWConfig,
    SSFWStatus,
    SSFWCommand,
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

// Forward declarations
typedef struct request  request_t;
typedef struct response response_t;
typedef struct socket   socket_t;

class RestAPI
{
private:
    std::string mHostname;
    int mPort;
    struct sockaddr_in mAddress;
    size_t mNumSockets;
    socket_t *mSockets;
    std::string mSysStr[SSCount];

    int connect (socket_t *s);
    int setNonBlock (socket_t *s, bool nonBlock);

    int doRequest (const request_t *request, response_t *response, int timeout = DEFAULT_TIMEOUT);

    int getBlob (sys_t sys, const char *name, char **buf, size_t *bufSize, const char *accept);

public:
    static int buildMasterName (const char *pattern, int seqId, char *buf, size_t bufSize);
    static int buildDataName   (int n, const char *pattern, int seqId, char *buf, size_t bufSize);

    RestAPI (std::string const & hostname, int port=80, int eigerModel=1, size_t numSockets=5);

    int get (sys_t sys, std::string const & param, std::string & value, int timeout = DEFAULT_TIMEOUT);
    int put (sys_t sys, std::string const & param, std::string const & value = "", std::string * reply = NULL, int timeout = DEFAULT_TIMEOUT);

    int initialize (void);
    int arm        (int *sequenceId);
    int trigger    (int timeout, double exposure = 0.0);
    int disarm     (void);
    int cancel     (void);
    int abort      (void);
    int wait       (void);
    int statusUpdate (void);

    int getFileSize (const char *filename, size_t *size);
    int waitFile    (const char *filename, double timeout = DEFAULT_TIMEOUT);
    int getFile     (const char *filename, char **buf, size_t *bufSize);
    int deleteFile  (const char *filename);

    int getMonitorImage  (char **buf, size_t *bufSize, size_t timeout = 500);
};

#endif
