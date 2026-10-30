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

#include <gtest/gtest.h>
#include "storage/retrieval/ob_block_stat_iter.h"
#include "storage/memtable/ob_memtable.h"
#include "storage/memtable/ob_memtable_key.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace memtable;

class EmptyRangeIterator : public ObStoreRowIterator
{
public:
  int get_next_row(const ObDatumRow *&row) override
  {
    row = nullptr;
    return OB_ITER_END;
  }
};

class RecordingMemtable : public ObMemtable
{
public:
  explicit RecordingMemtable(ObIAllocator &allocator) : allocator_(allocator), scans_(0) {}
  bool is_memtable() const override { return true; }
  int scan(const ObTableIterParam &param, ObTableAccessContext &context,
      const ObDatumRange &range, ObStoreRowIterator *&iter) override
  {
    UNUSED(context);
    ++scans_;
    range_ = range;
    ObMemtableKey start;
    ObMemtableKey end;
    int ret = start.encode(*param.get_out_col_descs(), &range.start_key_.get_store_rowkey());
    if (OB_SUCC(ret)) {
      ret = end.encode(*param.get_out_col_descs(), &range.end_key_.get_store_rowkey());
    }
    if (OB_SUCC(ret)) {
      iter = OB_NEWx(EmptyRangeIterator, &allocator_);
      if (nullptr == iter) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    return ret;
  }
  ObIAllocator &allocator_;
  ObDatumRange range_;
  int64_t scans_;
};

class BlockStatRangeTest : public ::testing::Test
{
public:
  BlockStatRangeTest() : memtable_(allocator_) {}
  void SetUp() override
  {
    for (int64_t i = 0; i < 4; ++i) {
      ObColDesc desc;
      desc.col_id_ = OB_APP_MIN_COLUMN_ID + i;
      desc.col_type_.set_type(0 == i ? ObVarcharType : 1 == i ? ObIntType : ObUInt64Type);
      desc.col_type_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      ASSERT_EQ(OB_SUCCESS, columns_.push_back(desc));
    }
    ASSERT_EQ(OB_SUCCESS, read_info_.init(allocator_, 4, 2, columns_, nullptr));
    ASSERT_EQ(OB_SUCCESS, rowkey_read_info_.init(allocator_, 4, 2, columns_));
    iterator_.iter_allocator_ = &allocator_;
    iterator_.main_table_param_.iter_param_.read_info_ = &read_info_;
    iterator_.main_table_param_.iter_param_.rowkey_read_info_ = &rowkey_read_info_;
    ASSERT_EQ(nullptr, iterator_.rowkey_read_info_);
    ASSERT_EQ(OB_SUCCESS, iterator_.scan_tables_.push_back(&memtable_));
    range().table_id_ = 1001;
  }
  void TearDown() override { iterator_.reset(); }
  void make_key(ObDatumRowkey &key, const int64_t count, const int64_t document = 10)
  {
    ObObj *objects = static_cast<ObObj *>(allocator_.alloc(sizeof(ObObj) * 4));
    ASSERT_NE(nullptr, objects);
    for (int i = 0; i < 4; ++i) { new (&objects[i]) ObObj(); }
    char *token = static_cast<char *>(allocator_.alloc(5));
    ASSERT_NE(nullptr, token);
    MEMCPY(token, "token", 5);
    objects[0].set_varchar(ObString(5, token));
    objects[0].set_collation_type(CS_TYPE_UTF8MB4_BIN);
    objects[1].set_int(document);
    objects[2].set_int(-100);
    objects[3].set_int(-1);
    ASSERT_EQ(OB_SUCCESS, key.from_rowkey(ObRowkey(objects, count), allocator_));
  }
  ObDatumRange &range() { return iterator_.curr_scan_range_; }
  int construct() { return iterator_.construct_iters(); }
  void reset_iters() { iterator_.reset_iters(); }
  int shrink(const ObDatumRowkey &key)
  {
    iterator_.rowkey_read_info_ = &rowkey_read_info_;
    return iterator_.shrink_scan_range(key);
  }
  void expect_logical_range(const int64_t start_count, const int64_t end_count)
  {
    EXPECT_EQ(start_count, memtable_.range_.start_key_.get_datum_cnt());
    EXPECT_EQ(end_count, memtable_.range_.end_key_.get_datum_cnt());
    EXPECT_EQ(start_count, memtable_.range_.start_key_.get_store_rowkey().get_obj_cnt());
    EXPECT_EQ(end_count, memtable_.range_.end_key_.get_store_rowkey().get_obj_cnt());
    EXPECT_EQ(range().table_id_, memtable_.range_.table_id_);
    EXPECT_EQ(range().border_flag_.get_data(), memtable_.range_.border_flag_.get_data());
  }
  ObArenaAllocator allocator_;
  ObSEArray<ObColDesc, 4> columns_;
  ObTableReadInfo read_info_;
  ObRowkeyReadInfo rowkey_read_info_;
  RecordingMemtable memtable_;
  ObBlockStatIterator iterator_;
};

TEST_F(BlockStatRangeTest, physical_keys_encode_at_initial_construction)
{
  make_key(range().start_key_, 4);
  make_key(range().end_key_, 4, 20);
  ObMemtableKey raw;
  ASSERT_EQ(OB_ERR_UNEXPECTED, raw.encode(columns_, &range().start_key_.get_store_rowkey()));
  ASSERT_EQ(OB_ERR_UNEXPECTED, raw.encode(columns_, &range().end_key_.get_store_rowkey()));
  ASSERT_EQ(OB_SUCCESS, construct());
  expect_logical_range(2, 2);
  EXPECT_EQ(4, range().start_key_.get_store_rowkey().get_obj_cnt());
  EXPECT_EQ(4, range().end_key_.get_store_rowkey().get_obj_cnt());
  EXPECT_EQ(-100, range().start_key_.datums_[2].get_int());
}

TEST_F(BlockStatRangeTest, reconstruct_before_first_block)
{
  make_key(range().start_key_, 4);
  make_key(range().end_key_, 4, 20);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, construct());
    expect_logical_range(2, 2);
    reset_iters();
  }
  EXPECT_EQ(3, memtable_.scans_);
}

TEST_F(BlockStatRangeTest, scanner_range_owns_both_endpoint_strings)
{
  make_key(range().start_key_, 4);
  make_key(range().end_key_, 4, 20);
  ASSERT_EQ(OB_SUCCESS, construct());
  const_cast<char *>(range().start_key_.datums_[0].get_string().ptr())[0] = 'x';
  const_cast<char *>(range().end_key_.datums_[0].get_string().ptr())[0] = 'y';
  EXPECT_EQ(ObString::make_string("token"), memtable_.range_.start_key_.datums_[0].get_string());
  EXPECT_EQ(ObString::make_string("token"), memtable_.range_.end_key_.datums_[0].get_string());
  EXPECT_EQ(ObString::make_string("token"),
      memtable_.range_.start_key_.get_store_rowkey().get_obj_ptr()[0].get_varchar());
  EXPECT_EQ(ObString::make_string("token"),
      memtable_.range_.end_key_.get_store_rowkey().get_obj_ptr()[0].get_varchar());
}

TEST_F(BlockStatRangeTest, physical_progress_survives_reconstruction)
{
  make_key(range().start_key_, 2);
  make_key(range().end_key_, 4, 30);
  for (int64_t document = 11; document <= 12; ++document) {
    ObDatumRowkey progress;
    make_key(progress, 4, document);
    ASSERT_EQ(OB_SUCCESS, shrink(progress));
    ASSERT_EQ(4, range().start_key_.get_datum_cnt());
    progress.datums_[1].set_int(999);
    EXPECT_EQ(document, range().start_key_.datums_[1].get_int());
    EXPECT_EQ(-100, range().start_key_.datums_[2].get_int());
    EXPECT_EQ(-1, range().start_key_.datums_[3].get_int());
    ASSERT_EQ(OB_SUCCESS, construct());
    expect_logical_range(2, 2);
    EXPECT_EQ(document, memtable_.range_.start_key_.get_store_rowkey().get_obj_ptr()[1].get_int());
    reset_iters();
  }
}

TEST_F(BlockStatRangeTest, logical_and_prefix_keys_keep_flags)
{
  for (int count = 1; count <= 2; ++count) {
    make_key(range().start_key_, count);
    make_key(range().end_key_, count, 20);
    for (int flags = 0; flags < 4; ++flags) {
      range().border_flag_.set_data(flags);
      ASSERT_EQ(OB_SUCCESS, construct());
      expect_logical_range(count, count);
      reset_iters();
    }
  }
}

TEST_F(BlockStatRangeTest, canonical_physical_ranges_keep_flags)
{
  ObDatumRange logical;
  make_key(logical.start_key_, 2);
  make_key(logical.end_key_, 2, 20);
  for (int flags = 0; flags < 4; ++flags) {
    logical.border_flag_.set_data(flags);
    ASSERT_EQ(OB_SUCCESS, logical.to_multi_version_range(allocator_, range()));
    ASSERT_GT(range().start_key_.get_datum_cnt(), 2);
    ASSERT_GT(range().end_key_.get_datum_cnt(), 2);
    ASSERT_EQ(OB_SUCCESS, construct());
    expect_logical_range(2, 2);
    reset_iters();
  }
}

TEST_F(BlockStatRangeTest, whole_range_sentinels_survive_construction)
{
  range().set_whole_range();
  ASSERT_EQ(OB_SUCCESS, construct());
  EXPECT_TRUE(memtable_.range_.start_key_.is_min_rowkey());
  EXPECT_TRUE(memtable_.range_.end_key_.is_max_rowkey());
  EXPECT_TRUE(memtable_.range_.start_key_.get_store_rowkey().is_min());
  EXPECT_TRUE(memtable_.range_.end_key_.get_store_rowkey().is_max());
}

} // namespace storage
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
