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

#ifndef OCEANBASE_COMMON_COMPRESS_NONE_COMPRESSOR_H_
#define OCEANBASE_COMMON_COMPRESS_NONE_COMPRESSOR_H_
#include "lib/compress/ob_compressor.h"

//#define COMPRESSOR_NAME "none"
namespace oceanbase
{
namespace common
{
class ObNoneCompressor : public ObCompressor
{
public:
  ObNoneCompressor() {}
  virtual ~ObNoneCompressor() {}
  virtual int compress(const char *src_buffer,
                       const int64_t src_data_size,
                       char *dst_buffer,
                       const int64_t dst_buffer_size,
                       int64_t &dst_data_size);
  virtual int decompress(const char *src_buffer,
                         const int64_t src_data_size,
                         char *dst_buffer,
                         const int64_t dst_buffer_size,
                         int64_t &dst_data_size);
    virtual const char *get_compressor_name() const;
    virtual ObCompressorType get_compressor_type() const;
    virtual int get_max_overflow_size(const int64_t src_data_size,
                                      int64_t &max_overflow_size) const;
};

} //namespace common
} //namespace oceanbase
#endif // OCEANBASE_COMMON_COMPRESS_NONE_COMPRESSOR_H_
