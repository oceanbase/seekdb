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

#ifndef _OCEANBASE_OB_LUA_HANDLER_
#define _OCEANBASE_OB_LUA_HANDLER_

#include <thread>

#include "lib/container/ob_vector.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/thread/threads.h"

namespace oceanbase
{
namespace diagnose
{
class ObUnixDomainListener : public lib::Threads
{
  static constexpr int MAX_CONNECTION_QUEUE_LENGTH = 1;
  static constexpr int CODE_BUFFER_SIZE = 1 << 13; // 8K
  static constexpr const char *addr = "run/lua.sock";
public:
  explicit ObUnixDomainListener()
    : listen_fd_(-1)
  {}
  void run1() override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObUnixDomainListener);
private:
  int listen_fd_;
};

class ObLuaHandler
{
  using Function = std::function<int(void)>;
public:
  static constexpr int64_t LUA_MEMORY_LIMIT = (1UL << 25); // 32M
  ObLuaHandler() :
    alloc_count_(0),
    free_count_(0),
    alloc_size_(0),
    free_size_(0),
    destructors_(16, nullptr, "LuaHandler") {}
  void memory_update(const int size);
  int process(const char* lua_code);
  int64_t memory_usage() { return alloc_size_ - free_size_; }
  int register_destructor(Function func) { return destructors_.push_back(func); }
  int unregister_last_destructor() { return destructors_.remove(destructors_.size() - 1); }
  static ObLuaHandler& get_instance()
  {
    static ObLuaHandler instance;
    return instance;
  }
private:
  int64_t alloc_count_;
  int64_t free_count_;
  int64_t alloc_size_;
  int64_t free_size_;
  common::ObVector<Function> destructors_;
  static void *realloc_functor(void *userdata, void *ptr, size_t osize, size_t nsize);
  DISALLOW_COPY_AND_ASSIGN(ObLuaHandler);
};

}
}

#endif // _OCEANBASE_OB_LUA_HANDLER_
