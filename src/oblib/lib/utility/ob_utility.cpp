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

#include "ob_utility.h"
#ifdef _WIN32
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <sys/mman.h>
#endif
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

int64_t lower_align(int64_t input, int64_t align)
{
  int64_t ret = input;
  ret = (input + align - 1) & ~(align - 1);
  ret = ret - ((ret - input + align - 1) & ~(align - 1));
  return ret;
};

int64_t upper_align(int64_t input, int64_t align)
{
  int64_t ret = input;
  ret = (input + align - 1) & ~(align - 1);
  return ret;
};



int mprotect_page(const void *mem_ptr, int64_t len, int prot, const char *addr_name)
{
  int ret = OB_SUCCESS;
  int64_t mem_start = reinterpret_cast<int64_t>(mem_ptr);
  int64_t mem_end = mem_start + len;
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  int64_t pagesize = si.dwPageSize;
  const char *action = "protect";
  DWORD win_prot = PAGE_NOACCESS;
  if ((prot & 0x3) == 0x3) { // PROT_READ | PROT_WRITE
    action = "release";
    win_prot = PAGE_READWRITE;
  } else if (prot & 0x1) { // PROT_READ
    win_prot = PAGE_READONLY;
  }
#else
  int64_t pagesize = sysconf(_SC_PAGE_SIZE);
  const char *action = "protect";
  if ((prot & PROT_WRITE) && (prot & PROT_READ)) {
    action = "release";
  }
#endif
  if (mem_start != 0) {
    int64_t page_start = mem_start - (mem_start % pagesize);
    int64_t page_end = mem_end - (mem_end % pagesize) + pagesize;
    int64_t page_cnt = (page_end - page_start) / pagesize;
    void *mem_start_addr = reinterpret_cast<void*>(mem_start);
    void *mem_end_addr = reinterpret_cast<void*>(mem_end);
    void *page_start_addr = reinterpret_cast<void*>(page_start);
    void *page_end_addr = reinterpret_cast<void*>(page_end);
    if (page_cnt <= 0) {
      //The page to mprotect exceeds the protected address, mprotect cannot be added
      LIB_LOG(INFO, "can not mprotect mem_ptr", K(mem_start), K(pagesize), K(len), KCSTRING(addr_name), KCSTRING(action));
#ifdef _WIN32
    } else {
      DWORD old_prot;
      if (!VirtualProtect(page_start_addr, pagesize * page_cnt, win_prot, &old_prot)) {
        ret = OB_ERR_UNEXPECTED;
        LIB_LOG(WARN, "VirtualProtect mem_ptr failed",
                K(ret), K(GetLastError()), K(mem_start_addr), K(page_start_addr), K(pagesize),
                K(page_cnt), K(prot), KCSTRING(addr_name), KCSTRING(action));
      } else {
        LIB_LOG(INFO, "VirtualProtect success", K(mem_start_addr), K(mem_end_addr),
                K(page_start_addr), K(page_end_addr),
                K(pagesize), K(page_cnt), KCSTRING(addr_name), KCSTRING(action));
      }
    }
#else
    } else if (0 != mprotect(page_start_addr, pagesize * page_cnt, prot)) {
      ret = OB_ERR_UNEXPECTED;
      const char *errmsg = strerror(errno);
      LIB_LOG(WARN, "mprotect mem_ptr failed",
              K(ret), KCSTRING(errmsg), K(mem_start_addr), K(page_start_addr), K(pagesize),
              K(page_cnt), K(prot), KCSTRING(addr_name), KCSTRING(action));
    } else {
      LIB_LOG(INFO, "mprotect success", K(mem_start_addr), K(mem_end_addr),
              K(page_start_addr), K(page_end_addr),
              K(pagesize), K(page_cnt), KCSTRING(addr_name), KCSTRING(action));
    }
#endif
  }
  return ret;
}
char* upper_align_buf(char *in_buf, int64_t align)
{
  char *out_buf = NULL;

  out_buf = reinterpret_cast<char*>(upper_align(reinterpret_cast<int64_t>(in_buf), align));
  return out_buf;
}
} //common
} //oceanbase
