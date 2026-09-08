/*
 * Copyright (c) 2026 OceanBase.
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
#ifndef SEEKDB_LIB_RESOURCE_WASM_MEMORY_H_
#define SEEKDB_LIB_RESOURCE_WASM_MEMORY_H_

#ifdef __EMSCRIPTEN__
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <emscripten/heap.h>

namespace oceanbase
{
namespace lib
{
inline void *allocate_wasm_memory(uint64_t size, size_t alignment)
{
  if (alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0 || size == 0) {
    errno = EINVAL;
    return nullptr;
  }
  if (size > SIZE_MAX - alignment) {
    errno = ENOMEM;
    return nullptr;
  }
  // Bypass seekdb's malloc interception to avoid recursing into AChunkMgr.
  // The allocation is aligned up front; Wasm cannot partially unmap a malloc
  // allocation to trim its prefix/suffix like the native mmap path does.
  void *ptr = emscripten_builtin_memalign(alignment, static_cast<size_t>(size));
  if (ptr != nullptr) {
    memset(ptr, 0, static_cast<size_t>(size));
  } else {
    errno = ENOMEM;
  }
  return ptr;
}

inline void free_wasm_memory(void *ptr)
{
  emscripten_builtin_free(ptr);
}

inline int release_wasm_pages(size_t length)
{
  if (length == 0) { return 0; }
  // Linear memory cannot decommit pages. Report failure so callers retain
  // the charge or free the entire chunk, instead of reporting phantom savings.
  errno = ENOTSUP;
  return -1;
}
} // namespace lib
} // namespace oceanbase
#endif
#endif
