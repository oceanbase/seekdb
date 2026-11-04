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

#include "lib/compress/zstd_compressor.h"
#include <limits>

namespace oceanbase
{
namespace common
{
namespace
{

bool valid_buffers(const char *src, int64_t src_size, char *dst, int64_t dst_size)
{
  bool valid = src_size >= 0 && dst_size >= 0
      && (src != nullptr || src_size == 0) && (dst != nullptr || dst_size == 0)
      && static_cast<uint64_t>(src_size) <= std::numeric_limits<ptrdiff_t>::max()
      && static_cast<uint64_t>(dst_size) <= std::numeric_limits<ptrdiff_t>::max();
  if (valid && src_size > 0 && dst_size > 0) {
    const uintptr_t source = reinterpret_cast<uintptr_t>(src);
    const uintptr_t destination = reinterpret_cast<uintptr_t>(dst);
    valid = source <= destination
        ? destination - source >= static_cast<uint64_t>(src_size)
        : source - destination >= static_cast<uint64_t>(dst_size);
  }
  return valid;
}

} // namespace

int ZstdCompressor::compress(const char *src_buffer, int64_t src_data_size,
                             char *dst_buffer, int64_t dst_buffer_size, int64_t &dst_data_size)
{
  int ret = OB_SUCCESS;
  dst_data_size = 0;
  if (!valid_buffers(src_buffer, src_data_size, dst_buffer, dst_buffer_size)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    uint8_t empty = 0;
    const auto input = rust::Slice<const uint8_t>(
        src_buffer == nullptr ? &empty : reinterpret_cast<const uint8_t *>(src_buffer), src_data_size);
    const auto result = compression::zstd_compress(input,
        reinterpret_cast<uint8_t *>(dst_buffer), static_cast<size_t>(dst_buffer_size));
    ret = zstd_error_code(result.code);
    if (OB_SUCCESS == ret) {
      dst_data_size = static_cast<int64_t>(result.size);
    }
  }
  return ret;
}

int ZstdCompressor::decompress(const char *src_buffer, int64_t src_data_size,
                               char *dst_buffer, int64_t dst_buffer_size, int64_t &dst_data_size)
{
  int ret = OB_SUCCESS;
  dst_data_size = 0;
  if (!valid_buffers(src_buffer, src_data_size, dst_buffer, dst_buffer_size)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    uint8_t empty = 0;
    const auto input = rust::Slice<const uint8_t>(
        src_buffer == nullptr ? &empty : reinterpret_cast<const uint8_t *>(src_buffer), src_data_size);
    const auto result = compression::zstd_decompress(input,
        reinterpret_cast<uint8_t *>(dst_buffer), static_cast<size_t>(dst_buffer_size));
    ret = zstd_error_code(result.code);
    if (OB_SUCCESS == ret) {
      dst_data_size = static_cast<int64_t>(result.size);
    }
  }
  return ret;
}

int ZstdCompressor::get_max_overflow_size(int64_t src_data_size, int64_t &max_overflow_size) const
{
  int ret = OB_SUCCESS;
  max_overflow_size = 0;
  if (src_data_size < 0
      || static_cast<uint64_t>(src_data_size) > std::numeric_limits<size_t>::max()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const size_t bound = compression::zstd_compress_bound(static_cast<size_t>(src_data_size));
    if (bound < static_cast<uint64_t>(src_data_size)
        || bound > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      max_overflow_size = static_cast<int64_t>(bound) - src_data_size;
    }
  }
  return ret;
}

const char *ZstdCompressor::get_compressor_name() const
{
  return all_compressor_name[ZSTD_COMPRESSOR];
}

ObCompressorType ZstdCompressor::get_compressor_type() const
{
  return ZSTD_COMPRESSOR;
}

} // namespace common
} // namespace oceanbase
