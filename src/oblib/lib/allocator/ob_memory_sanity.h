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

#ifndef OCEANBASE_COMMON_OB_MEMORY_SANITY_H_
#define OCEANBASE_COMMON_OB_MEMORY_SANITY_H_

#include <cstdint>

namespace oceanbase {
namespace common {

// Describes the backing storage required for one sub-allocation protected by
// Memory Sanity. The caller owns the allocation policy; Memory Sanity owns the
// shadow granularity, minimum alignment, padding, and redzone layout.
struct SanityAllocLayout {
  int64_t user_size_ = 0;
  int64_t storage_size_ = 0;
  int64_t alignment_ = 0;
};

#if defined(ENABLE_SANITY) && defined(OB_HAVE_BUNDLED_JEMALLOC) &&             \
    defined(__linux__)

// Memory Sanity is a process-wide allocator mode rather than a PageArena
// property. It is active only when the process selected the jemalloc backend.
bool memory_sanity_enabled() noexcept;

// Fill layout for a requested allocation. requested_alignment == 0 means
// that the allocator did not request a stronger alignment of its own.
bool memory_sanity_prepare_allocation(int64_t user_size,
                                      int64_t requested_alignment,
                                      SanityAllocLayout &layout) noexcept;

// Publish a successful raw allocation to instrumented application code:
// unpoison only the requested bytes and keep padding/redzone inaccessible.
void memory_sanity_mark_allocated(const void *ptr,
                                  const SanityAllocLayout &layout) noexcept;

// Range-level operations used by arena lifetime transitions such as reuse,
// tracer rollback, and releasing a retained page.
void memory_sanity_poison(const void *ptr, int64_t size) noexcept;
void memory_sanity_unpoison(const void *ptr, int64_t size) noexcept;

#else

inline bool memory_sanity_enabled() noexcept { return false; }

inline bool
memory_sanity_prepare_allocation(int64_t user_size, int64_t requested_alignment,
                                 SanityAllocLayout &layout) noexcept {
  static_cast<void>(user_size);
  static_cast<void>(requested_alignment);
  layout = SanityAllocLayout();
  return false;
}

inline void
memory_sanity_mark_allocated(const void *ptr,
                             const SanityAllocLayout &layout) noexcept {
  static_cast<void>(ptr);
  static_cast<void>(layout);
}

inline void memory_sanity_poison(const void *ptr, int64_t size) noexcept {
  static_cast<void>(ptr);
  static_cast<void>(size);
}

inline void memory_sanity_unpoison(const void *ptr, int64_t size) noexcept {
  static_cast<void>(ptr);
  static_cast<void>(size);
}

#endif

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_MEMORY_SANITY_H_
