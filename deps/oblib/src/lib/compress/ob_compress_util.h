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

#ifndef OB_COMPRESSOR_UTIL_H_
#define OB_COMPRESSOR_UTIL_H_
#include "lib/ob_define.h"
#include "lib/utility/ob_template_utils.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace common
{
enum ObCompressorType : uint8_t
{
  INVALID_COMPRESSOR             = 0 ,
  NONE_COMPRESSOR                = 1,
  LZ4_COMPRESSOR                 = 2,
  SNAPPY_COMPRESSOR              = 3,
  ZLIB_COMPRESSOR                = 4,
  ZSTD_COMPRESSOR                = 5,
  ZSTD_1_3_8_COMPRESSOR          = 6,
  LZ4_191_COMPRESSOR             = 7,
  STREAM_LZ4_COMPRESSOR          = 8,//used for clog rpc compress
  STREAM_ZSTD_COMPRESSOR         = 9,//used for clog rpc compress
  STREAM_ZSTD_1_3_8_COMPRESSOR   = 10,//used for clog rpc compress
  ZLIB_LITE_COMPRESSOR           = 11,//Composed of qpl+zlib

  MAX_COMPRESSOR
};

const char *const all_compressor_name[] =
{
  "",
  "none",
  "lz4_1.0",
  "snappy_1.0",
  "zlib_1.0",
  "zstd_1.0",
  "zstd_1.3.8",
  "lz4_1.9.1",
  "stream_lz4_1.0",
  "stream_zstd_1.0",
  "stream_zstd_1.3.8",
  "zlib_lite_1.0",
};

STATIC_ASSERT(ARRAYSIZEOF(all_compressor_name) == ObCompressorType::MAX_COMPRESSOR, "compressor count mismatch");

#define DISABLED_ZLIB_1_COMPRESS_IDX 3

const char *const compress_funcs[] =
{
  "lz4_1.0",
  "none",
  "snappy_1.0",
  "zlib_1.0", // temporarily disable zlib_1.0
  "zstd_1.0",
  "zstd_1.3.8",
  "lz4_1.9.1",
  "zlib_lite_1.0",
};

const char *const perf_compress_funcs[] =
{
  "lz4_1.0",
  "zstd_1.0",
  "zstd_1.3.8",
};

const char *const syslog_compress_funcs[] =
{
  "none",
  "zstd_1.0",
  "zstd_1.3.8",
};

const char *const sql_temp_store_compress_funcs[] =
{
  "none",
  "zstd",
  "lz4",
  "snappy",
  "zlib"
};

} /* namespace common */
} /* namespace oceanbase */

using oceanbase::common::ObIAllocator;
static void *ob_zstd_malloc(void *opaque, size_t size)
{
  void *buf = NULL;
  if (NULL != opaque) {
    ObIAllocator *allocator = reinterpret_cast<ObIAllocator*> (opaque);
    buf = allocator->alloc(size);
  }
  return buf;
}

static void ob_zstd_free(void *opaque, void *address)
{
  if (NULL != opaque) {
    ObIAllocator *allocator = reinterpret_cast<ObIAllocator*> (opaque);
    allocator->free(address);
  }
}

static void *ob_zlib_alloc(void *opaque, unsigned int items, unsigned int size)
{
  void *ret = NULL;
  ObIAllocator *allocator = static_cast<ObIAllocator *>(opaque);
  if (OB_NOT_NULL(allocator)) {
    ret = allocator->alloc(items * size);
  }
  return ret;
}

static void ob_zlib_free(void *opaque, void *address)
{
  ObIAllocator *allocator = static_cast<ObIAllocator *>(opaque);
  if (OB_NOT_NULL(allocator)) {
    allocator->free(address);
  }
}

#endif /* OB_COMPRESSOR_UTIL_H_ */
