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

#include "ob_zlib_compressor.h"
#include "lib/compress/ob_compress_util.h"
#include "rust/compression.h"

namespace oceanbase
{
namespace common
{

int ObZlibCompressor::compress(const char *src_buffer,
                               const int64_t src_data_size,
                               char *dst_buffer,
                               const int64_t dst_buffer_size,
                               int64_t &dst_data_size)
{
  int ret = OB_SUCCESS;
  int64_t max_overflow_size = 0;
  if (NULL == src_buffer
      || 0 >= src_data_size
      || NULL == dst_buffer
      || 0 >= dst_buffer_size) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid compress argument, ",
        K(ret), KP(src_buffer), K(src_data_size), KP(dst_buffer), K(dst_buffer_size));
  } else if (OB_FAIL(get_max_overflow_size(src_data_size, max_overflow_size))) {
  } else if ((src_data_size + max_overflow_size) > dst_buffer_size) {
    ret = OB_BUF_NOT_ENOUGH;
    LIB_LOG(WARN, "dst buffer not enough, ",
        K(ret), K(src_data_size), K(max_overflow_size), K(dst_buffer_size));
  } else {
    const auto result = compression::zlib_compress(
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(src_buffer), src_data_size),
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(dst_buffer), dst_buffer_size),
        static_cast<int32_t>(compress_level_));
    if (compression::Code::Ok != result.code) {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LIB_LOG(WARN, "failed to compress data, ",
          K(ret), KP(src_buffer), K(src_data_size), K_(compress_level));
    } else {
      dst_data_size = result.size;
    }
  }
  return ret;
}

int ObZlibCompressor::decompress(const char *src_buffer,
                                 const int64_t src_data_size,
                                 char *dst_buffer,
                                 const int64_t dst_buffer_size,
                                 int64_t &dst_data_size)
{
  int ret = OB_SUCCESS;
  if (NULL == src_buffer
      || 0 >= src_data_size
      || NULL == dst_buffer
      || 0 >= dst_buffer_size) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid decompress argument, ",
        K(ret), KP(src_buffer), K(src_data_size), KP(dst_buffer), K(dst_buffer_size));
  } else {
    const auto result = compression::zlib_decompress(
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(src_buffer), src_data_size),
        rust::Slice<uint8_t>(reinterpret_cast<uint8_t *>(dst_buffer), dst_buffer_size));
    if (compression::Code::Ok != result.code) {
      ret = OB_ERR_COMPRESS_DECOMPRESS_DATA;
      LIB_LOG(WARN, "failed to decompress data, ",
          K(ret), KP(src_buffer), K(src_data_size));
    } else {
      dst_data_size = result.size;
    }
  }
  return ret;
}

int ObZlibCompressor::set_compress_level(const int64_t compress_level)
{
  int ret = OB_SUCCESS;
  if (compress_level < -1 || compress_level > 9) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid argument, ", K(ret), K(compress_level));
  } else {
    compress_level_ = compress_level;
  }
  return ret;
}

const char *ObZlibCompressor::get_compressor_name() const
{
  return all_compressor_name[ZLIB_COMPRESSOR];
}

ObCompressorType ObZlibCompressor::get_compressor_type() const
{
  return ObCompressorType::ZLIB_COMPRESSOR;
}

int ObZlibCompressor::get_max_overflow_size(const int64_t src_data_size,
                                            int64_t &max_overflow_size) const
{
  int ret = OB_SUCCESS;
  if (src_data_size < 0) {
    ret = OB_INVALID_ARGUMENT;
    LIB_LOG(WARN, "invalid argument, ", K(ret), K(src_data_size));
  } else {
    const uint64_t bound = compression::zlib_compress_bound(static_cast<size_t>(src_data_size));
    if (bound < static_cast<uint64_t>(src_data_size) || bound > static_cast<uint64_t>(INT64_MAX)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      max_overflow_size = static_cast<int64_t>(bound) - src_data_size;
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
