#ifndef STREAM_API_H
#define STREAM_API_H

#include <stdlib.h>
#include <stdint.h>
#include <NDArray.h>
#include <zmq.h>
#include <stream2.h>

enum stream_err
{
    STREAM_SUCCESS,
    STREAM_TIMEOUT,
    STREAM_WRONG_HTYPE,
    STREAM_ERROR,
};

typedef struct
{
    size_t series;
}stream_header_t;

class StreamAPI
{
private:
    char *mHostname;
    void *mCtx, *mSock;
    size_t mSeries;
    size_t mFrame;

    int poll       (int timeout);   // timeout in seconds

public:
    StreamAPI      (const char *hostname);
    ~StreamAPI     (void);
    int getHeader  (stream_header_t *header, int timeout = 0);
    int waitFrame  (int *end, int timeout = 0);
    int getFrame   (NDArray **pArray, NDArrayPool *pNDArrayPool, int decompress);
};

class Stream2API
{
private:
    char *mHostname;
    void *mCtx, *mSock;
    uint64_t mSeries_id;
    char* mImage_dtype;
    zmq_msg_t mMsg;
    stream2_image_msg *mImageMsg;
    uint64_t mImage_size_x;
    uint64_t mImage_size_y;
    uint64_t mNumber_of_images;
    struct {
        std::string tsStr;
        epicsTimeStamp ts;
    } mCachedTs;

    epicsTimeStamp extractTimeStampFromMessage(stream2_image_msg *message);
    int poll (int timeout);   // timeout in seconds

public:
    Stream2API     (const char *hostname);
    ~Stream2API    (void);
    int getHeader  (stream_header_t *header, int timeout = 0);
    int waitFrame  (int *end, int timeout = 0);
    int getFrame   (NDArray **pArray, NDArrayPool *pNDArrayPool, int decompress, bool extractTimeStamp);
};


#endif
