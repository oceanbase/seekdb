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

#define USING_LOG_PREFIX LIB_ALLOC

#include "ob_tc_malloc.h"
#include <malloc.h>
#include "lib/signal/ob_signal_struct.h"
#include "lib/utility/ob_tracepoint.h"
#include "lib/allocator/ob_mem_leak_checker.h"
#include "lib/alloc/malloc_hook.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;



#define __DM_MALLOC 1
#define __DM_MMAP 2
#define __DM_MMAP_ALIGNED 3
#define __DIRECT_MALLOC__ __DM_MMAP_ALIGNED

#if __DIRECT_MALLOC__ == __DM_MALLOC
void *direct_malloc(int64_t size)
{
  return ::malloc(size);
}
void direct_free(void *p, int64_t size)
{
  UNUSED(size);
  ::free(p);
}
#elif __DIRECT_MALLOC__ == __DM_MMAP
void *direct_malloc(int64_t size)
{
  void *p = NULL;
  if (MAP_FAILED == (p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1,
                              0))) {
    p = NULL;
  }
  return p;
}
void direct_free(void *p, int64_t size)
{
  if (NULL != p) {
    munmap(p, size);
  }
}
#elif __DIRECT_MALLOC__ == __DM_MMAP_ALIGNED
const static uint64_t MMAP_BLOCK_ALIGN = 1ULL << 21;


#endif // __DIRECT_MALLOC__

namespace oceanbase
{
namespace common
{

ObIAllocator *global_default_allocator = NULL;

ObMemLeakChecker &get_mem_leak_checker()
{
  return ObMemLeakChecker::get_instance();
}

void reset_mem_leak_checker_label(const char *str)
{
  get_mem_leak_checker().set_str(str);
  get_mem_leak_checker().reset();
}

void reset_mem_leak_checker_rate(int64_t rate)
{
  get_mem_leak_checker().set_rate(rate);
}

const ObCtxInfo &get_global_ctx_info()
{
  static ObCtxInfo info;
  return info;
}

void  __attribute__((constructor(MALLOC_INIT_PRIORITY))) init_global_memory_pool()
{
  auto& t = EventTable::instance();
  auto& c = get_mem_leak_checker();
  auto& a = AChunkMgr::instance();
  in_hook()= true;
  global_default_allocator = ObMallocAllocator::get_instance();
  in_hook()= false;
#ifndef OB_USE_ASAN
  abort_unless(OB_SUCCESS == install_ob_signal_handler());
#endif
  init_proc_map_info();
#ifdef ENABLE_SANITY
  abort_unless(init_sanity());
#endif
}

int64_t get_virtual_memory_used(int64_t *resident_size)
{
  static const int ps = sysconf(_SC_PAGESIZE);
  int64_t page_cnt = 0;
  int64_t res_page_cnt = 0;
  FILE *statm = fopen("/proc/self/statm", "r");
  if (OB_NOT_NULL(statm)) {
    fscanf(statm, "%ld %ld", &page_cnt, &res_page_cnt);
    fclose(statm);
    if (resident_size) *resident_size = res_page_cnt * ps;
  }
  return page_cnt * ps;
}

} // end namespace common
} // end namespace oceanbase
