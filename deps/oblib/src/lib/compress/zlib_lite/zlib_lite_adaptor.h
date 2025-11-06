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

#ifndef OCEANBASE_COMMON_COMPRESS_ZLIB_LITE_ADAPTOR_H_
#define OCEANBASE_COMMON_COMPRESS_ZLIB_LITE_ADAPTOR_H_

#include <stdint.h>

namespace oceanbase
{
namespace common 
{
namespace ZLIB_LITE
{

#define OB_PUBLIC_API __attribute__((visibility("default")))

class OB_PUBLIC_API ObZlibLiteAdaptor {
public:
  using allocator = void *(*)(int64_t);
  using deallocator = void (*)(void *);
  
public:
  explicit ObZlibLiteAdaptor();
  virtual ~ObZlibLiteAdaptor();

  int  init(allocator alloc, deallocator dealloc, int32_t io_thread_count);
  void deinit();
  
  int64_t compress(const char* src_buffer,
               const int64_t src_data_size,
               char* dst_buffer,
               const int64_t dst_buffer_size);
  int64_t decompress(const char* src_buffer,
                 const int64_t src_data_size,
                 char* dst_buffer,
                 const int64_t dst_buffer_size);

  const char *compression_method() const;

private:
  //has the same function as the compress and uncompress functions in the zlib source code.
  // return zlib error code, not oceanbase error code
  int zlib_compress(char *dest, int64_t *dest_len, const char *source, int64_t source_len);
  int zlib_decompress(char *dest, int64_t *dest_len, const char *source, int64_t source_len);

private:
  bool qpl_support_ = false;

  //zlib compress level,default is 1.
  static constexpr int compress_level = 1;

  //zlib window bits,in order to compress and decompress each other with the qpl algorithm, this parameter can only be -12.
  static constexpr int window_bits = -12;
};

#undef OB_PUBLIC_API

} // namespace ZLIB_LITE

} // namespace common
} // namespace oceanbase
#endif // OCEANBASE_COMMON_COMPRESS_ZLIB_LITE_ADAPTOR_H_
