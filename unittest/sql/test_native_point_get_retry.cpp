/*
 * Copyright (c) 2025 OceanBase.
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

#include "sql/das/ob_das_scan_op.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/table/native_point_get_input.h"
#include "data_plane/access/ob_tablet_scan.h"
#include "data_plane/transaction/ob_tx_read_snapshot.h"
#include "lib/charset/ob_charset.h"
#include "share/rc/ob_server_runtime.h"
#include <gtest/gtest.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;

namespace
{
class EmptyTabletRows final : public ObNewRowIterator
{
public:
  int get_next_row(ObNewRow *&row) override { row = nullptr; return OB_ITER_END; }
  void reset() override {}
};

// Only the external tablet service is replaced. Task initialization, scan-tree
// creation, the scan iterator's release/reuse, and key restoration are real.
class RecordingTabletScan final : public ObITabletScan
{
public:
  int table_scan(ObVTableScanParam &base_param, ObNewRowIterator *&result) override
  {
    ObTableScanParam &param = static_cast<ObTableScanParam &>(base_param);
    ++scan_calls_;
    last_param_ = &param;
    last_keys_ = param.datum_lookup_keys_;
    last_range_count_ = param.key_ranges_.count();
    last_is_get_ = param.is_get_;
    if (!param.is_valid()) { return OB_INVALID_ARGUMENT; }
    // Storage may return a partially initialized iterator with a retryable
    // error. The real ObDASScanIter must return it before the task reopens.
    EXPECT_FALSE(active_);
    active_ = true;
    result = &rows_;
    const int ret = next_scan_error_;
    next_scan_error_ = OB_SUCCESS;
    return ret;
  }

  int reuse_scan_iter(const bool, ObNewRowIterator *iter) override
  {
    ++reuse_calls_;
    EXPECT_EQ(&rows_, iter);
    EXPECT_TRUE(active_);
    keys_during_reuse_ = last_param_->datum_lookup_keys_;
    return reuse_error_;
  }

  int revert_scan_iter(ObNewRowIterator *iter) override
  {
    ++revert_calls_;
    EXPECT_EQ(&rows_, iter);
    EXPECT_TRUE(active_);
    keys_during_revert_ = last_param_->datum_lookup_keys_;
    active_ = false;
    return OB_SUCCESS;
  }

  EmptyTabletRows rows_;
  ObTableScanParam *last_param_ = nullptr;
  const ObIArray<DatumLookupKey> *last_keys_ = nullptr;
  const ObIArray<DatumLookupKey> *keys_during_revert_ = nullptr;
  const ObIArray<DatumLookupKey> *keys_during_reuse_ = nullptr;
  int64_t last_range_count_ = -1;
  bool last_is_get_ = false;
  bool active_ = false;
  int scan_calls_ = 0;
  int revert_calls_ = 0;
  int reuse_calls_ = 0;
  int next_scan_error_ = OB_SUCCESS;
  int reuse_error_ = OB_SUCCESS;
};

class RetryScanOp final : public ObDASScanOp
{
public:
  explicit RetryScanOp(ObIAllocator &allocator) : ObDASScanOp(allocator) {}
  // The real DAS retry driver sets this flag before the next open. Expose only
  // that input state; open/release/reuse are not overridden.
  void mark_partition_retry() { in_part_retry_ = true; }
};

class NativePointGetRetry : public ::testing::Test
{
protected:
  NativePointGetRetry()
    : arena_(), exec_(arena_), eval_(exec_), ctdef_(arena_), rtdef_(),
      snapshot_(), owner_(), task_(arena_), tablet_(), previous_service_(nullptr),
      can_release_(false)
  {}

  void SetUp() override
  {
    previous_service_ = share::server_service<ObITabletScan>();
    share::bind_server_service<ObITabletScan>(&tablet_);
    ctdef_.ref_table_id_ = 1000001;
    ctdef_.schema_version_ = 1;
    ctdef_.is_get_ = true;
    rtdef_.eval_ctx_ = &eval_;
    rtdef_.stmt_allocator_.set_alloc(&arena_);
    rtdef_.scan_allocator_.set_alloc(&arena_);
    rtdef_.timeout_ts_ = INT64_MAX;
    ASSERT_EQ(OB_SUCCESS, rtdef_.init_pd_op(exec_, ctdef_));
    snapshot_.init_none_read();
    task_.set_scan_ctdef(&ctdef_);
    task_.set_scan_rtdef(&rtdef_);
    task_.set_snapshot(&snapshot_);
    task_.set_tablet_id(ObTabletID(1000001));
    // Also establishes a valid table_param_ for release on a test assertion
    // failure before the first open.
    ASSERT_EQ(OB_SUCCESS, task_.init_scan_param());
    can_release_ = true;
    const int64_t values[3] = {INT64_MIN, 23, INT64_MAX};
    ASSERT_EQ(OB_SUCCESS, owner_.init(values, int64_t(9) << 32));
    task_.set_native_point_get_keys(&owner_.lookup_keys());
  }

  void TearDown() override
  {
    // Iterators are arena-owned in production. Release them with their dynamic
    // type intact, then reclaim their storage with the fixture's arena.
    if (can_release_) { EXPECT_EQ(OB_SUCCESS, task_.release_op()); }
    EXPECT_FALSE(tablet_.active_);
    share::bind_server_service<ObITabletScan>(previous_service_);
  }

  void expect_native_request()
  {
    ASSERT_EQ(&owner_.lookup_keys(), tablet_.last_keys_);
    EXPECT_EQ(&owner_.lookup_keys(), task_.get_scan_param().datum_lookup_keys_);
    EXPECT_TRUE(tablet_.last_is_get_);
    EXPECT_EQ(0, tablet_.last_range_count_);
    ASSERT_EQ(1, tablet_.last_keys_->count());
    const DatumLookupKey &key = tablet_.last_keys_->at(0);
    ASSERT_EQ(3, key.count_);
    ASSERT_NE(nullptr, key.cached_rowkey_);
    for (int64_t i = 0; i < 3; ++i) {
      EXPECT_EQ(owner_.values()[i], key.datums_[i].get_int());
      // Fixed storage still borrows the execution owner's stable I64 bytes.
      EXPECT_EQ(reinterpret_cast<const char *>(&owner_.values()[i]), key.datums_[i].ptr_);
      // The generic cached key owns the same scalar bits directly inline.
      const DatumValue &cached = key.cached_rowkey_->get_cell(i);
      EXPECT_EQ(owner_.values()[i], cached.get_int());
      EXPECT_EQ(0, cached.datum_.length());
    }
    EXPECT_EQ(int64_t(9) << 32, key.group_id_);
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  ObDASScanCtDef ctdef_;
  ObDASScanRtDef rtdef_;
  transaction::ObTxReadSnapshot snapshot_;
  NativePointGetInput owner_;
  RetryScanOp task_;
  RecordingTabletScan tablet_;
  ObITabletScan *previous_service_;
  bool can_release_;
};

TEST_F(NativePointGetRetry, failed_open_release_reopen_restores_the_same_native_key_without_cherry_pick)
{
  tablet_.next_scan_error_ = OB_NOT_MASTER;
  ASSERT_EQ(OB_NOT_MASTER, task_.open_op());
  expect_native_request();
  ASSERT_EQ(1, tablet_.scan_calls_);
  ASSERT_EQ(OB_SUCCESS, task_.release_op());
  EXPECT_EQ(1, tablet_.revert_calls_);
  EXPECT_EQ(&owner_.lookup_keys(), tablet_.keys_during_revert_);
  ASSERT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);

  task_.mark_partition_retry();
  // No setter or TableScan cherry-pick runs between release and reopen.
  ASSERT_EQ(OB_SUCCESS, task_.open_op());
  expect_native_request();
  EXPECT_EQ(2, tablet_.scan_calls_);
  EXPECT_TRUE(tablet_.active_);
}

TEST_F(NativePointGetRetry, fallback_after_retry_release_cannot_republish_the_preceding_native_key)
{
  tablet_.next_scan_error_ = OB_NOT_MASTER;
  ASSERT_EQ(OB_NOT_MASTER, task_.open_op());
  ASSERT_EQ(OB_SUCCESS, task_.release_op());
  ASSERT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);

  task_.set_native_point_get_keys(nullptr);
  ctdef_.is_get_ = false;
  ObNewRange range;
  range.set_whole_range();
  ASSERT_EQ(OB_SUCCESS, task_.get_scan_param().key_ranges_.push_back(range));
  task_.mark_partition_retry();
  ASSERT_EQ(OB_SUCCESS, task_.open_op());
  EXPECT_EQ(nullptr, tablet_.last_keys_);
  EXPECT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);
  EXPECT_FALSE(tablet_.last_is_get_);
  EXPECT_EQ(1, tablet_.last_range_count_);
}

TEST_F(NativePointGetRetry, successful_reuse_clears_both_publication_and_the_saved_source)
{
  ASSERT_EQ(OB_SUCCESS, task_.open_op());
  expect_native_request();
  ASSERT_EQ(OB_SUCCESS, task_.reuse_iter());
  EXPECT_EQ(1, tablet_.reuse_calls_);
  EXPECT_EQ(&owner_.lookup_keys(), tablet_.keys_during_reuse_);
  ASSERT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);
  // A cleared scan view alone is insufficient: initialization would revive a
  // stale task source unless reuse also invalidated the saved generation.
  ASSERT_EQ(OB_SUCCESS, task_.init_scan_param());
  EXPECT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);
}

TEST_F(NativePointGetRetry, failed_reuse_keeps_the_source_until_storage_stops_borrowing)
{
  ASSERT_EQ(OB_SUCCESS, task_.open_op());
  tablet_.reuse_error_ = OB_NOT_MASTER;
  ASSERT_EQ(OB_NOT_MASTER, task_.reuse_iter());
  EXPECT_EQ(&owner_.lookup_keys(), tablet_.keys_during_reuse_);
  ASSERT_EQ(&owner_.lookup_keys(), task_.get_scan_param().datum_lookup_keys_);
  ASSERT_EQ(OB_SUCCESS, task_.init_scan_param());
  EXPECT_EQ(&owner_.lookup_keys(), task_.get_scan_param().datum_lookup_keys_);

  task_.set_errcode(OB_SUCCESS);
  tablet_.reuse_error_ = OB_SUCCESS;
  ASSERT_EQ(OB_SUCCESS, task_.reuse_iter());
  ASSERT_EQ(OB_SUCCESS, task_.init_scan_param());
  EXPECT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);
}

TEST_F(NativePointGetRetry, local_native_keys_remain_unserializable_after_retry_clears_the_scan_view)
{
  ASSERT_EQ(OB_SUCCESS, task_.open_op());
  ASSERT_EQ(OB_SUCCESS, task_.release_op());
  ASSERT_EQ(nullptr, task_.get_scan_param().datum_lookup_keys_);
  char buffer[1024] = {};
  int64_t position = 0;
  // The task still owns a local source despite the released scan's null view;
  // copying it as an ordinary empty-range GET would silently change meaning.
  EXPECT_EQ(OB_NOT_SUPPORTED, task_.serialize(buffer, sizeof(buffer), position));
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
