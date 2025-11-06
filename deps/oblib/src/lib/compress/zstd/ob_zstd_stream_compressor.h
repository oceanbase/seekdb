/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_COMMON_STREAM_COMPRESS_ZSTD_COMPRESSOR_
#define OCEANBASE_COMMON_STREAM_COMPRESS_ZSTD_COMPRESSOR_

#include "lib/compress/ob_stream_compressor.h"

namespace oceanbase
{
namespace common
{
namespace zstd
{

class ObZstdStreamCompressor : public ObStreamCompressor
{
public:
  explicit ObZstdStreamCompressor(ObIAllocator &allocator)
    : allocator_(allocator) {}
  virtual ~ObZstdStreamCompressor() {}

  const char *get_compressor_name() const;
  ObCompressorType get_compressor_type() const;

  int create_compress_ctx(void *&ctx);
  int reset_compress_ctx(void *&ctx);
  int free_compress_ctx(void *ctx);

  // a block is considered not compressible enough,  compressed_size will be zero
  int stream_compress(void *ctx, const char *src, const int64_t src_size, char *dest, const int64_t dest_capacity, int64_t &compressed_size);

  int create_decompress_ctx(void *&ctx);
  int reset_decompress_ctx(void *&ctx);
  int free_decompress_ctx(void *ctx);

  int stream_decompress(void *ctx, const char *src, const int64_t src_size, char *dest, const int64_t dest_capacity, int64_t &decompressed_size);

  int get_compress_bound_size(const int64_t src_size, int64_t &bound_size) const;
  int insert_uncompressed_block(void *dctx, const void *block, const int64_t block_size);
private:
  ObIAllocator &allocator_;
};
} // namespace zstd
} //namespace common
} //namespace oceanbase
#endif //OCEANBASE_COMMON_STREAM_COMPRESS_ZSTD_COMPRESSOR_
