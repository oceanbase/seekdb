// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
// Linked only into the BLAS test. Fail one selected allocation after all worker
// threads have joined, so failures belong to the real LAPACKE workspace path.
#include <atomic>
#include <cstddef>
static std::atomic<int> remaining{-1};
extern "C" void * __real_malloc(size_t);
extern "C" void fail_blas_allocation_after(int count) { remaining.store(count); }
extern "C" void *__wrap_malloc(size_t size)
{
  int count = remaining.load();
  if (count >= 0 && remaining.fetch_sub(1) == 0) { return nullptr; }
  return __real_malloc(size);
}
