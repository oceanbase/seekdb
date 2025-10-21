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

#ifndef DEPS_OBLIB_SRC_LIB_COMPRESS_ZSTD_OB_ZSTD_WRAPPER_H_
#define DEPS_OBLIB_SRC_LIB_COMPRESS_ZSTD_OB_ZSTD_WRAPPER_H_

#include <stddef.h>

namespace oceanbase
{
namespace common
{
namespace zstd
{

/*= Custom memory allocation functions */
typedef void* (*OB_ZSTD_allocFunction) (void* opaque, size_t size);
typedef void  (*OB_ZSTD_freeFunction) (void* opaque, void* address);
typedef struct { OB_ZSTD_allocFunction customAlloc; OB_ZSTD_freeFunction customFree; void* opaque; } OB_ZSTD_customMem;

#define OB_PUBLIC_API __attribute__ ((visibility ("default")))

class OB_PUBLIC_API ObZstdWrapper final
{
public:
  // for normal
  static int compress(
      OB_ZSTD_customMem &zstd_mem,
      const char *src_buffer,
      const size_t src_data_size,
      char *dst_buffer,
      const size_t dst_buffer_size,
      size_t &compress_ret_size);
  static int decompress(
      OB_ZSTD_customMem &zstd_mem,
      const char *src_buffer,
      const size_t src_data_size,
      char *dst_buffer,
      const size_t dst_buffer_size,
      size_t &dst_data_size);

  // for stream
  static int create_cctx(OB_ZSTD_customMem &ob_zstd_mem, void *&ctx);
  static void free_cctx(void *&ctx);
  static int compress_block(void *ctx, const char *src, const size_t src_size,
      char *dest, const size_t dest_capacity, size_t &compressed_size);
  static int create_dctx(OB_ZSTD_customMem &ob_zstd_mem, void *&ctx);
  static void free_dctx(void *&ctx);
  static int decompress_block(void *ctx, const char *src, const size_t src_size,
      char *dest, const size_t dest_capacity, size_t &decompressed_size);
  static size_t compress_bound(const size_t src_size);
  static int insert_block(void *ctx, const void *block, const size_t block_size);
};

#undef OB_PUBLIC_API

} // namespace zstd
} //namespace common
} //namespace oceanbase

#endif /* DEPS_OBLIB_SRC_LIB_COMPRESS_ZSTD_OB_ZSTD_WRAPPER_H_ */
