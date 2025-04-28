#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#elif defined(_WIN32)
#define INCL_EXTRA_HTON_FUNCTIONS 1
#include <winsock2.h>
#define be32toh(x) ntohl(x)
#define be64toh(x) ntohll(x)
#pragma comment(lib, "ws2_32.lib")
#else
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#include <endian.h>
#endif
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "bitshuffle/src/bitshuffle.h"
#include "lz4/lib/lz4.h"
#include "src/compression.h"

#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wtautological-constant-out-of-range-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#ifndef BSHUF_BLOCKED_MULT
#define BSHUF_BLOCKED_MULT 8
#endif

static uint32_t read_u32_be(const char* buf) {
    uint32_t value;
    memcpy(&value, buf, sizeof(value));
    return be32toh(value);
}

static uint64_t read_u64_be(const char* buf) {
    uint64_t value;
    memcpy(&value, buf, sizeof(value));
    return be64toh(value);
}

static _Bool parse_header(const char** const src,
                          const char* src_end,
                          uint64_t* const orig_size,
                          uint32_t* const block_size) {
    if (src_end - *src < 12)
        return 0;
    *orig_size = read_u64_be(*src);
    *block_size = read_u32_be(*src + 8);
    if (*orig_size >= SIZE_MAX || *block_size >= SIZE_MAX)
        return 0;
    *src += 12;
    return 1;
}

static _Bool decompress_bslz4_block(char** const dst,
                                    const char** const src,
                                    const char* const src_end,
                                    char* const tmp_buf,
                                    const uint32_t block_size,
                                    const size_t elem_size) {
    if (src_end - *src < 4)
        return 0;
    const uint32_t compressed_size = read_u32_be(*src);
    *src += 4;
    if (compressed_size > INT_MAX || (int)compressed_size > src_end - *src)
        return 0;
    if (LZ4_decompress_safe(*src, &tmp_buf[0], compressed_size, block_size) !=
        (int)block_size)
    {
        return 0;
    }
    if (bitshuf_decode_block(*dst, &tmp_buf[0], &tmp_buf[block_size],
                             block_size / elem_size, elem_size) != 0)
    {
        return 0;
    }
    *dst += block_size;
    *src += compressed_size;
    return 1;
}

static size_t decompress_buffer_bslz4_hdf5(char* dst,
                                           size_t dst_size,
                                           const char* src,
                                           size_t src_size,
                                           size_t elem_size) {
    if (elem_size == 0 || elem_size > UINT32_MAX / BSHUF_BLOCKED_MULT)
        return COMPRESSION_ERROR;

    const char* const src_end = src + src_size;
    uint64_t orig_size;
    uint32_t block_size;
    if (!parse_header(&src, src_end, &orig_size, &block_size))
        return COMPRESSION_ERROR;

    if (dst_size == 0)
        return (size_t)orig_size;

    if (orig_size > dst_size || (orig_size != 0 && block_size == 0) ||
        block_size % (BSHUF_BLOCKED_MULT * elem_size) != 0 ||
        block_size > INT_MAX)
    {
        return COMPRESSION_ERROR;
    }

    if (orig_size == 0)
        return 0;

    char* tmp_buf = malloc(block_size * 2);
    if (!tmp_buf)
        return COMPRESSION_ERROR;

    const char* const dst_end = dst + dst_size;
    const int leftover_bytes =
        orig_size % (int)(BSHUF_BLOCKED_MULT * elem_size);
    const uint64_t full_block_count = orig_size / block_size;
    const uint32_t last_block_size = (orig_size % block_size) - leftover_bytes;

    for (uint64_t block = 0; block < full_block_count; ++block) {
        if (!decompress_bslz4_block(&dst, &src, src_end, tmp_buf, block_size,
                                    elem_size))
        {
            free(tmp_buf);
            return COMPRESSION_ERROR;
        }
    }
    if (last_block_size > 0) {
        if (!decompress_bslz4_block(&dst, &src, src_end, tmp_buf,
                                    last_block_size, elem_size))
        {
            free(tmp_buf);
            return COMPRESSION_ERROR;
        }
    }
    free(tmp_buf);

    if (leftover_bytes > 0) {
        if (leftover_bytes > dst_end - dst || leftover_bytes != src_end - src)
            return COMPRESSION_ERROR;
        memcpy(dst, src, leftover_bytes);
    }

    return (size_t)orig_size;
}

static _Bool decompress_lz4_block(char** const dst,
                                  const char** const src,
                                  const char* const src_end,
                                  const uint32_t block_size) {
    if (src_end - *src < 4)
        return 0;
    const uint32_t compressed_size = read_u32_be(*src);
    *src += 4;
    if (compressed_size > INT_MAX || (int)compressed_size > src_end - *src)
        return 0;
    if (compressed_size == block_size) {
        memcpy(*dst, *src, block_size);
    } else {
        if (LZ4_decompress_safe(*src, *dst, compressed_size, block_size) !=
            (int)block_size)
        {
            return 0;
        }
    }
    *dst += block_size;
    *src += compressed_size;
    return 1;
}

static size_t decompress_buffer_lz4_hdf5(char* dst,
                                         size_t dst_size,
                                         const char* src,
                                         size_t src_size) {
    const char* const src_end = src + src_size;
    uint64_t orig_size;
    uint32_t block_size;
    if (!parse_header(&src, src_end, &orig_size, &block_size))
        return COMPRESSION_ERROR;

    if (dst_size == 0)
        return (size_t)orig_size;

    if (orig_size > dst_size || (orig_size != 0 && block_size == 0) ||
        block_size > INT_MAX)
    {
        return COMPRESSION_ERROR;
    }

    if (orig_size == 0)
        return 0;

    const uint64_t full_block_count = orig_size / block_size;
    const uint32_t last_block_size = orig_size % block_size;

    for (uint64_t block = 0; block < full_block_count; ++block) {
        if (!decompress_lz4_block(&dst, &src, src_end, block_size))
            return COMPRESSION_ERROR;
    }
    if (last_block_size > 0) {
        if (!decompress_lz4_block(&dst, &src, src_end, last_block_size))
            return COMPRESSION_ERROR;
    }

    return (size_t)orig_size;
}

size_t compression_decompress_buffer(CompressionAlgorithm algorithm,
                                     char* dst,
                                     size_t dst_size,
                                     const char* src,
                                     size_t src_size,
                                     size_t elem_size) {
    switch (algorithm) {
        case COMPRESSION_BSLZ4:
            return decompress_buffer_bslz4_hdf5(dst, dst_size, src, src_size,
                                                elem_size);
        case COMPRESSION_LZ4:
            return decompress_buffer_lz4_hdf5(dst, dst_size, src, src_size);
    }
    return COMPRESSION_ERROR;
}
