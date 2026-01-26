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

#include "lib/utility/ob_platform_utils.h"
#include <unistd.h>

#ifdef __APPLE__
// Include mach headers here (not in the header file) to avoid macro conflicts
// mach/mach.h defines USEC_PER_SEC which conflicts with ob_trans_define.h
#include <mach/mach.h>
#endif

namespace oceanbase
{
namespace lib
{

int64_t ob_get_available_memory()
{
#ifdef __linux__
  return sysconf(_SC_AVPHYS_PAGES) * sysconf(_SC_PAGESIZE);
#elif defined(__APPLE__)
  vm_size_t page_size;
  vm_statistics64_data_t vm_stat;
  mach_port_t mach_port = mach_host_self();
  mach_msg_type_number_t count = sizeof(vm_stat) / sizeof(natural_t);
  if (host_page_size(mach_port, &page_size) == KERN_SUCCESS &&
      host_statistics64(mach_port, HOST_VM_INFO, (host_info64_t)&vm_stat, &count) == KERN_SUCCESS) {
    return (int64_t)vm_stat.free_count * page_size;
  }
  return -1;
#else
  return -1;
#endif
}

} // namespace lib
} // namespace oceanbase
