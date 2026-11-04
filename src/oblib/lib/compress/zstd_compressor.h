/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once

#include "lib/compress/ob_compressor.h"
#include "lib/ob_errno.h"
#include "rust/compression.h"

namespace oceanbase
{
namespace common
{

inline int zstd_error_code(compression::Code code)
{
  switch (code) {
    case compression::Code::Ok: return OB_SUCCESS;
    case compression::Code::InvalidArgument: return OB_INVALID_ARGUMENT;
    case compression::Code::OutputTooSmall: return OB_BUF_NOT_ENOUGH;
    case compression::Code::InvalidData: return OB_ERR_COMPRESS_DECOMPRESS_DATA;
    case compression::Code::AllocationFailed: return OB_ALLOCATE_MEMORY_FAILED;
    case compression::Code::InvalidState: return OB_STATE_NOT_MATCH;
    case compression::Code::ResourceLimit: return OB_EXCEED_MEM_LIMIT;
    case compression::Code::Unsupported: return OB_NOT_SUPPORTED;
    default: return OB_ERR_UNEXPECTED;
  }
}

class ZstdCompressor final : public ObCompressor
{
public:
  int compress(const char *src_buffer, int64_t src_data_size,
               char *dst_buffer, int64_t dst_buffer_size, int64_t &dst_data_size) override;
  int decompress(const char *src_buffer, int64_t src_data_size,
                 char *dst_buffer, int64_t dst_buffer_size, int64_t &dst_data_size) override;
  int get_max_overflow_size(int64_t src_data_size, int64_t &max_overflow_size) const override;
  const char *get_compressor_name() const override;
  ObCompressorType get_compressor_type() const override;
};

} // namespace common
} // namespace oceanbase
