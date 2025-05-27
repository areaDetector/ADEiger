#ifndef STREAM2_API_H
#define STREAM2_API_H

#include <stream2.h>
#include <NDArray.h>

typedef struct
{
    size_t series;
}stream_header_t;

class Stream2API
{
private:
    char *mHostname;
    void *mCtx, *mSock;
    stream2_image_msg *mImageMsg;
    int poll       (int timeout);   // timeout in seconds

public:
    Stream2API      (const char *hostname);
    ~Stream2API     (void);
    int getHeader  (stream_header_t *header, int timeout = 0);
    int waitFrame  (int *end, int timeout = 0);
    int getFrame   (NDArray **pArray, NDArrayPool *pNDArrayPool, int decompress);

    static int uncompress (stream_frame_t *frame, char *data = NULL);

};

#endif
