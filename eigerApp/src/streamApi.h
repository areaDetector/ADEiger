#ifndef STREAM_API_H
#define STREAM_API_H

#include <stdlib.h>

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

typedef struct stream_frame
{
    bool end;
    size_t series, frame;
    size_t shape[2];
    enum { UINT8, UINT16, UINT32 } type;

    char encoding[32];
    size_t compressedSize, uncompressedSize;

    void *data;
}stream_frame_t;

class StreamAPI
{
private:
    char *mHostname;
    void *mCtx, *mSock;

    int poll       (int timeout);   // timeout in seconds

public:
    StreamAPI      (const char *hostname);
    ~StreamAPI     (void);
    int getHeader  (stream_header_t *header, int timeout = 0);
    int getFrame   (stream_frame_t  *frame,  int timeout = 0);

    static int uncompress (stream_frame_t *frame, char *data = NULL);

};

#endif
