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
#include "lib/ob_errno.h"
#include "lib/string/ob_string.h"
#include "rust/compression.h"

namespace oceanbase
{
namespace common
{
enum ObCompressorType : uint8_t
{
  INVALID_COMPRESSOR             = 0,
  NONE_COMPRESSOR                = 1,
  ZSTD_COMPRESSOR          = 2,
  ZLIB_COMPRESSOR                = 3,

  MAX_COMPRESSOR
};

const char *const all_compressor_name[] =
{
  "",
  "none",
  "zstd",
  "zlib_1.0",
};

STATIC_ASSERT(ARRAYSIZEOF(all_compressor_name) == ObCompressorType::MAX_COMPRESSOR, "compressor count mismatch");

const char *const compress_funcs[] =
{
  "none",
  "zlib_1.0",
  "zstd",
};

const char *const perf_compress_funcs[] =
{
  "zstd",
};

const char *const sql_temp_store_compress_funcs[] =
{
  "none",
  "zstd",
};

inline int get_compressor_type(const ObString &compressor_name,
                               ObCompressorType &compressor_type)
{
  int ret = OB_SUCCESS;
  if (compressor_name.ptr() == nullptr || compressor_name.length() == 0) {
    compressor_type = NONE_COMPRESSOR;
  } else if (compressor_name.case_compare("none") == 0) {
    compressor_type = NONE_COMPRESSOR;
  } else if (compressor_name.case_compare("zstd") == 0) {
    compressor_type = ZSTD_COMPRESSOR;
  } else if (compressor_name.case_compare("zlib_1.0") == 0) {
    compressor_type = ZLIB_COMPRESSOR;
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

inline int get_compressor_type(const char *compressor_name,
                               ObCompressorType &compressor_type)
{
  if (compressor_name == nullptr) {
    return OB_INVALID_ARGUMENT;
  } else if (compressor_name[0] == '\0') {
    return OB_NOT_SUPPORTED;
  }
  return get_compressor_type(ObString::make_string(compressor_name), compressor_type);
}

inline int compression_error_code(compression::Code code)
{
  switch (code) {
    case compression::Code::Ok: return OB_SUCCESS;
    case compression::Code::InvalidArgument: return OB_INVALID_ARGUMENT;
    case compression::Code::OutputTooSmall: return OB_BUF_NOT_ENOUGH;
    case compression::Code::InvalidData: return OB_ERR_COMPRESS_DECOMPRESS_DATA;
    case compression::Code::OutOfMemory:
    case compression::Code::AllocationFailed: return OB_ALLOCATE_MEMORY_FAILED;
    case compression::Code::InvalidState: return OB_STATE_NOT_MATCH;
    case compression::Code::ResourceLimit: return OB_EXCEED_MEM_LIMIT;
    case compression::Code::Unsupported: return OB_NOT_SUPPORTED;
    case compression::Code::SizeOverflow: return OB_SIZE_OVERFLOW;
    default: return OB_ERR_UNEXPECTED;
  }
}

} /* namespace common */
} /* namespace oceanbase */

#endif /* OB_COMPRESSOR_UTIL_H_ */
