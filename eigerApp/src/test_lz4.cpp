// This program reads lz4 compressed buffers read from the stream and stream2 interfaces.
// It attempts to decompress them using the standard lz4 library.
// It also decompresses the stream2 buffer using the decompression functions from compress.
// If decompression is successful it writes the decompressed data as a TIFF file so it can be visualized.

#include <stdio.h>
#include <stdlib.h>
#include <lz4.h>
#include <compression.h>

int main(int argc, char *argv[]) 
{
  #define MAX_COMPRESSED_SIZE 100000
  #define MAX_DECOMPRESSED_SIZE 10000000
  char *compressedBuff = (char *)calloc(MAX_COMPRESSED_SIZE, 1);
  char *decompressedBuff = (char *)calloc(MAX_DECOMPRESSED_SIZE, 1);
  FILE *fp;
  size_t compressedSize, decompressedSize; 

  // Read the buffer read from the Stream interface with lz4 compression
  fp = fopen("Stream_lz4_data", "rb");
  compressedSize = fread(compressedBuff, 1, MAX_COMPRESSED_SIZE, fp);
  fclose(fp);
  decompressedSize = LZ4_decompress_safe(compressedBuff, (char *)decompressedBuff, compressedSize, MAX_DECOMPRESSED_SIZE);
  printf("Stream_lz4_data decompressed with lz4_decompress_safe, compressedSize=%d, decompressedSize=%d\n", (int)compressedSize, (int)decompressedSize);

  // Read the buffer read from the Stream2 interface with lz4 compression
  fp = fopen("Stream2_lz4_data", "rb");

  compressedSize = fread(compressedBuff, 1, MAX_COMPRESSED_SIZE, fp);
  fclose(fp);
  decompressedSize = LZ4_decompress_safe(compressedBuff, (char *)decompressedBuff, compressedSize, MAX_DECOMPRESSED_SIZE);
  printf("Stream2_lz4_data decompressed with lz4_decompress_safe, compressedSize=%d, decompressedSize=%d\n", (int)compressedSize, (int)decompressedSize);

  // Decompress with the Dectris routine
  decompressedSize = compression_decompress_buffer(COMPRESSION_LZ4, decompressedBuff, MAX_DECOMPRESSED_SIZE, compressedBuff, compressedSize, 4);
  printf("Stream2_lz4_data decompressed with Dectris compression_decompress_buffer, compressedSize=%d, decompressedSize=%d\n", 
         (int)compressedSize, (int)decompressedSize);
}
