#ifndef DECTRIS_COMPRESSION_H_
#define DECTRIS_COMPRESSION_H_

#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define COMPRESSION_ERROR SIZE_MAX

/* DECTRIS compression algorithms */
typedef enum {
    /* Bitshuffle with LZ4 compression (HDF5 framing)
     *
     * Data is stored as a series of bitshuffle transposed blocks compressed
     * with LZ4. The format is the same as the bitshuffle HDF5 filter.
     *
     * # See also
     *
     * https://github.com/kiyo-masui/bitshuffle
     * https://github.com/kiyo-masui/bitshuffle/blob/f57e2e50cc6855d5cf7689b9bc688b3ef1dffe02/src/bshuf_h5filter.c
     * https://lz4.github.io/lz4/
     * https://github.com/lz4/lz4/blob/master/doc/lz4_Block_format.md
     */
    COMPRESSION_BSLZ4,

    /* LZ4 compression (HDF5 framing)
     *
     * Data is stored as a series of LZ4 compressed blocks. The LZ4 filter
     * format for HDF5 is used for framing.
     *
     * # See also
     *
     * https://lz4.github.io/lz4/
     * https://github.com/lz4/lz4/blob/master/doc/lz4_Block_format.md
     * https://support.hdfgroup.org/services/filters/HDF5_LZ4.pdf
     */
    COMPRESSION_LZ4,

} CompressionAlgorithm;

/* Decompresses the contents of a source buffer into a destination buffer. */
size_t compression_decompress_buffer(CompressionAlgorithm algorithm,
                                     char* dst,
                                     size_t dst_size,
                                     const char* src,
                                     size_t src_size,
                                     size_t elem_size);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* DECTRIS_COMPRESSION_H_ */
