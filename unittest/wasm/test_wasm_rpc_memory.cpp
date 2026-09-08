// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "rpc/ob_sql_mem_pool.h"
#include "lib/resource/achunk_mgr.h"
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>

using oceanbase::obmysql::ObSqlMemPool;

static void exercise(ObSqlMemPool &pool)
{
  for (unsigned round = 0; round < 3; ++round) {
    assert(pool.alloc(-1) == nullptr);
    assert(pool.alloc(INT64_MAX) == nullptr);
    // Fit inside the retained first page after reuse. With an embedded pool,
    // resetting that page to zero would return the pool object itself.
    auto *small = static_cast<unsigned char *>(pool.alloc(16));
    assert(small != nullptr);
    assert(static_cast<void *>(small) != static_cast<void *>(&pool));
    memset(small, 0xa5, 16);
    std::array<unsigned char *, 32> blocks;
    for (size_t i = 0; i < blocks.size(); ++i) {
      blocks[i] = static_cast<unsigned char *>(pool.alloc(4096));
      assert(blocks[i] != nullptr);
      memset(blocks[i], int(i + 1), 4096);
    }
    for (size_t i = 0; i < blocks.size(); ++i) {
      for (size_t byte = 0; byte < 4096; ++byte) assert(blocks[i][byte] == i + 1);
    }
    for (size_t byte = 0; byte < 16; ++byte) assert(small[byte] == 0xa5);
    pool.reuse();
  }
}

int main()
{
  oceanbase::lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  oceanbase::lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  ObSqlMemPool stack_pool;
  exercise(stack_pool);
  assert(ObSqlMemPool::create("WasmRpc", -1) == nullptr);
  assert(ObSqlMemPool::create("WasmRpc", INT64_MAX) == nullptr);
  assert(ObSqlMemPool::create("WasmRpc", 128, -1) == nullptr);
  ObSqlMemPool *embedded_pool = ObSqlMemPool::create("WasmRpc", 128, 256);
  assert(embedded_pool != nullptr);
  exercise(*embedded_pool);
  embedded_pool->~ObSqlMemPool();
  puts("PASS: RPC request memory pages, overflow rejection and embedded-pool reuse");
}
