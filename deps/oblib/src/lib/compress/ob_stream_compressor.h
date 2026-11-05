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

#ifndef  OCEANBASE_COMMON_STREAM_COMPRESS_OB_COMPRESSOR_H_
#define  OCEANBASE_COMMON_STREAM_COMPRESS_OB_COMPRESSOR_H_

#include <stdint.h>
#include "lib/string/ob_string.h"
#include "lib/compress/ob_compress_util.h"
namespace oceanbase
{
namespace common
{
class ObStreamCompressor
{
public:
  static const char *none_compressor_name;
public:
  ObStreamCompressor() {}
  virtual ~ObStreamCompressor() {}

  virtual int create_compress_ctx(void *&ctx) = 0;
  virtual int reset_compress_ctx(void *&ctx) = 0;
  virtual int free_compress_ctx(void *ctx) = 0;
  virtual int stream_compress(void *ctx,
                              const char *src_buffer,
                              const int64_t src_data_size,
                              char *dst_buffer,
                              const int64_t dst_buffer_size,
                              int64_t &dst_data_size) = 0;

  virtual int create_decompress_ctx(void *&ctx) = 0;
  virtual int reset_decompress_ctx(void *&ctx) = 0;
  virtual int free_decompress_ctx(void *ctx) = 0;
  virtual int stream_decompress(void *ctx,
                                const char *src_buffer,
                                const int64_t src_data_size,
                                char *dst_buffer,
                                const int64_t dst_buffer_size,
                                int64_t &dst_data_size) = 0;

  virtual int insert_uncompressed_block(void *dctx, const void *block, const int64_t block_size) = 0;
  virtual int get_compress_bound_size(const int64_t src_data_size, int64_t &bound_size) const = 0;

  virtual const char *get_compressor_name() const = 0;
  virtual ObCompressorType get_compressor_type() const = 0;
};

}//namespace common
}//namespace oceanbase

#endif
