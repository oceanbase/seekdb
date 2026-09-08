// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
#include "lib/oblog/ob_ringbuf_log_writer.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

int main()
{
  using namespace oceanbase::common;
  constexpr int producers = 4;
  constexpr int iterations = 50000;
  alignas(8) char storage[4096];
  ObRingBuf ring;
  assert(ring.init(storage, sizeof(storage)) == 0);
  std::atomic<int> finished{0};
  std::vector<std::thread> threads;
  for (int producer = 0; producer < producers; ++producer) threads.emplace_back([&, producer] {
    for (int i = 0; i < iterations; ++i) {
      const int words = 4 + (i % 29);
      int64_t position;
      while ((position = ring.alloc(8 + words * 8)) < 0) std::this_thread::yield();
      auto *data = reinterpret_cast<uint64_t *>(ring.data_of(position));
      data[0] = producer;
      data[1] = i;
      for (int j = 2; j < words; ++j) {
        if (j == 3 && i % 7 == 0) std::this_thread::yield();
        data[j] = UINT64_C(0xfedcba9800000000) | (uint64_t(producer) << 24) | (i ^ j);
      }
      if (i % 13 == 0) ring.rollback(position);
      else ring.commit(position);
    }
    finished.fetch_add(1);
  });
  int seen[producers] = {};
  int64_t position = 0;
  while (finished.load() != producers || position != ring.get_push()) {
    if (position == ring.get_push()) { std::this_thread::yield(); continue; }
    const uint64_t raw = ATOMIC_LOAD_ACQ(reinterpret_cast<uint64_t *>(ring.entry_at(position % sizeof(storage))));
    RingBufEntry header;
    std::memcpy(&header, &raw, sizeof(header));
    if (header.busy_) { std::this_thread::yield(); continue; }
    assert(header.total_len_ >= 8 && header.total_len_ <= sizeof(storage) && header.total_len_ % 8 == 0);
    if (header.type_ == RingBufEntry::TYPE_COMMIT) {
      const auto *data = reinterpret_cast<const uint64_t *>(ring.data_of(position));
      const int producer = data[0];
      const int sequence = data[1];
      assert(producer >= 0 && producer < producers && sequence >= 0 && sequence < iterations);
      assert(sequence % 13 != 0);
      assert(header.total_len_ == 8 + (4 + sequence % 29) * 8);
      for (unsigned j = 2; j < (header.total_len_ - 8) / 8; ++j) {
        assert(data[j] == (UINT64_C(0xfedcba9800000000) | (uint64_t(producer) << 24) | (sequence ^ j)));
      }
      ++seen[producer];
    } else assert(header.type_ == RingBufEntry::TYPE_ROLLBACK);
    position += header.total_len_;
    ring.advance_pop(position);
  }
  for (auto &thread : threads) thread.join();
  for (const int count : seen) assert(count == iterations - (iterations + 12) / 13);
  ring.destroy();
  std::puts("log ring: concurrent publication, rollback and wraparound passed");
}
