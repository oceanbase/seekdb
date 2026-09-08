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
#include <assert.h>
#include <atomic>
#include <stdio.h>
#include <thread>
#include <stddef.h>
#include "lib/atomic/ob_atomic.h"
#include "lib/atomic/atomic128.h"
#include "lib/charset/ob_config.h"
#include "lib/resource/wasm_memory.h"
#include "lib/alloc/alloc_struct.h"
#include "lib/utility/ob_platform_utils.h"
#include "easy_atomic.h"
// List corruption diagnostics are fatal in this standalone platform test.
// Keep the real list and its real headers, without linking the server logger.
#define OB_LOG_RET(...) abort()
#include "lib/list/ob_atomic_list.h"
#include "lib/queue/ob_link.h"
#undef OB_LOG_RET

#ifdef __EMSCRIPTEN__
static_assert(sizeof(void *) == 4, "This gate tests wasm32");
#endif
static_assert(SIZEOF_VOIDP == sizeof(void *));
static_assert(SIZEOF_CHARP == sizeof(char *));
static_assert(SIZEOF_SIZE_T == sizeof(size_t));
static_assert(SIZEOF_LONG == sizeof(long));
static_assert(SIZEOF_ULONG == sizeof(unsigned long));
static_assert(sizeof(types::uint128_t) == 16);
static_assert(alignof(types::uint128_t) == 16);

void load_from_peer(types::uint128_t &, types::uint128_t *);

static void test_list()
{
  using namespace oceanbase::common;
  static_assert(alignof(ObHeadNode) >= 8);
#ifdef __wasm32__
  ObHeadNode encoded;
  // Exercise pointers above 2 GiB without dereferencing synthetic addresses.
  SET_FREELIST_POINTER_VERSION(encoded, reinterpret_cast<void *>(UINT32_C(0xf2345678)), UINT64_C(0x87654321));
  assert(reinterpret_cast<uintptr_t>(FREELIST_POINTER(encoded)) == UINT32_C(0xf2345678));
  assert(FREELIST_VERSION(encoded) == UINT64_C(0x87654321));
  SET_FREELIST_POINTER_VERSION(encoded, FROM_PTR(nullptr), UINT64_C(0x100000000));
  assert(TO_PTR(FREELIST_POINTER(encoded)) == nullptr);
  assert(FREELIST_VERSION(encoded) == 0);
#endif

  struct Node { void *next; std::atomic<int> owner{0}; };
  Node nodes[64];
  ObAtomicList list;
  assert(list.init("wasm-test", offsetof(Node, next)) == OB_SUCCESS);
  assert(list.empty() && list.pop() == nullptr);
  for (auto &node : nodes) { list.push(&node); }
  std::thread threads[4];
  for (auto &thread : threads) {
    thread = std::thread([&] {
      for (int i = 0; i < 25000; ++i) {
        Node *node;
        while ((node = static_cast<Node *>(list.pop())) == nullptr) { PAUSE(); }
        assert(node->owner.fetch_add(1) == 0);
        assert(node->owner.fetch_sub(1) == 1);
        list.push(node);
      }
    });
  }
  for (auto &thread : threads) { thread.join(); }
  bool seen[64] = {};
  int count = 0;
  while (auto *node = static_cast<Node *>(list.pop())) {
    const ptrdiff_t index = node - nodes;
    assert(index >= 0 && index < 64 && !seen[index]);
    seen[index] = true;
    ++count;
  }
  assert(count == 64 && list.empty());
  list.push(&nodes[2]);
  nodes[0].next = FROM_PTR(&nodes[1]);
  list.batch_push(&nodes[0], &nodes[1]);
  assert(list.head() == &nodes[0]);
  assert(list.next(&nodes[0]) == &nodes[1]);
  assert(list.remove(&nodes[0]) == &nodes[0]);
  assert(list.popall() == &nodes[1]);
  assert(nodes[1].next == &nodes[2] && nodes[2].next == nullptr);
  assert(list.empty());
}

static void test_memory()
{
#ifdef __EMSCRIPTEN__
  using namespace oceanbase::lib;
  constexpr size_t alignment = 2 * 1024 * 1024;
  constexpr size_t size = alignment;
  assert(AChunk::calc_hold(size + 1) == AChunk::aligned(size + 1));
  assert(AChunk::calc_hold(size * 2 + 1) == AChunk::aligned(size * 2 + 1));
  for (int round = 0; round < 32; ++round) {
    auto *memory = static_cast<unsigned char *>(allocate_wasm_memory(size, alignment));
    assert(memory != nullptr && reinterpret_cast<uintptr_t>(memory) % alignment == 0);
    for (size_t i = 0; i < size; ++i) { assert(memory[i] == 0); }
    memset(memory, 0xab, size);
    assert(release_wasm_pages(size) == -1 && errno == ENOTSUP);
    assert(memory[0] == 0xab && memory[size - 1] == 0xab);
    free_wasm_memory(memory);
  }
  assert(release_wasm_pages(0) == 0);
  assert(allocate_wasm_memory(UINT64_C(1) << 32, alignment) == nullptr && errno == ENOMEM);
  assert(allocate_wasm_memory(16, 3) == nullptr && errno == EINVAL);
  assert(allocate_wasm_memory(emscripten_get_heap_max(), alignment) == nullptr && errno == ENOMEM);
#endif
}

static void test_platform()
{
  char error[128];
  assert(ob_strerror_r(EINVAL, error, sizeof(error)) == error);
  char name[16];
  assert(oceanbase::lib::ob_set_thread_name("wasm-platform") == 0);
  assert(oceanbase::lib::ob_get_thread_name(name, sizeof(name)) == 0);
  assert(strcmp(name, "wasm-platform") == 0);
  const int64_t main_id = ob_syscall_gettid();
  std::thread thread([&] {
    assert(ob_syscall_gettid() != main_id);
  });
  thread.join();
  easy_atomic_t lock = 0;
  easy_spin_lock(&lock);
  assert(!easy_trylock(&lock));
  easy_spin_unlock(&lock);
  assert(easy_trylock(&lock));
  easy_unlock(&lock);
  easy_spinrwlock_t rwlock = EASY_SPINRWLOCK_INITIALIZER;
  assert(easy_spinrwlock_rdlock(&rwlock) == EASY_OK);
  assert(easy_spinrwlock_try_wrlock(&rwlock) == EASY_AGAIN);
  assert(easy_spinrwlock_unlock(&rwlock) == EASY_OK);
  assert(easy_spinrwlock_wrlock(&rwlock) == EASY_OK);
  assert(easy_spinrwlock_try_rdlock(&rwlock) == EASY_AGAIN);
  assert(easy_spinrwlock_unlock(&rwlock) == EASY_OK);
}

int main()
{
  // The second wasm32 pointer slot is only 4-byte aligned. An accidental i64
  // atomic operation here traps instead of silently testing an aligned slot.
  alignas(8) oceanbase::common::ObLink links[2];
  oceanbase::common::ObLink sentinel;
  links[1].next_ = &sentinel;
  auto *slot = reinterpret_cast<uintptr_t *>(&links[1].next_);
  assert(oceanbase::common::set_last_bit(slot) == reinterpret_cast<uintptr_t>(&sentinel));
  assert(oceanbase::common::is_last_bit_set(ATOMIC_LOAD(slot)));
  oceanbase::common::unset_last_bit(slot);
  assert(links[1].next_ == &sentinel && links[0].next_ == nullptr);
  types::uint128_t pair = {0, UINT64_MAX};
  types::uint128_t expected = {1, 2};
  types::uint128_t desired = {3, 4};
  assert(!CAS128(&pair, expected, desired));
  assert(expected.lo == 0 && expected.hi == UINT64_MAX);
  assert(CAS128(&pair, expected, desired));
  assert(expected.lo == 0 && expected.hi == UINT64_MAX);
  types::uint128_t snapshot;
  LOAD128(snapshot, &pair);
  assert(snapshot.lo == 3 && snapshot.hi == 4);

  pair = {0, UINT64_MAX};
  std::atomic<bool> start{false};
  std::thread threads[4];
  constexpr uint64_t iterations = 25000;
  for (auto &thread : threads) {
    thread = std::thread([&] {
      while (!start.load(std::memory_order_acquire)) { PAUSE(); }
      for (uint64_t i = 0; i < iterations; ++i) {
        types::uint128_t before;
        load_from_peer(before, &pair);
        for (;;) {
          assert(before.hi == ~before.lo);
          types::uint128_t after = {before.lo + 1, ~(before.lo + 1)};
          if (CAS128(&pair, before, after)) { break; }
          // A failed CAS must refresh both halves of expected atomically.
        }
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (auto &thread : threads) { thread.join(); }
  LOAD128(snapshot, &pair);
  assert(snapshot.lo == 4 * iterations && snapshot.hi == ~snapshot.lo);

  alignas(8) uint64_t counter = UINT64_C(0xffffffff);
  assert(ATOMIC_FAA(&counter, 1) == UINT64_C(0xffffffff));
  assert(ATOMIC_LOAD(&counter) == UINT64_C(0x100000000));
  WEAK_BARRIER();
  test_list();
  test_memory();
  test_platform();
  puts("PASS: platform ABI, i64/CAS128 atomics, atomic list reuse, thread identity/names and easy locks");
#ifdef __EMSCRIPTEN__
  puts("PASS: wasm32 aligned memory, zero fill, reuse, OOM and chunk accounting");
#endif
}
