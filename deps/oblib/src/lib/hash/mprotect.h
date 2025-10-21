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

#include <sys/mman.h>

#if 0
struct MProtect
{
  int protect(int flag) {
    uint64_t page_size = 4096;
    return mprotect((void*)(~(page_size - 1) & ((int64_t)buf_)), page_size, flag);
  }
  char buf_[0];
} __attribute__((aligned(4096)));
struct MProtectGuard
{
  explicit MProtectGuard(MProtect& host): host_(host) {
    host.protect(PROT_READ | PROT_WRITE);
  }
  ~MProtectGuard(){
    host_.protect(PROT_READ);
  }
  MProtect& host_;
};
#else
struct MProtect
{};

struct MProtectGuard
{
  explicit MProtectGuard(MProtect& host) { (void)host; }
  ~MProtectGuard(){}
};
#endif
