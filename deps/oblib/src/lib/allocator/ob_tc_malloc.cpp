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
#ifdef __linux__
#include <malloc.h>
#elif defined(__APPLE__)
#include <stdlib.h> // malloc is in stdlib.h on macOS
#elif defined(_WIN32)
#define _WINSOCKAPI_
#include <windows.h>
#include <psapi.h>
#endif
#include "lib/signal/ob_signal_struct.h"
#include "lib/utility/ob_tracepoint.h"
#include "lib/utility/ob_platform_utils.h"
#include "lib/alloc/malloc_hook.h"
#include "lib/alloc/alloc_func.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;

namespace oceanbase
{
namespace common
{

ObIAllocator *global_default_allocator = NULL;

const ObCtxInfo &get_global_ctx_info()
{
  static ObCtxInfo info;
  return info;
}

void  __attribute__((constructor(MALLOC_INIT_PRIORITY))) init_global_memory_pool()
{
  auto& t = EventTable::instance();
  auto& a = AChunkMgr::instance();
  // Set unlimited memory limit early to avoid 8GB default limit issue
  // This will be reset to proper value in main() after config is loaded
  set_memory_limit(INT64_MAX);
  in_hook()= true;
  global_default_allocator = ObMallocAllocator::get_instance();
  in_hook()= false;
}

int64_t get_virtual_memory_used(int64_t *resident_size)
{
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS pmc;
  if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
    if (resident_size) *resident_size = static_cast<int64_t>(pmc.WorkingSetSize);
    return static_cast<int64_t>(pmc.PagefileUsage);
  }
  if (resident_size) *resident_size = 0;
  return 0;
#else
  static const ssize_t ps = lib::ob_get_page_size();
  int64_t page_cnt = 0;
  int64_t res_page_cnt = 0;
  FILE *statm = fopen("/proc/self/statm", "r");
  if (OB_NOT_NULL(statm)) {
    fscanf(statm, "%ld %ld", &page_cnt, &res_page_cnt);
    fclose(statm);
    if (resident_size) *resident_size = res_page_cnt * ps;
  }
  return page_cnt * ps;
#endif
}

} // end namespace common
} // end namespace oceanbase
