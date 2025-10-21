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

#ifndef _OCEANBASE_OB_LUA_API_
#define _OCEANBASE_OB_LUA_API_

#include "lib/utility/ob_macro_utils.h"
#include "lib/allocator/ob_allocator.h"

class lua_State;

namespace oceanbase
{
namespace diagnose
{
extern void *alloc(const int size);
extern void free(void *ptr);

class APIRegister
{
  static constexpr int PRINT_BUFFER_SIZE = 1 << 20; // 1M
public:
  APIRegister() : conn_fd_(-1), print_offset_(0), print_capacity_(0), print_buffer_(nullptr)
  { 
    set_print_buffer((char *)diagnose::alloc(PRINT_BUFFER_SIZE), PRINT_BUFFER_SIZE);
  }
  ~APIRegister()
  {
    diagnose::free(print_buffer_);
    set_print_buffer(nullptr, 0);
  }
  void register_api(lua_State* L);
  int get_fd() { return conn_fd_; }
  void set_fd(int fd) { conn_fd_ = fd; }
  int flush();
  int append(const char *buffer, const int len);
  int append(const char *buffer) { return append(buffer, strlen(buffer)); }
  void set_print_buffer(char *buffer, const int cap)
  {
    print_buffer_ = buffer;
    print_capacity_ = cap;
  }
  static APIRegister& get_instance()
  {
    static APIRegister instance;
    return instance;
  }
private:
  int conn_fd_;
  int print_offset_;
  int print_capacity_;
  char *print_buffer_;
  DISALLOW_COPY_AND_ASSIGN(APIRegister);
};

} // diagnose
} // oceanbase


#endif //_OCEANBASE_OB_LUA_API_
