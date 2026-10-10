/**
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

// Focused experiment checks. Compile against the same release objects as seekdb.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "storage/tx/ob_tx_ctx.h"
#include "storage/tx/tx_ctx_cache.h"

using oceanbase::transaction::ObTxCtx;
using oceanbase::transaction::TxCtxCache;

static void check(bool condition, const char *message)
{
  if (!condition) {
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::abort();
  }
}

int main()
{
  std::printf("sizeof(ObTxCtx)=%zu alignof(ObTxCtx)=%zu sizeof(TxCtxCache)=%zu\n",
              sizeof(ObTxCtx), alignof(ObTxCtx), sizeof(TxCtxCache));
  check(nullptr == TxCtxCache::create(0), "zero-capacity cache must not allocate");
  ObTxCtx *direct = TxCtxCache::alloc(nullptr);
  check(nullptr != direct, "direct allocation");
  check(0 == reinterpret_cast<uintptr_t>(direct) % alignof(ObTxCtx), "64-byte alignment");
  TxCtxCache::free(direct);

  TxCtxCache *cache = TxCtxCache::create(1);
  check(nullptr != cache, "cache allocation");
  ObTxCtx *first = TxCtxCache::alloc(cache);
  ObTxCtx *overflow = TxCtxCache::alloc(cache);
  check(nullptr != first && nullptr != overflow && first != overflow, "two independent contexts");
  TxCtxCache::free(first);
  TxCtxCache::free(overflow);
  ObTxCtx *reused = TxCtxCache::alloc(cache);
  check(first == reused, "cache preserves its one available context");
  check(1 == cache->get_hit_count() && 2 == cache->get_miss_count(), "hit and miss accounting");
  // The connection disappears before the transaction's last reference does.
  TxCtxCache::close(cache);
  check(nullptr == cache, "close detaches the lifecycle owner");
  TxCtxCache::free(reused);

  cache = TxCtxCache::create(64);
  std::vector<std::thread> workers;
  for (int i = 0; i < 8; ++i) {
    workers.emplace_back([cache]() {
      for (int n = 0; n < 20000; ++n) {
        ObTxCtx *ctx = TxCtxCache::alloc(cache);
        check(nullptr != ctx, "parallel allocation");
        check(0 == reinterpret_cast<uintptr_t>(ctx) % alignof(ObTxCtx), "parallel alignment");
        TxCtxCache::free(ctx);
      }
    });
  }
  for (std::thread &worker : workers) { worker.join(); }
  check(160000 == cache->get_hit_count() + cache->get_miss_count(), "parallel accounting");
  std::printf("parallel replay-style reuse: hits=%ld misses=%ld\n",
              cache->get_hit_count(), cache->get_miss_count());
  TxCtxCache::close(cache);

  // Exercise simultaneous late returns after the owner's destruction.
  for (int n = 0; n < 200; ++n) {
    cache = TxCtxCache::create(1);
    workers.clear();
    std::atomic<int> ready{0};
    std::atomic<bool> closed{false};
    for (int i = 0; i < 8; ++i) {
      workers.emplace_back([cache, &ready, &closed]() {
        ObTxCtx *ctx = TxCtxCache::alloc(cache);
        check(nullptr != ctx, "late-return allocation");
        ready.fetch_add(1);
        while (!closed.load()) { std::this_thread::yield(); }
        TxCtxCache::free(ctx);
      });
    }
    while (ready.load() != 8) { std::this_thread::yield(); }
    TxCtxCache::close(cache);
    closed.store(true);
    for (std::thread &worker : workers) { worker.join(); }
  }
  std::puts("PASS: reuse, overflow, alignment, parallel allocation, owner-before-context destruction");
  return 0;
}
