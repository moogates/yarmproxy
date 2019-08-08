#include "compress_wrap.h"
#include "quicklz.h"
#include "LogWrapper.h"


using namespace MyUtil;
using namespace xce::feed;

bool CompressWrap::Compress(const string& data, string * output) {
  char* compressed = new char[data.size() + 400];

  char * scratch = new char[QLZ_SCRATCH_COMPRESS];
  memset(scratch, 0, QLZ_SCRATCH_COMPRESS);
  size_t compressed_size = qlz_compress((void*)data.data(), compressed, data.size(), scratch);


  if(compressed_size < data.size()) {
    output->insert(0, compressed, compressed_size);
  }
  delete [] compressed;
  delete [] scratch;

  return compressed_size < data.size();
}

void CompressWrap::Decompress(const char * data, string * output) {
  size_t decompressed_size = qlz_size_decompressed(data);
  char* decompressed = new char[decompressed_size + 2];

  char * scratch = new char[QLZ_SCRATCH_DECOMPRESS];
  memset(scratch, 0, QLZ_SCRATCH_DECOMPRESS);

  size_t sz = qlz_decompress(data, decompressed, scratch);
  MCE_DEBUG("qlz_decompress size : " << sz << " - " << decompressed_size );

  // 最后两个字符, 是什么意思?
  decompressed[decompressed_size] = '\t';
  decompressed[decompressed_size + 1] = '\0';
  output->insert(0, decompressed, decompressed_size + 2);

  delete [] decompressed;
  delete [] scratch;
}

void CompressWrap::Decompress(const string& data, string * output) {
  Decompress(data.data(), output);
}

