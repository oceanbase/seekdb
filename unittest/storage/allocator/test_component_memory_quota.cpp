/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#include "gtest/gtest.h"
#include "lib/alloc/alloc_func.h"
#include "lib/allocator/ob_fifo_allocator.h"
#include "lib/rc/context.h"
#include "storage/allocator/ob_vector_allocator.h"
#include "storage/meta_mem/ob_storage_meta_mem_mgr.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::storage;

namespace
{

MemoryQuota *test_vector_quota = nullptr;

MemoryQuota *resolve_test_vector_quota(const int64_t ctx_id)
{
  return ObCtxIds::VECTOR_CTX_ID == ctx_id ? test_vector_quota : nullptr;
}

class TestVectorQuotaResolverGuard final
{
public:
  explicit TestVectorQuotaResolverGuard(MemoryQuota &quota)
  {
    test_vector_quota = &quota;
    set_memory_quota_resolver(ObCtxIds::VECTOR_CTX_ID,
                              resolve_test_vector_quota);
  }
  ~TestVectorQuotaResolverGuard()
  {
    set_memory_quota_resolver(ObCtxIds::VECTOR_CTX_ID, nullptr);
    test_vector_quota = nullptr;
  }
};

struct TestMetaPoolObject
{
  void reset() {}
};

} // namespace

TEST(TestVectorMemoryQuota, actual_bytes_and_dynamic_limit)
{
  ObVectorAllocator allocator;
  MemoryQuota &quota = allocator.memory_quota();
  quota.reset(128);
  ASSERT_TRUE(quota.reserve(64));
  ASSERT_TRUE(quota.reconcile(64, 80));
  EXPECT_EQ(80, quota.committed());
  quota.set_limit(40);
  EXPECT_FALSE(quota.reserve(1));
  quota.release(80);
  EXPECT_EQ(0, quota.committed());
}

TEST(TestVectorMemoryQuota, memory_context_and_bitmap_share_one_admission_word)
{
  ObVectorAllocator allocator;
  MemoryQuota &quota = allocator.memory_quota();
  quota.reset(128);

  MemoryReservation context_allocation(&quota, 80);
  ASSERT_TRUE(context_allocation.valid());
  ASSERT_TRUE(context_allocation.reconcile(80));

  MemoryReservation bitmap_allocation(&quota, 49);
  EXPECT_FALSE(bitmap_allocation.valid());
  EXPECT_EQ(80, quota.committed());
  EXPECT_EQ(0, quota.reserved());

  quota.release(80);
  EXPECT_EQ(0, quota.committed());
}

TEST(TestVectorMemoryQuota, fifo_metadata_pages_use_vector_context_quota)
{
  MemoryQuota quota(4L << 20);
  TestVectorQuotaResolverGuard resolver_guard(quota);
  {
    ObFIFOAllocator allocator;
    ASSERT_EQ(OB_SUCCESS, allocator.init(
        nullptr, 4096, ObMemAttr("VectorMetadata", ObCtxIds::VECTOR_CTX_ID)));
    void *metadata = allocator.alloc(128);
    ASSERT_NE(nullptr, metadata);
    EXPECT_GT(quota.committed(), 0);
    allocator.free(metadata);
    allocator.reset();
    EXPECT_EQ(0, quota.committed());
    EXPECT_EQ(0, quota.reserved());
  }
}

TEST(TestMetaMemoryQuota, preserves_double_percentage_rule)
{
  EXPECT_EQ(400, ObStorageMetaMemMgr::calculate_memory_quota_limit(1000, 20));
  EXPECT_EQ(0, ObStorageMetaMemMgr::calculate_memory_quota_limit(1000, 0));
  EXPECT_EQ(0, ObStorageMetaMemMgr::calculate_memory_quota_limit(0, 20));
}

TEST(TestMetaMemoryQuota, production_order_charges_inner_and_overflow_allocations)
{
  // Production constructs all pools before installing the META_OBJECT
  // resolver.  Explicit ownership must therefore work with no resolver.
  set_memory_quota_resolver(ObCtxIds::META_OBJ_CTX_ID, nullptr);
  MemoryQuota quota(16L << 20);
  {
    ObStorageMetaObjPool<TestMetaPoolObject> pool(
        1, "MetaQuotaTest", ObCtxIds::META_OBJ_CTX_ID, quota);
    const int64_t construction_charge = quota.committed();
    EXPECT_GT(construction_charge, 0);

    TestMetaPoolObject *inner_object = nullptr;
    ASSERT_EQ(OB_SUCCESS, pool.acquire(inner_object));
    ASSERT_NE(nullptr, inner_object);
    const int64_t inner_charge = quota.committed();
    EXPECT_GT(inner_charge, construction_charge);

    TestMetaPoolObject *overflow_object = nullptr;
    ASSERT_EQ(OB_SUCCESS, pool.acquire(overflow_object));
    ASSERT_NE(nullptr, overflow_object);
    const int64_t overflow_charge = quota.committed();
    EXPECT_GT(overflow_charge, inner_charge);

    quota.set_limit(overflow_charge);
    TestMetaPoolObject *rejected_object = nullptr;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, pool.acquire(rejected_object));
    EXPECT_EQ(nullptr, rejected_object);
    EXPECT_GT(quota.reject_count(), 0);

    pool.release(overflow_object);
    pool.release(inner_object);

    pool.destroy();
    EXPECT_EQ(0, quota.committed());
    EXPECT_EQ(0, quota.reserved());
  }
}
