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

#ifndef _OCEANBASE_OB_LUA_HANDLER_WIN_
#define _OCEANBASE_OB_LUA_HANDLER_WIN_

#if !defined(_WIN32)
#error "ob_lua_handler_win.h is for Windows only"
#endif

#include <thread>
#include <functional>

#include "lib/container/ob_vector.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/thread/threads.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace diagnose
{

// Stub: Lua diagnose uses Linux-specific APIs (epoll, Unix sockets, process_vm_readv)
class ObUnixDomainListener : public lib::Threads
{
public:
  explicit ObUnixDomainListener() : lib::Threads(1) {}
  void run1() override { /* no-op on Windows */ }
private:
  DISALLOW_COPY_AND_ASSIGN(ObUnixDomainListener);
};

// Stub: ObLuaHandler used by ob_lua_api (not built on Windows)
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
  void memory_update(const int size) { (void)size; }
  int process(const char* lua_code) { (void)lua_code; return common::OB_SUCCESS; }
  int64_t memory_usage() { return 0; }
  int register_destructor(Function func) { (void)func; return common::OB_SUCCESS; }
  int unregister_last_destructor() { return common::OB_SUCCESS; }
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
  DISALLOW_COPY_AND_ASSIGN(ObLuaHandler);
};

}
}

#endif // _OCEANBASE_OB_LUA_HANDLER_WIN_
