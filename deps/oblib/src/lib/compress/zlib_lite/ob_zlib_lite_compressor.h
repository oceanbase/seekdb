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

#ifndef OCEANBASE_COMMON_COMPRESS_ZLIB_LITE_COMPRESSOR_H_
#define OCEANBASE_COMMON_COMPRESS_ZLIB_LITE_COMPRESSOR_H_

#include "lib/compress/ob_compressor.h"

namespace oceanbase
{
namespace common
{
namespace ZLIB_LITE
{

class ObZlibLiteAdaptor;

class ObZlibLiteCompressor : public ObCompressor {
public:
  explicit ObZlibLiteCompressor();
  virtual ~ObZlibLiteCompressor();

  int  init(int32_t io_thread_count);
  void deinit();

  int compress(const char* src_buffer, const int64_t src_data_size, char* dst_buffer, const int64_t dst_buffer_size,
        int64_t& dst_data_size) override;

  int decompress(const char* src_buffer, const int64_t src_data_size, char* dst_buffer, const int64_t dst_buffer_size,
        int64_t& dst_data_size) override;

  const char* get_compressor_name() const override;

  int get_max_overflow_size(const int64_t src_data_size, int64_t& max_overflow_size) const override;
  virtual ObCompressorType get_compressor_type() const override;

  const char *compression_method() const;

private:
  ObZlibLiteAdaptor *adaptor_;
};
} // namespace ZLIB_LITE

} // namespace common
} // namespace oceanbase
#endif // OCEANBASE_COMMON_COMPRESS_ZLIB_LITE_COMPRESSOR_H_
