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

#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_memory_sanity.h"
#include "lib/allocator/ob_malloc.h"

#if defined(ENABLE_SANITY) && defined(OB_HAVE_BUNDLED_JEMALLOC) &&             \
    defined(__linux__)

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <execinfo.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <jemalloc/jemalloc.h>
#include <sanity/sanity.h>

#ifndef MAP_FIXED_NOREPLACE
#error "jemalloc Sanity requires MAP_FIXED_NOREPLACE in the Linux headers"
#endif

// The packaged libsanity runtime interposes libc through dlsym and can recurse
// into malloc before seekdb has entered main().  The compiler pass only needs
// these globals and memory_sanity_abort(); libc checks are supplied by
// ob_sanity_libc_wrap.cpp without dlsym.
int64_t sanity_min_addr = 0;
int64_t sanity_max_addr = 0;

namespace oceanbase {
namespace common {

bool memory_sanity_enabled() noexcept { return is_jemalloc_backend(); }

namespace {

// The former OBMalloc Sanity integration searched the same upper bounds. Its
// maximum candidate was [0x0c0000000000, 0x600000000000): about 84 TiB of
// application address space plus 10.5 TiB of shadow; occupied mappings made it
// retreat in 128 GiB steps. Preserve that policy instead of the 64 GiB limit
// used by the first jemalloc proof of concept.
constexpr uintptr_t HEAP_MAX_CANDIDATES[] = {
    0x600000000000ULL,
    0x500000000000ULL,
    0x400000000000ULL,
};
constexpr size_t ADDRESS_SEARCH_STEP = 128ULL << 30;
constexpr size_t BOOTSTRAP_GAP = 2ULL << 20;
constexpr size_t REDZONE_SIZE = 16;
constexpr int64_t SHADOW_GRANULARITY = 8;
constexpr int64_t ARENA_REDZONE_SIZE = 8;

struct ReservedRegion {
  uintptr_t begin_ = 0;
  uintptr_t end_ = 0;

  size_t size() const { return end_ - begin_; }
  uintptr_t shadow_begin() const { return begin_ >> 3; }
  uintptr_t shadow_end() const { return end_ >> 3; }
  size_t shadow_size() const { return shadow_end() - shadow_begin(); }
  bool valid() const { return begin_ > 0 && end_ > begin_; }
};

enum class InitState : int {
  UNINITIALIZED = 0,
  INITIALIZING = 1,
  INITIALIZED = 2,
  FAILED = -1,
};

std::atomic<uintptr_t> heap_begin{0};
std::atomic<uintptr_t> heap_end{0};
std::atomic<uintptr_t> next_extent_addr{0};
std::atomic<InitState> init_state{InitState::UNINITIALIZED};
std::atomic<bool> map_fixed_noreplace_unavailable{false};
__thread bool initializing_arena = false;
unsigned sanity_arena = 0;

uintptr_t align_up(uintptr_t value, size_t alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

bool has_map_fixed_noreplace_semantics() {
  const long page_size = sysconf(_SC_PAGESIZE);
  if (page_size <= 0) {
    return false;
  }
  constexpr int BASE_FLAGS = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  void *occupied = mmap(nullptr, static_cast<size_t>(page_size), PROT_NONE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == occupied) {
    return false;
  }

  errno = 0;
  void *collision = mmap(occupied, static_cast<size_t>(page_size), PROT_NONE,
                         BASE_FLAGS | MAP_FIXED_NOREPLACE, -1, 0);
  const int collision_errno = errno;
  const bool supported = MAP_FAILED == collision && EEXIST == collision_errno;
  if (MAP_FAILED != collision && collision != occupied) {
    static_cast<void>(munmap(collision, static_cast<size_t>(page_size)));
  }
  static_cast<void>(munmap(occupied, static_cast<size_t>(page_size)));
  return supported;
}

bool reserve_exact(uintptr_t address, size_t size) {
  constexpr int BASE_FLAGS = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  void *result = mmap(reinterpret_cast<void *>(address), size, PROT_NONE,
                      BASE_FLAGS | MAP_FIXED_NOREPLACE, -1, 0);
  if (result != reinterpret_cast<void *>(address)) {
    if (MAP_FAILED != result) {
      static_cast<void>(munmap(result, size));
    }
    return false;
  }
  return true;
}

void release_region(const ReservedRegion &region) {
  if (region.valid()) {
    static_cast<void>(
        munmap(reinterpret_cast<void *>(region.begin_), region.size()));
    static_cast<void>(munmap(reinterpret_cast<void *>(region.shadow_begin()),
                             region.shadow_size()));
  }
}

bool reserve_region(const ReservedRegion &region) {
  if (!region.valid() || !reserve_exact(region.begin_, region.size())) {
    return false;
  }
  if (!reserve_exact(region.shadow_begin(), region.shadow_size())) {
    static_cast<void>(
        munmap(reinterpret_cast<void *>(region.begin_), region.size()));
    return false;
  }
  return true;
}

ReservedRegion probe_largest_region() {
  ReservedRegion best;
  for (const uintptr_t candidate_end : HEAP_MAX_CANDIDATES) {
    for (uintptr_t candidate_begin = candidate_end >> 3;
         candidate_begin < candidate_end;
         candidate_begin += ADDRESS_SEARCH_STEP) {
      const ReservedRegion candidate{candidate_begin, candidate_end};
      if (reserve_region(candidate)) {
        release_region(candidate);
        if (candidate.size() > best.size()) {
          best = candidate;
        }
        break;
      }
    }
  }
  return best;
}

bool reserve_sanity_region(ReservedRegion &selected) {
  // Retry the selection if another pre-main mapping appears between the probe
  // and the final reservation.
  for (int attempt = 0; attempt < 3; ++attempt) {
    selected = probe_largest_region();
    if (selected.valid() && reserve_region(selected)) {
      static_cast<void>(madvise(reinterpret_cast<void *>(selected.begin_),
                                selected.size(), MADV_DONTDUMP));
      static_cast<void>(
          madvise(reinterpret_cast<void *>(selected.shadow_begin()),
                  selected.shadow_size(), MADV_DONTDUMP));
      return true;
    }
  }
  return false;
}

bool make_shadow_accessible(uintptr_t address, size_t size) {
  const size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  const uintptr_t shadow_begin = address >> 3;
  const uintptr_t shadow_end = (address + size + 7) >> 3;
  const uintptr_t page_begin = shadow_begin & ~(page_size - 1);
  const uintptr_t page_end = align_up(shadow_end, page_size);
  return 0 == mprotect(reinterpret_cast<void *>(page_begin),
                       page_end - page_begin, PROT_READ | PROT_WRITE);
}

void set_shadow(void *ptr, size_t size, uint8_t value) {
  volatile uint8_t *shadow = reinterpret_cast<volatile uint8_t *>(
      reinterpret_cast<uintptr_t>(ptr) >> 3);
  const size_t shadow_size = (size + 7) >> 3;
  for (size_t i = 0; i < shadow_size; ++i) {
    shadow[i] = value;
  }
}

void unpoison_user_memory(void *ptr, size_t size) {
  // Returned pointers are max_align_t-aligned and therefore start on a shadow
  // boundary.  Write shadow directly to avoid sanity_unpoison()'s first-use
  // dlsym allocation while jemalloc may hold an arena lock.
  volatile uint8_t *shadow = reinterpret_cast<volatile uint8_t *>(
      reinterpret_cast<uintptr_t>(ptr) >> 3);
  const size_t full_blocks = size >> 3;
  for (size_t i = 0; i < full_blocks; ++i) {
    shadow[i] = 0;
  }
  if (0 != (size & 7)) {
    shadow[full_blocks] = static_cast<uint8_t>(size & 7);
  }
}

// Supply a new backing extent to the dedicated Sanity arena.  jemalloc asks
// for a size and alignment; the hook chooses an address inside the reserved
// application interval, maps that exact slice, and enables its shadow.
void *extent_alloc(extent_hooks_t *, void *new_addr, size_t size,
                   size_t alignment, bool *zero, bool *commit, unsigned) {
  // Do not honor jemalloc requests for an externally chosen exact address: all
  // Sanity extents must come from next_extent_addr inside our reserved range.
  if (nullptr != new_addr) {
    return nullptr;
  }
  uintptr_t current = next_extent_addr.load(std::memory_order_relaxed);
  const uintptr_t end = heap_end.load(std::memory_order_relaxed);
  uintptr_t allocated = 0;
  do {
    allocated =
        align_up(current, std::max(alignment, static_cast<size_t>(4096)));
    if (size > end || allocated >= end || size > end - allocated) {
      return nullptr;
    }
  } while (!next_extent_addr.compare_exchange_weak(current, allocated + size,
                                                   std::memory_order_relaxed));

  void *mapped =
      mmap(reinterpret_cast<void *>(allocated), size, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (mapped != reinterpret_cast<void *>(allocated)) {
    return nullptr;
  }
  if (!make_shadow_accessible(allocated, size)) {
    static_cast<void>(
        mmap(reinterpret_cast<void *>(allocated), size, PROT_NONE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0));
    return nullptr;
  }
  set_shadow(mapped, size, 0xF0);
  *zero = true;
  *commit = true;
  return mapped;
}

// Boolean-returning jemalloc extent hooks use false for success and true for
// failure or refusal, which is the opposite of the usual C boolean convention.

// Refuse to unmap the extent so jemalloc retains it for later arena reuse.
// Unmapping would punch a hole in the reserved Sanity interval that an
// unrelated mmap could occupy.
bool extent_dalloc(extent_hooks_t *, void *, size_t, bool, unsigned) {
  return true;
}

// The Sanity arena lives for the process lifetime.  Keep its virtual mappings
// intact even if jemalloc reaches the final-destruction callback during
// teardown; the operating system releases them when the process exits.
void extent_destroy(extent_hooks_t *, void *, size_t, bool, unsigned) {}

// extent_alloc() always returns a readable, writable, committed mapping and
// sets *commit to true, so any later commit request is already satisfied.
bool extent_commit(extent_hooks_t *, void *, size_t, size_t, size_t, unsigned) {
  return false;
}

// Decline decommit requests to preserve the fixed virtual mapping.  Physical
// pages can still be discarded through extent_purge() without creating a hole.
bool extent_decommit(extent_hooks_t *, void *, size_t, size_t, size_t,
                     unsigned) {
  return true;
}

// Discard physical pages while preserving the virtual address reservation.
// Return false when madvise succeeds, per jemalloc's extent-hook convention.
bool extent_purge(extent_hooks_t *, void *addr, size_t, size_t offset,
                  size_t length, unsigned) {
  return 0 !=
         madvise(static_cast<char *>(addr) + offset, length, MADV_DONTNEED);
}

// Splitting an extent only changes jemalloc's metadata; both resulting ranges
// remain within the same existing mapping, so no operating-system work is
// required and the operation succeeds.
bool extent_split(extent_hooks_t *, void *, size_t, size_t, size_t, bool,
                  unsigned) {
  return false;
}

// Merging adjacent extents likewise requires no mapping change.  Allow
// jemalloc to coalesce them in its arena metadata.
bool extent_merge(extent_hooks_t *, void *, size_t, void *, size_t, bool,
                  unsigned) {
  return false;
}

// Use the same MADV_DONTNEED implementation for both lazy and forced purge.
extent_hooks_t hooks = {extent_alloc,  extent_dalloc,   extent_destroy,
                        extent_commit, extent_decommit, extent_purge,
                        extent_purge,  extent_split,    extent_merge};

bool initialize_arena() {
  if (!has_map_fixed_noreplace_semantics()) {
    map_fixed_noreplace_unavailable.store(true, std::memory_order_release);
    return false;
  }
  ReservedRegion selected;
  if (!reserve_sanity_region(selected)) {
    return false;
  }
  heap_begin.store(selected.begin_, std::memory_order_relaxed);
  heap_end.store(selected.end_, std::memory_order_relaxed);
  next_extent_addr.store(selected.begin_ + BOOTSTRAP_GAP,
                         std::memory_order_relaxed);
  sanity_min_addr = static_cast<int64_t>(selected.begin_);
  sanity_max_addr = static_cast<int64_t>(selected.end_);
  extent_hooks_t *hook_ptr = &hooks;
  size_t arena_size = sizeof(sanity_arena);
  SanityDisableCheckRangeGuard guard;
  if (0 != je_mallctl("arenas.create", &sanity_arena, &arena_size, &hook_ptr,
                      sizeof(hook_ptr))) {
    release_region(selected);
    sanity_min_addr = 0;
    sanity_max_addr = 0;
    heap_begin.store(0, std::memory_order_relaxed);
    heap_end.store(0, std::memory_order_relaxed);
    return false;
  }
  return true;
}

bool ensure_initialized() {
  InitState state = init_state.load(std::memory_order_acquire);
  if (InitState::INITIALIZED == state) {
    return true;
  }
  InitState expected = InitState::UNINITIALIZED;
  if (init_state.compare_exchange_strong(expected, InitState::INITIALIZING,
                                         std::memory_order_acq_rel)) {
    initializing_arena = true;
    const bool success = initialize_arena();
    initializing_arena = false;
    init_state.store(success ? InitState::INITIALIZED : InitState::FAILED,
                     std::memory_order_release);
    return success;
  }
  while (InitState::INITIALIZING ==
         (state = init_state.load(std::memory_order_acquire))) {
    syscall(SYS_sched_yield);
  }
  return InitState::INITIALIZED == state;
}

__attribute__((constructor(200))) void initialize_jemalloc_sanity_arena() {
  if (!ensure_initialized()) {
    if (map_fixed_noreplace_unavailable.load(std::memory_order_acquire)) {
      static constexpr char MESSAGE[] =
          "seekdb: Sanity requires kernel support for "
          "MAP_FIXED_NOREPLACE\n";
      syscall(SYS_write, STDERR_FILENO, MESSAGE, sizeof(MESSAGE) - 1);
    } else {
      static constexpr char MESSAGE[] =
          "seekdb: failed to initialize jemalloc sanity arena\n";
      syscall(SYS_write, STDERR_FILENO, MESSAGE, sizeof(MESSAGE) - 1);
    }
    syscall(SYS_exit_group, 127);
    __builtin_unreachable();
  }
}

int allocation_flags(size_t alignment) {
  return MALLOCX_ARENA(sanity_arena) | MALLOCX_ALIGN(alignment) |
         MALLOCX_TCACHE_NONE;
}

void *allocate_aligned(size_t alignment, size_t size) {
  // arenas.create may allocate through the process-wide malloc symbol.  Those
  // bootstrap allocations must use jemalloc's default arena or initialization
  // would recursively wait for itself.
  if (initializing_arena) {
    SanityDisableCheckRangeGuard guard;
    return alignment <= alignof(std::max_align_t)
               ? je_malloc(size)
               : je_memalign(alignment, size);
  }
  if (!ensure_initialized()) {
    return nullptr;
  }
  alignment = std::max(alignment, alignof(std::max_align_t));
  if (0 != (alignment & (alignment - 1))) {
    return nullptr;
  }
  if (size > SIZE_MAX - REDZONE_SIZE) {
    return nullptr;
  }
  const size_t total = size + REDZONE_SIZE;
  void *ptr = nullptr;
  {
    SanityDisableCheckRangeGuard guard;
    ptr = je_mallocx(total, allocation_flags(alignment));
  }
  if (nullptr == ptr) {
    return nullptr;
  }
  // jemalloc returns the aligned allocation base directly.  Keep everything
  // after the requested user range poisoned as the redzone and size-class
  // slack; no prefix header or manually aligned interior pointer is needed.
  unpoison_user_memory(ptr, size);
  return ptr;
}

} // namespace

void *jemalloc_sanity_malloc(size_t size) noexcept {
  return allocate_aligned(alignof(std::max_align_t), size);
}

void jemalloc_sanity_free(void *ptr) noexcept {
  if (nullptr != ptr) {
    if (!sanity_addr_in_range(ptr, 0)) {
      SanityDisableCheckRangeGuard guard;
      je_free(ptr);
      return;
    }
    size_t usable = 0;
    {
      SanityDisableCheckRangeGuard guard;
      usable = je_sallocx(ptr, 0);
    }
    memory_sanity_poison(ptr, static_cast<int64_t>(usable));
    {
      SanityDisableCheckRangeGuard guard;
      je_dallocx(ptr, MALLOCX_TCACHE_NONE);
    }
  }
}

void *jemalloc_sanity_realloc(void *ptr, size_t size) noexcept {
  if (nullptr == ptr) {
    return jemalloc_sanity_malloc(size);
  }
  if (0 == size) {
    jemalloc_sanity_free(ptr);
    return nullptr;
  }
  if (!sanity_addr_in_range(ptr, 0)) {
    SanityDisableCheckRangeGuard guard;
    return je_realloc(ptr, size);
  }
  size_t old_usable = 0;
  {
    SanityDisableCheckRangeGuard guard;
    old_usable = je_sallocx(ptr, 0);
  }
  void *new_ptr = jemalloc_sanity_malloc(size);
  if (nullptr != new_ptr) {
    // The source range may include the poisoned redzone and size-class slack.
    // This is allocator-internal copying; shadow state is not copied and the
    // newly allocated user range already has the correct accessibility.
    {
      SanityDisableCheckRangeGuard guard;
      std::memcpy(new_ptr, ptr, std::min(old_usable, size));
    }
    jemalloc_sanity_free(ptr);
  }
  return new_ptr;
}

void *jemalloc_sanity_memalign(size_t alignment, size_t size) noexcept {
  return allocate_aligned(alignment, size);
}

size_t jemalloc_sanity_usable_size(void *ptr) noexcept {
  if (sanity_addr_in_range(ptr, 0)) {
    SanityDisableCheckRangeGuard guard;
    return je_sallocx(ptr, 0);
  }
  SanityDisableCheckRangeGuard guard;
  return je_malloc_usable_size(ptr);
}

bool jemalloc_sanity_enable_background_threads() noexcept {
  // A jemalloc background thread has no guarded allocator-call boundary and
  // legitimately accesses metadata that remains poisoned to application code.
  return true;
}

bool memory_sanity_prepare_allocation(int64_t user_size,
                                      int64_t requested_alignment,
                                      SanityAllocLayout &layout) noexcept {
  layout = SanityAllocLayout();
  if (user_size <= 0 ||
      user_size > INT64_MAX - (SHADOW_GRANULARITY - 1 + ARENA_REDZONE_SIZE) ||
      requested_alignment < 0 || requested_alignment > UINT32_MAX ||
      (0 != requested_alignment &&
       0 != (requested_alignment & (requested_alignment - 1)))) {
    return false;
  }

  const int64_t alignment = std::max(requested_alignment, SHADOW_GRANULARITY);
  layout.user_size_ = user_size;
  layout.storage_size_ =
      static_cast<int64_t>(
          align_up(static_cast<uintptr_t>(user_size), SHADOW_GRANULARITY)) +
      ARENA_REDZONE_SIZE;
  layout.alignment_ = alignment;
  return true;
}

void memory_sanity_mark_allocated(const void *ptr,
                                  const SanityAllocLayout &layout) noexcept {
  if (nullptr == ptr) {
    return;
  }
  if (layout.user_size_ <= 0 || layout.storage_size_ <= layout.user_size_ ||
      layout.alignment_ < SHADOW_GRANULARITY ||
      0 != (layout.alignment_ & (layout.alignment_ - 1)) ||
      0 != (reinterpret_cast<uintptr_t>(ptr) &
            (static_cast<uintptr_t>(layout.alignment_) - 1))) {
    memory_sanity_abort();
  }

  memory_sanity_unpoison(ptr, layout.user_size_);
  const int64_t redzone_offset = static_cast<int64_t>(
      align_up(static_cast<uintptr_t>(layout.user_size_), SHADOW_GRANULARITY));
  memory_sanity_poison(static_cast<const char *>(ptr) + redzone_offset,
                       layout.storage_size_ - redzone_offset);
}

void memory_sanity_poison(const void *ptr, int64_t size) noexcept {
  const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t range_begin = heap_begin.load(std::memory_order_relaxed);
  const uintptr_t range_end = heap_end.load(std::memory_order_relaxed);
  if (nullptr != ptr && size > 0 && begin >= range_begin && begin < range_end &&
      static_cast<uint64_t>(size) <= range_end - begin) {
    const uintptr_t aligned_begin = align_up(begin, SHADOW_GRANULARITY);
    if (aligned_begin - begin < static_cast<uint64_t>(size)) {
      const size_t aligned_size =
          static_cast<size_t>(size) - (aligned_begin - begin);
      set_shadow(reinterpret_cast<void *>(aligned_begin), aligned_size, 0xF0);
    }
  }
}

void memory_sanity_unpoison(const void *ptr, int64_t size) noexcept {
  const uintptr_t begin = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t range_begin = heap_begin.load(std::memory_order_relaxed);
  const uintptr_t range_end = heap_end.load(std::memory_order_relaxed);
  if (nullptr != ptr && size > 0 && begin >= range_begin && begin < range_end &&
      static_cast<uint64_t>(size) <= range_end - begin) {
    const uintptr_t aligned_begin = align_up(begin, SHADOW_GRANULARITY);
    if (aligned_begin - begin < static_cast<uint64_t>(size)) {
      const size_t aligned_size =
          static_cast<size_t>(size) - (aligned_begin - begin);
      unpoison_user_memory(reinterpret_cast<void *>(aligned_begin),
                           aligned_size);
    }
  }
}

} // namespace common
} // namespace oceanbase

extern "C" void memory_sanity_abort() {
  static constexpr char MESSAGE[] = "seekdb: memory sanity check failed\n";
  SanityDisableCheckRangeGuard guard;
  syscall(SYS_write, STDERR_FILENO, MESSAGE, sizeof(MESSAGE) - 1);
  void *frames[32];
  const int frame_count = backtrace(frames, sizeof(frames) / sizeof(frames[0]));
  backtrace_symbols_fd(frames, frame_count, STDERR_FILENO);
  __builtin_trap();
}

#endif
