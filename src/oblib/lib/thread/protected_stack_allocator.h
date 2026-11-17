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

#ifndef PROTECTED_STACK_ALLOCATOR
#define PROTECTED_STACK_ALLOCATOR

#include <stdint.h>
#include <stddef.h>
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h>
#endif

namespace oceanbase {
namespace lib {

struct ObStackHeader
{
  constexpr static uint64_t MAGIC = 0xcaca12344321acac;
  bool check_magic() { return MAGIC == magic_; }
  ObStackHeader()
    : magic_(MAGIC) {}
  uint64_t magic_;
  
  ssize_t size_;
  // use thread instead of alloc
  uint64_t pth_;
  char *base_;
  bool has_guarded_page_;
};

class ProtectedStackAllocator
{
public:
  void *alloc(const ssize_t size);
  void *smart_call_alloc(const ssize_t size);
  void dealloc(void *ptr);
  static ObStackHeader *stack_header(void *ptr);
  static ssize_t page_size();
private:
  void *_alloc(const uint64_t ctx_id, const ssize_t size,
               const bool guard_page);
  void *__alloc(const uint64_t ctx_id, const ssize_t size,
                const bool guard_page);
};

extern ProtectedStackAllocator g_stack_allocer;

}  // lib
}  // oceanbase

#endif /* PROTECTED_STACK_ALLOCATOR */
