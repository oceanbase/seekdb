// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>

extern "C" uint64_t seekdb_rust_thread_work(uint64_t, uint32_t);
extern "C" uint64_t seekdb_rust_spawn_workers();
extern "C" uint64_t seekdb_rust_atomic_sum();
extern "C" uint64_t seekdb_rust_locked_sum();
extern "C" uint32_t seekdb_rust_tls_drops();

static uint64_t expected_sum(uint64_t id)
{
  return 10000 * id + uint64_t(9999) * 10000 / 2;
}

int main()
{
  assert(seekdb_rust_tls_drops() == 0);
  uint64_t expected = 0;
  for (unsigned round = 0; round < 3; ++round) {
    std::array<uint64_t, 2> sums{};
    std::array<std::thread, 2> workers;
    for (unsigned i = 0; i < 2; ++i) {
      workers[i] = std::thread([&, i] {
        sums[i] = seekdb_rust_thread_work((uint64_t(1) << 48) + i, 10000);
      });
    }
    for (auto &worker : workers) worker.join();
    printf("round %u: TLS drops after C++ workers = %u\n", round, seekdb_rust_tls_drops());
    for (unsigned i = 0; i < 2; ++i) {
      assert(sums[i] == expected_sum((uint64_t(1) << 48) + i));
      expected += sums[i];
    }
    const uint64_t rust_expected = expected_sum(uint64_t(1) << 40) + expected_sum((uint64_t(1) << 40) + 1);
    assert(seekdb_rust_spawn_workers() == rust_expected);
    printf("round %u: TLS drops after Rust workers = %u\n", round, seekdb_rust_tls_drops());
    expected += rust_expected;
    assert(seekdb_rust_atomic_sum() == expected);
    assert(seekdb_rust_locked_sum() == expected);
    assert(seekdb_rust_tls_drops() == (round + 1) * 4);
  }
  puts("PASS: C++/Rust threads, 64-bit ABI, TLS teardown, allocation, mutex and condition variable");
}
