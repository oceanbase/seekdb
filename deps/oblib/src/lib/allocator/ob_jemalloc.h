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

#ifndef OCEANBASE_COMMON_OB_JEMALLOC_H_
#define OCEANBASE_COMMON_OB_JEMALLOC_H_

#include <cstddef>

#if defined(OB_HAVE_BUNDLED_JEMALLOC)
extern "C" {
extern const char *je_malloc_conf;
void *je_malloc(size_t size) noexcept;
void je_free(void *ptr) noexcept;
void *je_realloc(void *ptr, size_t size) noexcept;
void *je_memalign(size_t alignment, size_t size) noexcept;
int je_posix_memalign(void **memptr, size_t alignment, size_t size) noexcept;
size_t je_malloc_usable_size(void *ptr) noexcept;
int je_mallctl(const char *name, void *oldp, size_t *oldlenp,
               void *newp, size_t newlen) noexcept;
}
#endif

namespace oceanbase
{
namespace common
{

inline void *jemalloc_malloc(const size_t size)
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  return je_malloc(size);
#else
  (void)size;
  return nullptr;
#endif
}

inline void jemalloc_free(void *ptr)
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  je_free(ptr);
#else
  (void)ptr;
#endif
}

inline void *jemalloc_realloc(void *ptr, const size_t size)
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  return je_realloc(ptr, size);
#else
  (void)ptr;
  (void)size;
  return nullptr;
#endif
}

inline void *jemalloc_memalign(const size_t alignment, const size_t size)
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
#if defined(__APPLE__)
  void *ptr = nullptr;
  return 0 == je_posix_memalign(&ptr, alignment, size) ? ptr : nullptr;
#else
  return je_memalign(alignment, size);
#endif
#else
  (void)alignment;
  (void)size;
  return nullptr;
#endif
}

inline size_t jemalloc_usable_size(void *ptr)
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  return nullptr == ptr ? 0 : je_malloc_usable_size(ptr);
#else
  (void)ptr;
  return 0;
#endif
}

inline bool jemalloc_enable_background_threads()
{
#if defined(OB_HAVE_BUNDLED_JEMALLOC)
  bool enabled = true;
  return 0 == je_mallctl("background_thread", nullptr, nullptr,
                         &enabled, sizeof(enabled));
#else
  return true;
#endif
}

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_JEMALLOC_H_
