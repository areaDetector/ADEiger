#ifndef STREAM_API_H
#define STREAM_API_H

#include <stdlib.h>

typedef struct
{
    size_t series;
}stream_header_t;

typedef struct stream_frame
{
    bool end;
    size_t series, frame;
    size_t shape[2];
    enum { UINT16, UINT32 } type;

    char encoding[32];
    size_t compressedSize, uncompressedSize;

    void *data;
}stream_frame_t;

class StreamAPI
{
private:
    char *mHostname;
    void *mCtx, *mSock;

public:
    StreamAPI      (const char *hostname);
    ~StreamAPI     (void);
    int getHeader  (stream_header_t *header);
    int getFrame   (stream_frame_t  *frame);

    static int uncompress (stream_frame_t *frame, char *data = NULL);

};

#endif
