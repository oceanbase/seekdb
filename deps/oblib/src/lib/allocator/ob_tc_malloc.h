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

#ifndef __OB_COMMON_OB_BLOCK_ALLOCATOR_H__
#define __OB_COMMON_OB_BLOCK_ALLOCATOR_H__
#include <stdint.h>
#include <sys/mman.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObCtxInfo;
class ObMemLeakChecker;
extern const ObCtxInfo &get_global_ctx_info();
/// @fn print memory usage of each module
extern void ob_purge_memory_pool();
void reset_mem_leak_checker_label(const char *str);
void reset_mem_leak_checker_rate(int64_t rate);
extern ObMemLeakChecker &get_mem_leak_checker();
extern int64_t get_virtual_memory_used(int64_t *resident_size=nullptr);

/// set the memory as read-only
/// @note the ptr should have been returned by ob_malloc, and only the small block is supported now
/// @param prot See the manpage of mprotect
// int ob_mprotect(void *ptr, int prot);

}; // end namespace common
}; // end namespace oceanbase

#endif /* __OB_COMMON_OB_BLOCK_ALLOCATOR_H__ */
