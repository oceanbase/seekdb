/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#include "gtest/gtest.h"
#include "lib/alloc/alloc_func.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/page_arena.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/basic/ob_chunk_row_store.h"
#include "sql/engine/basic/ob_ra_datum_store.h"
#include "sql/engine/basic/ob_workarea_memory_limit.h"
#include "sql/engine/ob_sql_memory_manager.h"

#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{

MemoryQuota *test_workarea_quota = nullptr;

MemoryQuota *resolve_test_workarea_quota(const int64_t ctx_id)
{
  return ObCtxIds::WORK_AREA == ctx_id ? test_workarea_quota : nullptr;
}

class TestWorkareaQuotaResolverGuard final
{
public:
  explicit TestWorkareaQuotaResolverGuard(MemoryQuota &quota)
  {
    test_workarea_quota = &quota;
    set_memory_quota_resolver(ObCtxIds::WORK_AREA,
                              resolve_test_workarea_quota);
  }
  ~TestWorkareaQuotaResolverGuard()
  {
    set_memory_quota_resolver(ObCtxIds::WORK_AREA, nullptr);
    test_workarea_quota = nullptr;
  }
};

class TestStoredRow final
{
public:
  explicit TestStoredRow(const int64_t row_size)
      : bytes_(row_size, 0),
        row_(new (bytes_.data()) ObChunkDatumStore::StoredRow())
  {
    row_->cnt_ = 0;
    row_->row_size_ = static_cast<uint32_t>(row_size);
  }

  const ObChunkDatumStore::StoredRow &get() const { return *row_; }

private:
  std::vector<char> bytes_;
  ObChunkDatumStore::StoredRow *row_;
};

// Keep the quota/spill policy test inside the SQL module.  The temporary-file
// service has its own module tests; here a successful write is enough to drive
// the real ObChunkDatumStore dump, block reuse and reclaim accounting paths.
class TestChunkDatumStore final : public ObChunkDatumStore
{
public:
  TestChunkDatumStore(const oceanbase::lib::ObLabel &label,
                      ObIAllocator &allocator,
                      ObSqlMemoryCallback &callback)
      : ObChunkDatumStore(label, &allocator),
        callback_(callback),
        spilled_bytes_(0)
  {}

  int64_t spilled_bytes() const { return spilled_bytes_; }

private:
  int write_file(void *buf, const int64_t size) override
  {
    int ret = OB_SUCCESS;
    if (size < 0 || (size > 0 && nullptr == buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (size > 0) {
      spilled_bytes_ += size;
      callback_.dumped(size);
    }
    return ret;
  }

  ObSqlMemoryCallback &callback_;
  int64_t spilled_bytes_;
};

// Changes the shared limit immediately before one backing allocation.  This
// deterministically models a concurrent store or dynamic limit shrink after a
// store's spill sample but before hard-quota admission.
class ShrinkQuotaOnNextAllocation final : public ObIAllocator
{
public:
  ShrinkQuotaOnNextAllocation(ObIAllocator &allocator, MemoryQuota &quota)
      : allocator_(allocator), quota_(quota), reclaim_margin_(0), armed_(false),
        triggered_(false)
  {}

  void arm(const int64_t reclaim_margin)
  {
    reclaim_margin_ = reclaim_margin;
    armed_ = true;
    triggered_ = false;
  }

  bool triggered() const { return triggered_; }

  void *alloc(const int64_t size) override
  {
    shrink_if_armed(size);
    return allocator_.alloc(size);
  }

  void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    shrink_if_armed(size);
    return allocator_.alloc(size, attr);
  }

  void free(void *ptr) override { allocator_.free(ptr); }
  int64_t total() const override { return allocator_.total(); }
  int64_t used() const override { return allocator_.used(); }

private:
  void shrink_if_armed(const int64_t size)
  {
    if (armed_) {
      armed_ = false;
      triggered_ = true;
      const int64_t requested_charge =
          size + TrackedAllocator::header_size();
      quota_.set_limit(quota_.committed() + requested_charge
                       - reclaim_margin_);
    }
  }

  ObIAllocator &allocator_;
  MemoryQuota &quota_;
  int64_t reclaim_margin_;
  bool armed_;
  bool triggered_;
};

class TestRADatumStore final : public ObRADatumStore
{
public:
  TestRADatumStore(ObIAllocator &allocator, ObSqlMemoryCallback &callback)
      : ObRADatumStore(&allocator), callback_(callback), spilled_bytes_(0)
  {}

  int64_t spilled_bytes() const { return spilled_bytes_; }

private:
  int write_file(BlockIndex &bi, void *buf, int64_t size) override
  {
    int ret = OB_SUCCESS;
    if (size < 0 || (size > 0 && nullptr == buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      bi.offset_ = spilled_bytes_;
      bi.on_disk_ = true;
      spilled_bytes_ += size;
      callback_.dumped(size);
    }
    return ret;
  }

  ObSqlMemoryCallback &callback_;
  int64_t spilled_bytes_;
};

class TestChunkRowStore final : public ObChunkRowStore
{
public:
  TestChunkRowStore(ObIAllocator &allocator, ObSqlMemoryCallback &callback)
      : ObChunkRowStore(&allocator), callback_(callback), spilled_bytes_(0)
  {}

  int64_t spilled_bytes() const { return spilled_bytes_; }

private:
  int write_file(void *buf, int64_t size) override
  {
    int ret = OB_SUCCESS;
    if (size < 0 || (size > 0 && nullptr == buf)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      spilled_bytes_ += size;
      callback_.dumped(size);
    }
    return ret;
  }

  ObSqlMemoryCallback &callback_;
  int64_t spilled_bytes_;
};

} // namespace

TEST(TestWorkareaMemoryQuota, zero_inherits_eighty_percent)
{
  EXPECT_EQ(80, calculate_workarea_memory_limit(0, 100));
  EXPECT_EQ(81, calculate_workarea_memory_limit(0, 102));
  EXPECT_EQ(0, calculate_workarea_memory_limit(0, 0));
}

TEST(TestWorkareaMemoryQuota, explicit_limit_is_preserved)
{
  EXPECT_EQ(123, calculate_workarea_memory_limit(123, 1000));
  EXPECT_EQ(UNLIMITED_WORKAREA_MEMORY,
            calculate_workarea_memory_limit(UNLIMITED_WORKAREA_MEMORY, 1000));
}

TEST(TestWorkareaMemoryQuota, successful_spill_write_records_one_reclaim)
{
  oceanbase::common::MemoryQuota quota(1024);
  ObSqlMemoryTracker tracker;
  tracker.set_memory_quota(&quota);

  tracker.dumped(256);
  EXPECT_EQ(1, quota.reclaim_count());
  EXPECT_EQ(256, tracker.get_total_dump_size());

  tracker.dumped(-256);
  EXPECT_EQ(1, quota.reclaim_count());
  EXPECT_EQ(0, tracker.get_total_dump_size());
}

TEST(TestWorkareaMemoryQuota,
     two_zero_limit_stores_spill_before_shared_quota_rejection)
{
  MemoryQuota quota;
  TestWorkareaQuotaResolverGuard quota_resolver_guard(quota);
  ObSqlMemoryTracker tracker;
  tracker.set_memory_quota(&quota);
  DefaultPageAllocator backing_allocator(
      ObMemAttr("WAQuotaAlloc", ObCtxIds::WORK_AREA));
  TrackedAllocator quota_allocator(
      backing_allocator, nullptr, &quota,
      ObMemAttr("WAQuotaAlloc", ObCtxIds::WORK_AREA));
  const TestStoredRow row(3L << 10);

  {
    TestChunkDatumStore first_store(
        "WAQuotaFirst", quota_allocator, tracker);
    TestChunkDatumStore second_store(
        "WAQuotaSecond", quota_allocator, tracker);
    ASSERT_EQ(OB_SUCCESS,
              first_store.init(0, ObCtxIds::WORK_AREA,
                               "WAQuotaFirst", true, 0,
                               ObChunkDatumStore::MIN_BLOCK_SIZE));
    ASSERT_EQ(OB_SUCCESS,
              second_store.init(0, ObCtxIds::WORK_AREA,
                                "WAQuotaSecond", true, 0,
                                ObChunkDatumStore::MIN_BLOCK_SIZE));
    first_store.set_callback(&tracker);
    second_store.set_callback(&tracker);

    ASSERT_EQ(OB_SUCCESS, first_store.add_row(row.get()));
    ASSERT_EQ(OB_SUCCESS, second_store.add_row(row.get()));
    const int64_t aggregate_bytes = quota.committed();
    ASSERT_GT(aggregate_bytes, 0);

    // Both stores remain well below their former per-store threshold, while
    // their combined charge reaches the shared 80% spill watermark.  The
    // remaining hard-quota headroom is deliberately smaller than a new block.
    const int64_t shared_limit = aggregate_bytes + aggregate_bytes / 4;
    quota.set_limit(shared_limit);
    const int64_t spill_watermark =
        calculate_workarea_memory_limit(0, shared_limit);
    ASSERT_LT(first_store.get_mem_used(), spill_watermark);
    ASSERT_LT(second_store.get_mem_used(), spill_watermark);
    ASSERT_TRUE(should_spill_workarea(0, first_store.get_mem_used(),
                                      row.get().row_size_));
    ASSERT_LT(shared_limit - quota.committed(), aggregate_bytes / 2);

    const int64_t reject_count = quota.reject_count();
    const int64_t reclaim_count = quota.reclaim_count();
    ASSERT_EQ(OB_SUCCESS, first_store.add_row(row.get()));
    EXPECT_EQ(2, first_store.get_row_cnt());
    EXPECT_EQ(1, second_store.get_row_cnt());
    EXPECT_EQ(reject_count, quota.reject_count());
    EXPECT_GT(quota.reclaim_count(), reclaim_count);
    EXPECT_GT(first_store.spilled_bytes(), 0);
    EXPECT_LE(quota.committed(), shared_limit);
    ASSERT_EQ(OB_SUCCESS, first_store.finish_add_row(false));
  }

  EXPECT_EQ(0, quota.committed());
  EXPECT_EQ(0, quota.reserved());
}

TEST(TestWorkareaMemoryQuota,
     ra_datum_store_reclaims_after_shared_quota_admission_race)
{
  MemoryQuota quota;
  TestWorkareaQuotaResolverGuard quota_resolver_guard(quota);
  ObSqlMemoryTracker tracker;
  tracker.set_memory_quota(&quota);
  DefaultPageAllocator backing_allocator(
      ObMemAttr("WARADatumAlloc", ObCtxIds::WORK_AREA));
  TrackedAllocator quota_allocator(
      backing_allocator, nullptr, &quota,
      ObMemAttr("WARADatumAlloc", ObCtxIds::WORK_AREA));
  ShrinkQuotaOnNextAllocation race_allocator(quota_allocator, quota);
  std::vector<char> payload(40L << 10, 'x');
  ObDatum datum;
  datum.set_string(payload.data(), payload.size());
  ObSEArray<ObDatum, 1> row;
  ASSERT_EQ(OB_SUCCESS, row.push_back(datum));

  {
    TestRADatumStore first_store(race_allocator, tracker);
    TestRADatumStore second_store(race_allocator, tracker);
    ASSERT_EQ(OB_SUCCESS,
              first_store.init(0, ObCtxIds::WORK_AREA, "WARADatumFirst"));
    ASSERT_EQ(OB_SUCCESS,
              second_store.init(0, ObCtxIds::WORK_AREA, "WARADatumSecond"));
    first_store.set_mem_stat(&tracker);
    second_store.set_mem_stat(&tracker);

    ASSERT_EQ(OB_SUCCESS, first_store.add_row(row));
    ASSERT_EQ(OB_SUCCESS, second_store.add_row(row));
    const int64_t aggregate_bytes = quota.committed();
    ASSERT_GT(aggregate_bytes, 0);

    // The high limit keeps the pre-allocation sample below its spill
    // watermark.  The allocator then shrinks the limit exactly at admission:
    // the first attempt is rejected, while spilling this store's current
    // block creates enough headroom for the single retry.
    quota.set_limit(aggregate_bytes + 4 * ObRADatumStore::BIG_BLOCK_SIZE);
    ASSERT_FALSE(should_spill_workarea(
        0, first_store.get_mem_hold(), ObRADatumStore::BIG_BLOCK_SIZE));
    race_allocator.arm(first_store.get_mem_hold() / 2);
    const int64_t reject_count = quota.reject_count();
    const int64_t reclaim_count = quota.reclaim_count();

    ASSERT_EQ(OB_SUCCESS, first_store.add_row(row));
    EXPECT_TRUE(race_allocator.triggered());
    EXPECT_EQ(2, first_store.get_row_cnt());
    EXPECT_EQ(1, second_store.get_row_cnt());
    EXPECT_GT(quota.reject_count(), reject_count);
    EXPECT_GT(quota.reclaim_count(), reclaim_count);
    EXPECT_GT(first_store.spilled_bytes(), 0);
    EXPECT_LE(quota.committed(), quota.limit());
    ASSERT_EQ(OB_SUCCESS, first_store.finish_add_row());
  }

  EXPECT_EQ(0, quota.committed());
  EXPECT_EQ(0, quota.reserved());
}

TEST(TestWorkareaMemoryQuota,
     chunk_row_store_reuses_spilled_block_after_quota_admission_race)
{
  MemoryQuota quota;
  TestWorkareaQuotaResolverGuard quota_resolver_guard(quota);
  ObSqlMemoryTracker tracker;
  tracker.set_memory_quota(&quota);
  DefaultPageAllocator backing_allocator(
      ObMemAttr("WAChunkRowAlloc", ObCtxIds::WORK_AREA));
  TrackedAllocator quota_allocator(
      backing_allocator, nullptr, &quota,
      ObMemAttr("WAChunkRowAlloc", ObCtxIds::WORK_AREA));
  ShrinkQuotaOnNextAllocation race_allocator(quota_allocator, quota);
  std::vector<char> payload(40L << 10, 'x');
  ObObj cell;
  cell.set_varchar(ObString(payload.size(), payload.data()));
  ObNewRow row;
  row.cells_ = &cell;
  row.count_ = 1;

  {
    TestChunkRowStore first_store(race_allocator, tracker);
    TestChunkRowStore second_store(race_allocator, tracker);
    ASSERT_EQ(OB_SUCCESS,
              first_store.init(0, ObCtxIds::WORK_AREA,
                               "WAChunkRowFirst", true));
    ASSERT_EQ(OB_SUCCESS,
              second_store.init(0, ObCtxIds::WORK_AREA,
                                "WAChunkRowSecond", true));
    first_store.set_callback(&tracker);
    second_store.set_callback(&tracker);

    ASSERT_EQ(OB_SUCCESS, first_store.add_row(row));
    ASSERT_EQ(OB_SUCCESS, second_store.add_row(row));
    const int64_t aggregate_bytes = quota.committed();
    ASSERT_GT(aggregate_bytes, 0);
    quota.set_limit(aggregate_bytes + 4 * ObChunkRowStore::BLOCK_SIZE);
    ASSERT_FALSE(should_spill_workarea(
        0, first_store.get_mem_hold(), ObChunkRowStore::BLOCK_SIZE));
    race_allocator.arm(first_store.get_mem_hold() / 2);
    const int64_t reject_count = quota.reject_count();
    const int64_t reclaim_count = quota.reclaim_count();

    ASSERT_EQ(OB_SUCCESS, first_store.add_row(row));
    EXPECT_TRUE(race_allocator.triggered());
    EXPECT_EQ(2, first_store.get_row_cnt());
    EXPECT_EQ(1, second_store.get_row_cnt());
    EXPECT_GT(quota.reject_count(), reject_count);
    EXPECT_GT(quota.reclaim_count(), reclaim_count);
    EXPECT_GT(first_store.spilled_bytes(), 0);
    EXPECT_LE(quota.committed(), quota.limit());
    ASSERT_EQ(OB_SUCCESS, first_store.finish_add_row(false));
  }

  EXPECT_EQ(0, quota.committed());
  EXPECT_EQ(0, quota.reserved());
}
