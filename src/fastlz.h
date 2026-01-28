#ifndef FASTLZ_H
#define FASTLZ_H

#define FASTLZ_VERSION_STRING "0.5.0"

#if (defined(__WIN32__) || defined(__WINNT__) || defined(WIN32) || defined(WINNT))
  #if defined(FASTLZ_DLL) && defined(FASTLZ_COMPRESSOR)
    #define FASTLZ_API __declspec(dllexport)
  #elif defined(FASTLZ_DLL) && defined(FASTLZ_DECOMPRESSOR)
    #define FASTLZ_API __declspec(dllimport)
  #else
    #define FASTLZ_API
  #endif
#else
  #define FASTLZ_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
  Compress a block of data.
  @param input pointer to the block of data to compress
  @param length size of the block of data in bytes
  @param output pointer to destination buffer
  @return size of compressed data.
  
  If compression fails (e.g. input data is uncompressible or output buffer
  is too small), the function will return 0.
*/
FASTLZ_API int fastlz_compress(const void* input, int length, void* output);

/**
  Decompress a block of data.
  @param input pointer to the block of data to decompress
  @param length size of the block of data in bytes
  @param output pointer to destination buffer
  @param maxout size of destination buffer
  @return size of decompressed data.
  
  If decompression fails (e.q. corrupted data or destination buffer is
  too small), the function will return 0.
*/
FASTLZ_API int fastlz_decompress(const void* input, int length, void* output, int maxout);

/**
  Compress a block of data, choosing the compression level.
  @param level compression level, either 1 or 2
  @param input pointer to the block of data to compress
  @param length size of the block of data in bytes
  @param output pointer to destination buffer
  @return size of compressed data.
*/
FASTLZ_API int fastlz_compress_level(int level, const void* input, int length, void* output);

#ifdef __cplusplus
}
#endif

#endif /* FASTLZ_H */