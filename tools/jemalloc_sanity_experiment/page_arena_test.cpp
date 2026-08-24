#include "lib/allocator/page_arena.h"

#include <cstdio>
#include <cstring>

using oceanbase::common::PageArena;
using oceanbase::common::jemalloc_sanity_free;
using oceanbase::common::jemalloc_sanity_malloc;

struct DirectPageAllocator
{
  void *alloc(const int64_t size)
  {
    return jemalloc_sanity_malloc(static_cast<size_t>(size));
  }
  void free(void *ptr) { jemalloc_sanity_free(ptr); }
  void freed(const int64_t size) { static_cast<void>(size); }
};

__attribute__((noinline)) int run_case(const char *mode)
{
  DirectPageAllocator page_allocator;
  PageArena<char, DirectPageAllocator> arena(4096, page_allocator, true);
  char *first = arena.alloc(13);
  if (nullptr == first) {
    return 2;
  }

  if (0 == std::strcmp(mode, "arena_overflow")) {
    first[13] = 'x';
  } else if (0 == std::strcmp(mode, "arena_aligned_overflow")) {
    char *aligned = arena.alloc_aligned(13, 64);
    if (nullptr == aligned) {
      return 5;
    }
    aligned[13] = 'x';
  } else if (0 == std::strcmp(mode, "arena_down_overflow")) {
    char *down = arena.alloc_down(13);
    if (nullptr == down) {
      return 6;
    }
    down[13] = 'x';
  } else if (0 == std::strcmp(mode, "arena_down_reuse_uaf")) {
    char *down = arena.alloc_down(13);
    if (nullptr == down) {
      return 7;
    }
    down[12] = 'x';
    arena.reuse();
    down[0] = 'x';
  } else if (0 == std::strcmp(mode, "arena_reuse_uaf")) {
    arena.reuse();
    first[0] = 'x';
  } else if (0 == std::strcmp(mode, "arena_free_uaf")) {
    arena.free();
    first[0] = 'x';
  } else if (0 == std::strcmp(mode, "arena_reset_remain_uaf")) {
    arena.free_remain_one_page();
    first[0] = 'x';
  } else if (0 == std::strcmp(mode, "arena_tracer_uaf")) {
    if (!arena.set_tracer()) {
      return 8;
    }
    char *after_tracer = arena.alloc(13);
    if (nullptr == after_tracer || !arena.revert_tracer()) {
      return 9;
    }
    after_tracer[0] = 'x';
  } else if (0 == std::strcmp(mode, "arena_partial_free_uaf")) {
    arena.partial_slow_free(0, 0, arena.total());
    first[0] = 'x';
  } else if (0 == std::strcmp(mode, "arena_partial_retrace_valid")) {
    if (!arena.set_tracer()) {
      return 10;
    }
    arena.partial_slow_free(0, 0, arena.total());
    if (!arena.set_tracer()) {
      return 11;
    }
    char *after_partial_free = arena.alloc(13);
    if (nullptr == after_partial_free) {
      return 12;
    }
    std::memset(after_partial_free, 0x3d, 13);
    if (0x3d != after_partial_free[12]) {
      return 13;
    }
  } else if (0 == std::strcmp(mode, "arena_aligned_bf_overflow")) {
    char *aligned = arena.alloc_aligned_bf(13, 64);
    if (nullptr == aligned) {
      return 14;
    }
    aligned[13] = 'x';
  } else if (0 == std::strcmp(mode, "arena_realloc_overflow")) {
    char *grown = arena.realloc(first, 13, 29);
    if (nullptr == grown) {
      return 15;
    }
    grown[29] = 'x';
  } else if (0 == std::strcmp(mode, "arena_reuse_valid")) {
    arena.reuse();
    char *reused = arena.alloc(13);
    if (nullptr == reused) {
      return 16;
    }
    std::memset(reused, 0x7c, 13);
    if (0x7c != reused[12]) {
      return 17;
    }
  } else if (0 == std::strcmp(mode, "arena_typed_valid")) {
    PageArena<uint64_t, DirectPageAllocator> typed_arena(
        256, page_allocator, true);
    uint64_t *guard = typed_arena.alloc_down(64);
    uint64_t *first_typed = typed_arena.alloc(19);
    if (nullptr == guard || nullptr == first_typed) {
      return 18;
    }
    std::memset(guard, 0x31, 64);
    if (0x31 != reinterpret_cast<unsigned char *>(guard)[63]) {
      return 19;
    }
  } else if (0 == std::strcmp(mode, "arena_typed_aligned_valid")) {
    PageArena<uint64_t, DirectPageAllocator> typed_arena(
        256, page_allocator, true);
    uint64_t *guard = typed_arena.alloc_down(64);
    uint64_t *aligned = typed_arena.alloc_aligned(19, 1);
    if (nullptr == guard || nullptr == aligned) {
      return 20;
    }
    std::memset(guard, 0x42, 64);
    if (0x42 != reinterpret_cast<unsigned char *>(guard)[63]) {
      return 21;
    }
  } else if (0 == std::strcmp(mode, "arena_typed_down_valid")) {
    PageArena<uint64_t, DirectPageAllocator> typed_arena(
        256, page_allocator, true);
    uint64_t *guard = typed_arena.alloc_down(64);
    uint64_t *down = typed_arena.alloc_down(3);
    if (nullptr == guard || nullptr == down) {
      return 22;
    }
    std::memset(guard, 0x53, 64);
    if (0x53 != reinterpret_cast<unsigned char *>(guard)[63]) {
      return 23;
    }
  } else if (0 == std::strcmp(mode, "arena_typed_aligned_bf_valid")) {
    PageArena<uint64_t, DirectPageAllocator> typed_arena(
        256, page_allocator, true);
    uint64_t *guard = typed_arena.alloc_down(64);
    uint64_t *aligned = typed_arena.alloc_aligned_bf(19, 1);
    if (nullptr == guard || nullptr == aligned) {
      return 24;
    }
    std::memset(guard, 0x64, 64);
    if (0x64 != reinterpret_cast<unsigned char *>(guard)[63]) {
      return 25;
    }
  } else {
    char *second = arena.alloc(13);
    if (nullptr == second) {
      return 3;
    }
    std::memset(first, 0x5a, 13);
    std::memset(second, 0x6b, 13);
    if (0x5a != first[12] || 0x6b != second[12]) {
      return 4;
    }
  }
  return 0;
}

int main(int argc, char **argv)
{
  return run_case(argc > 1 ? argv[1] : "arena_valid");
}
