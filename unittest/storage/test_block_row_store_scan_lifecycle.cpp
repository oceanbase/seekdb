/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#define USING_LOG_PREFIX STORAGE

#include <gtest/gtest.h>
#include <string>

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_bitmap.h"
#include "lib/container/ob_se_array.h"
#include "storage/access/ob_aggregated_store.h"
#include "storage/access/ob_block_row_store.h"
#include "storage/access/ob_pushdown_aggregate_input.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_table_read_info.h"
#include "storage/blocksstable/index_block/ob_agg_row_struct.h"
#include "storage/blocksstable/index_block/ob_index_block_aggregator.h"
#include "storage/blocksstable/ob_imicro_block_reader.h"
#include "storage/blocksstable/ob_micro_block_row_scanner.h"

namespace oceanbase
{
namespace storage
{
namespace
{

class ScanLifecycleProbeStore final : public ObBlockRowStore
{
public:
  explicit ScanLifecycleProbeStore(ObTableAccessContext &context)
    : ObBlockRowStore(context), scan_epoch_(0), accumulated_(0)
  {}

  void mark_initialized() { is_inited_ = true; }
  void accumulate(const int64_t value) { accumulated_ += value; }
  int64_t scan_epoch() const { return scan_epoch_; }
  int64_t accumulated() const { return accumulated_; }

protected:
  int on_scan_start() override
  {
    ++scan_epoch_;
    accumulated_ = 0;
    return OB_SUCCESS;
  }

private:
  int64_t scan_epoch_;
  int64_t accumulated_;
};

class TestAggregateColumnReader final : public blocksstable::ObIMicroBlockReader
{
public:
  TestAggregateColumnReader(
      const ObITableReadInfo &read_info,
      const blocksstable::ObStorageDatum *values,
      const int64_t value_count)
    : values_(values)
  {
    is_inited_ = true;
    read_info_ = &read_info;
    row_count_ = value_count;
    reader_type_ = Reader;
  }

  int get_row(const int64_t index, blocksstable::ObDatumRow &row) override
  {
    int ret = OB_SUCCESS;
    if (index < 0 || index >= row_count_ || nullptr == values_
        || row.get_column_count() < 1) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      row.storage_datums_[0] = values_[index];
    }
    return ret;
  }

  int get_row_count(int64_t &row_count) override
  {
    row_count = row_count_;
    return OB_SUCCESS;
  }

  bool has_lob_out_row() const override { return false; }

private:
  const blocksstable::ObStorageDatum *values_;
};

class TestAggregateMultiColumnReader final : public blocksstable::ObIMicroBlockReader
{
public:
  TestAggregateMultiColumnReader(
      const ObITableReadInfo &read_info,
      const blocksstable::ObStorageDatum *values,
      const int64_t row_count,
      const int64_t column_count)
    : values_(values), column_count_(column_count)
  {
    is_inited_ = true;
    read_info_ = &read_info;
    row_count_ = row_count;
    reader_type_ = Reader;
  }

  int get_row(const int64_t index, blocksstable::ObDatumRow &row) override
  {
    int ret = OB_SUCCESS;
    if (index < 0 || index >= row_count_ || nullptr == values_
        || column_count_ <= 0 || row.get_column_count() < column_count_) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      for (int64_t i = 0; i < column_count_; ++i) {
        row.storage_datums_[i] = values_[index * column_count_ + i];
      }
    }
    return ret;
  }

  int get_row_count(int64_t &row_count) override
  {
    row_count = row_count_;
    return OB_SUCCESS;
  }

  bool has_lob_out_row() const override { return false; }

private:
  const blocksstable::ObStorageDatum *values_;
  int64_t column_count_;
};

class TestAggregateScanner final : public blocksstable::ObIMicroBlockRowScanner
{
public:
  TestAggregateScanner(
      common::ObIAllocator &allocator,
      blocksstable::ObIMicroBlockReader &reader)
    : ObIMicroBlockRowScanner(allocator)
  {
    reader_ = &reader;
  }
};

class RecordingAggregateProgram final
  : public share::aggregate::ObIPushdownAggregateProgram
{
public:
  RecordingAggregateProgram()
    : state_(share::aggregate::AGG_PROGRAM_NEW),
      selection_kind_(share::aggregate::AGG_SELECT_DENSE),
      values_(),
      value_count_(0)
  {}

  void destroy(common::ObIAllocator &allocator) override { UNUSED(allocator); }
  share::aggregate::ObPushdownAggregateProgramState state() const override
  { return state_; }
  int reset_scan() override
  {
    state_ = share::aggregate::AGG_PROGRAM_NEW;
    selection_kind_ = share::aggregate::AGG_SELECT_DENSE;
    value_count_ = 0;
    return OB_SUCCESS;
  }
  int can_consume(
      share::aggregate::ObIAggregateInputSegment &segment,
      bool &can_consume) override
  {
    return segment.can_read_values(0, can_consume);
  }
  int consume(share::aggregate::ObIAggregateInputSegment &segment) override
  {
    int ret = OB_SUCCESS;
    share::aggregate::ObAggregateValueBatchView batch;
    selection_kind_ = segment.selection().kind_;
    if (OB_FAIL(segment.read_values(0, batch))) {
    } else if (OB_UNLIKELY(value_count_ + batch.count_ > ARRAYSIZEOF(values_))) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      for (int64_t i = 0; i < batch.count_; ++i) {
        values_[value_count_++] = batch.datums_[i].get_int();
      }
      state_ = share::aggregate::AGG_PROGRAM_CONSUMING;
    }
    return ret;
  }
  int seal() override
  {
    state_ = share::aggregate::AGG_PROGRAM_SEALED;
    return OB_SUCCESS;
  }
  int emit(
      const int64_t max_rows,
      share::aggregate::ObAggregateEmitResult &result) override
  {
    UNUSED(max_rows);
    result.end_ = true;
    state_ = share::aggregate::AGG_PROGRAM_END;
    return OB_SUCCESS;
  }

  share::aggregate::ObAggregateSelectionKind selection_kind() const
  { return selection_kind_; }
  int64_t value_count() const { return value_count_; }
  int64_t value(const int64_t idx) const { return values_[idx]; }

private:
  share::aggregate::ObPushdownAggregateProgramState state_;
  share::aggregate::ObAggregateSelectionKind selection_kind_;
  int64_t values_[8];
  int64_t value_count_;
};

int serialize_extreme(
    common::ObIAllocator &allocator,
    const blocksstable::ObStorageDatum &datum,
    const int32_t column_index,
    const bool is_min,
    char *&buf,
    int64_t &buf_size)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<blocksstable::ObSkipIndexColMeta, 1> metas;
  blocksstable::ObSkipIndexColMeta meta;
  blocksstable::ObSkipIndexAggResult result;
  blocksstable::ObAggRowWriter writer;
  buf = nullptr;
  buf_size = 0;
  meta.col_idx_ = column_index;
  meta.col_type_ = is_min ? blocksstable::SK_IDX_MIN : blocksstable::SK_IDX_MAX;
  if (OB_FAIL(metas.push_back(meta))) {
  } else if (OB_FAIL(result.init(1, allocator))) {
  } else {
    result.get_agg_datum_row().storage_datums_[0] = datum;
    if (OB_FAIL(writer.init(metas, result, allocator))) {
    } else {
      buf_size = writer.get_serialize_data_size();
      if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int64_t pos = 0;
        MEMSET(buf, 0, buf_size);
        if (OB_FAIL(writer.write_agg_data(buf, buf_size, pos))) {
        } else if (pos != buf_size) {
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
  }
  return ret;
}

int serialize_all_null_min(
    common::ObIAllocator &allocator,
    const int64_t row_count,
    const int32_t column_index,
    char *&buf,
    int64_t &buf_size)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<blocksstable::ObSkipIndexColMeta, 2> metas;
  blocksstable::ObSkipIndexColMeta min_meta;
  blocksstable::ObSkipIndexColMeta null_count_meta;
  blocksstable::ObSkipIndexAggResult result;
  blocksstable::ObAggRowWriter writer;
  buf = nullptr;
  buf_size = 0;
  min_meta.col_idx_ = column_index;
  min_meta.col_type_ = blocksstable::SK_IDX_MIN;
  null_count_meta.col_idx_ = column_index;
  null_count_meta.col_type_ = blocksstable::SK_IDX_NULL_COUNT;
  if (OB_FAIL(metas.push_back(min_meta))) {
  } else if (OB_FAIL(metas.push_back(null_count_meta))) {
  } else if (OB_FAIL(result.init(2, allocator))) {
  } else {
    result.get_agg_datum_row().storage_datums_[0].set_null();
    result.get_agg_datum_row().storage_datums_[1].set_int(row_count);
    if (OB_FAIL(writer.init(metas, result, allocator))) {
    } else {
      buf_size = writer.get_serialize_data_size();
      if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(buf_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        int64_t pos = 0;
        MEMSET(buf, 0, buf_size);
        if (OB_FAIL(writer.write_agg_data(buf, buf_size, pos))) {
        } else if (pos != buf_size) {
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
  }
  return ret;
}

TEST(BlockRowStoreScanLifecycle, OpenResetsButPhysicalReusePreserves)
{
  common::ObArenaAllocator allocator;
  ObTableAccessContext context;
  context.stmt_allocator_ = &allocator;
  ScanLifecycleProbeStore store(context);
  store.mark_initialized();

  common::ObSEArray<ObColDesc, 1> columns;
  ObColDesc column;
  column.col_type_.set_int();
  ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(allocator, 1, 0, columns, nullptr));
  common::ObSEArray<int32_t, 1> projector;
  ASSERT_EQ(OB_SUCCESS, projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.out_cols_project_ = &projector;

  ASSERT_EQ(OB_SUCCESS, store.open(iter_param));
  EXPECT_EQ(1, store.scan_epoch());
  EXPECT_EQ(0, store.accumulated());
  store.accumulate(7);

  // SSTable iterator reuse and refresh_table_on_demand use this path inside
  // one logical scan.  Aggregate state must survive both.
  store.reuse();
  store.reuse();
  EXPECT_EQ(1, store.scan_epoch());
  EXPECT_EQ(7, store.accumulated());

  // A fresh scan/rescan calls open() once and starts a new aggregate epoch.
  ASSERT_EQ(OB_SUCCESS, store.open(iter_param));
  EXPECT_EQ(2, store.scan_epoch());
  EXPECT_EQ(0, store.accumulated());
}

TEST(PushdownAggregateInput, CapabilityMissDoesNotPublishPartialReduction)
{
  common::ObArenaAllocator allocator;
  common::ObSEArray<ObColDesc, 1> columns;
  ObColDesc column;
  column.col_type_.set_int();
  ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
  share::schema::ObColumnParam column_param(allocator);
  column_param.set_column_id(1);
  column_param.set_meta_type(column.col_type_);
  column_param.set_nullable_for_write(true);
  common::ObSEArray<share::schema::ObColumnParam *, 1> column_params;
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&column_param));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(
      allocator, 1, 0, columns, nullptr, &column_params));

  common::ObSEArray<int32_t, 1> aggregate_projector;
  ASSERT_EQ(OB_SUCCESS, aggregate_projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.agg_cols_project_ = &aggregate_projector;
  // Dense reader input cannot derive a nullable column's exact null count
  // without row ids.  This is a normal capability miss.
  ObPushdownAggregateInput input(iter_param, nullptr, nullptr, 3, false);

  share::aggregate::ObAggregateReduction sentinel;
  sentinel.present_ = share::aggregate::AGG_REDUCE_SUM;
  sentinel.row_count_ = 91;
  sentinel.null_count_ = 92;
  sentinel.logical_bytes_ = 93;
  ASSERT_EQ(OB_NOT_SUPPORTED, input.try_reduce(
      0,
      share::aggregate::AGG_REDUCE_ROW_COUNT
          | share::aggregate::AGG_REDUCE_NULL_COUNT,
      sentinel));
  EXPECT_EQ(share::aggregate::AGG_REDUCE_SUM, sentinel.present_);
  EXPECT_EQ(91, sentinel.row_count_);
  EXPECT_EQ(92, sentinel.null_count_);
  EXPECT_EQ(93, sentinel.logical_bytes_);
}

TEST(PushdownAggregateInput, ReaderWithoutDefaultStillPublishesOrdinaryValues)
{
  common::ObArenaAllocator allocator;
  common::ObSEArray<ObColDesc, 1> columns;
  ObColDesc column;
  column.col_type_.set_int();
  ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
  share::schema::ObColumnParam column_param(allocator);
  column_param.set_column_id(1);
  column_param.set_meta_type(column.col_type_);
  common::ObSEArray<share::schema::ObColumnParam *, 1> column_params;
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&column_param));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(
      allocator, 1, 0, columns, nullptr, &column_params));
  common::ObSEArray<int32_t, 1> aggregate_projector;
  ASSERT_EQ(OB_SUCCESS, aggregate_projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.agg_cols_project_ = &aggregate_projector;

  blocksstable::ObStorageDatum physical_values[2];
  physical_values[0].set_int(10);
  physical_values[1].set_int(20);
  TestAggregateColumnReader reader(read_info, physical_values, 2);
  int32_t selected_ids[] = {1, 0};
  ObPushdownAggregateInput input(
      iter_param, &reader, selected_ids, ARRAYSIZEOF(selected_ids), false);
  bool can_read = false;
  ASSERT_EQ(OB_SUCCESS, input.can_read_values(0, can_read));
  ASSERT_TRUE(can_read);
  share::aggregate::ObAggregateValueBatchView values;
  ASSERT_EQ(OB_SUCCESS, input.read_values(0, values));
  ASSERT_EQ(2, values.count_);
  EXPECT_EQ(20, values.datums_[0].get_int());
  EXPECT_EQ(10, values.datums_[1].get_int());
}

TEST(PushdownAggregateInput, CanonicalSlotsPutGroupingInputsBeforeAggregateInputs)
{
  common::ObArenaAllocator allocator;
  common::ObSEArray<ObColDesc, 2> columns;
  common::ObSEArray<share::schema::ObColumnParam *, 2> column_params;
  share::schema::ObColumnParam aggregate_column(allocator);
  share::schema::ObColumnParam group_column(allocator);
  ObColDesc aggregate_desc;
  ObColDesc group_desc;
  aggregate_desc.col_type_.set_int();
  group_desc.col_type_.set_int();
  aggregate_column.set_column_id(1);
  aggregate_column.set_meta_type(aggregate_desc.col_type_);
  group_column.set_column_id(2);
  group_column.set_meta_type(group_desc.col_type_);
  ASSERT_EQ(OB_SUCCESS, columns.push_back(aggregate_desc));
  ASSERT_EQ(OB_SUCCESS, columns.push_back(group_desc));
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&aggregate_column));
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&group_column));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(
      allocator, 2, 0, columns, nullptr, &column_params));

  // Deliberately reverse physical column order: canonical slot 0 is the
  // grouping input (physical column 1), then slot 1 is the aggregate input
  // (physical column 0).
  common::ObSEArray<int32_t, 1> group_projector;
  common::ObSEArray<int32_t, 1> aggregate_projector;
  ASSERT_EQ(OB_SUCCESS, group_projector.push_back(1));
  ASSERT_EQ(OB_SUCCESS, aggregate_projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.group_by_cols_project_ = &group_projector;
  iter_param.agg_cols_project_ = &aggregate_projector;

  blocksstable::ObStorageDatum physical_values[6];
  physical_values[0].set_int(10);
  physical_values[1].set_int(100);
  physical_values[2].set_int(20);
  physical_values[3].set_int(200);
  physical_values[4].set_int(30);
  physical_values[5].set_int(300);
  TestAggregateMultiColumnReader reader(read_info, physical_values, 3, 2);
  int32_t selected_ids[] = {2, 0};
  ObPushdownAggregateInput input(
      iter_param, &reader, selected_ids, ARRAYSIZEOF(selected_ids), false);

  ASSERT_EQ(share::aggregate::AGG_SELECT_ROW_IDS, input.selection().kind_);
  ASSERT_EQ(2, input.selection().count_);
  ASSERT_EQ(selected_ids, input.selection().row_ids_);

  share::aggregate::ObAggregateValueBatchView values;
  ASSERT_EQ(OB_SUCCESS, input.read_values(0, values));
  ASSERT_EQ(2, values.count_);
  EXPECT_EQ(300, values.datums_[0].get_int());
  EXPECT_EQ(100, values.datums_[1].get_int());

  ASSERT_EQ(OB_SUCCESS, input.read_values(1, values));
  ASSERT_EQ(2, values.count_);
  EXPECT_EQ(30, values.datums_[0].get_int());
  EXPECT_EQ(10, values.datums_[1].get_int());

  bool can_read = true;
  EXPECT_EQ(OB_INVALID_ARGUMENT, input.can_read_values(2, can_read));
  EXPECT_FALSE(can_read);
  EXPECT_EQ(OB_INVALID_ARGUMENT, input.read_values(-1, values));
  EXPECT_EQ(nullptr, values.datums_);
  EXPECT_EQ(0, values.count_);
}

TEST(PushdownAggregateInput, CharPaddingModeIsConsistentForRowsAndExactIndex)
{
  common::ObArenaAllocator allocator;
  common::ObSEArray<ObColDesc, 1> columns;
  ObColDesc column;
  column.col_type_.set_type(ObCharType);
  column.col_type_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
  share::schema::ObColumnParam column_param(allocator);
  column_param.set_column_id(1);
  column_param.set_meta_type(column.col_type_);
  common::ObAccuracy accuracy;
  accuracy.set_length(4);
  accuracy.set_length_semantics(LS_BYTE);
  column_param.set_accuracy(accuracy);
  common::ObSEArray<share::schema::ObColumnParam *, 1> column_params;
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&column_param));
  common::ObFixedArray<ObColExtend, common::ObIAllocator> column_extends(&allocator);
  ASSERT_EQ(OB_SUCCESS, column_extends.init(1));
  ObColExtend column_extend;
  column_extend.skip_index_attr_.set_min_max();
  ASSERT_EQ(OB_SUCCESS, column_extends.push_back(column_extend));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(
      allocator, 1, 0, columns, nullptr, &column_params, &column_extends));
  common::ObSEArray<int32_t, 1> aggregate_projector;
  ASSERT_EQ(OB_SUCCESS, aggregate_projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.agg_cols_project_ = &aggregate_projector;

  blocksstable::ObDatumRow row;
  ASSERT_EQ(OB_SUCCESS, row.init(1));
  row.storage_datums_[0].set_string("a", 1);
  ObPushdownAggregateInput raw_row_input(iter_param, row, false);
  ObPushdownAggregateInput padded_row_input(iter_param, row, true);
  share::aggregate::ObAggregateValueBatchView raw_values;
  share::aggregate::ObAggregateValueBatchView padded_values;
  ASSERT_EQ(OB_SUCCESS, raw_row_input.read_values(0, raw_values));
  ASSERT_EQ(OB_SUCCESS, padded_row_input.read_values(0, padded_values));
  EXPECT_EQ("a", std::string(raw_values.datums_[0].ptr_, raw_values.datums_[0].len_));
  EXPECT_EQ("a   ", std::string(padded_values.datums_[0].ptr_, padded_values.datums_[0].len_));

  blocksstable::ObStorageDatum index_min;
  index_min.set_string("a", 1);
  char *agg_buf = nullptr;
  int64_t agg_buf_size = 0;
  ASSERT_EQ(OB_SUCCESS, serialize_extreme(
      allocator,
      index_min,
      read_info.get_columns_index().at(0),
      true,
      agg_buf,
      agg_buf_size));
  blocksstable::ObIndexBlockRowHeader row_header;
  row_header.row_count_ = 1;
  row_header.set_major_node();
  blocksstable::ObMicroIndexInfo index_info;
  index_info.row_header_ = &row_header;
  index_info.agg_row_buf_ = agg_buf;
  index_info.agg_buf_size_ = agg_buf_size;
  index_info.set_blockscan();
  ObPushdownAggregateInput raw_index_input(iter_param, index_info, false);
  ObPushdownAggregateInput padded_index_input(iter_param, index_info, true);
  share::aggregate::ObAggregateReduction raw_reduction;
  share::aggregate::ObAggregateReduction padded_reduction;
  ASSERT_EQ(OB_SUCCESS, raw_index_input.try_reduce(
      0, share::aggregate::AGG_REDUCE_MIN, raw_reduction));
  ASSERT_EQ(OB_SUCCESS, padded_index_input.try_reduce(
      0, share::aggregate::AGG_REDUCE_MIN, padded_reduction));
  ASSERT_NE(nullptr, raw_reduction.min_);
  ASSERT_NE(nullptr, padded_reduction.min_);
  EXPECT_EQ("a", std::string(raw_reduction.min_->ptr_, raw_reduction.min_->len_));
  EXPECT_EQ("a   ", std::string(padded_reduction.min_->ptr_, padded_reduction.min_->len_));
}

TEST(PushdownAggregateInput, ExactIndexAllNullPublishesNullExtreme)
{
  common::ObArenaAllocator allocator;
  common::ObSEArray<ObColDesc, 1> columns;
  ObColDesc column;
  column.col_type_.set_int();
  ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
  share::schema::ObColumnParam column_param(allocator);
  column_param.set_column_id(1);
  column_param.set_meta_type(column.col_type_);
  column_param.set_nullable_for_write(true);
  common::ObSEArray<share::schema::ObColumnParam *, 1> column_params;
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&column_param));
  common::ObFixedArray<ObColExtend, common::ObIAllocator> column_extends(&allocator);
  ASSERT_EQ(OB_SUCCESS, column_extends.init(1));
  ObColExtend column_extend;
  column_extend.skip_index_attr_.set_min_max();
  ASSERT_EQ(OB_SUCCESS, column_extends.push_back(column_extend));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(
      allocator, 1, 0, columns, nullptr, &column_params, &column_extends));
  common::ObSEArray<int32_t, 1> aggregate_projector;
  ASSERT_EQ(OB_SUCCESS, aggregate_projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.agg_cols_project_ = &aggregate_projector;

  char *agg_buf = nullptr;
  int64_t agg_buf_size = 0;
  static const int64_t ROW_COUNT = 3;
  ASSERT_EQ(OB_SUCCESS, serialize_all_null_min(
      allocator,
      ROW_COUNT,
      read_info.get_columns_index().at(0),
      agg_buf,
      agg_buf_size));
  blocksstable::ObIndexBlockRowHeader row_header;
  row_header.row_count_ = ROW_COUNT;
  row_header.set_major_node();
  blocksstable::ObMicroIndexInfo index_info;
  index_info.row_header_ = &row_header;
  index_info.agg_row_buf_ = agg_buf;
  index_info.agg_buf_size_ = agg_buf_size;
  index_info.set_blockscan();
  ObPushdownAggregateInput input(iter_param, index_info, false);
  share::aggregate::ObAggregateReduction reduction;
  ASSERT_EQ(OB_SUCCESS, input.try_reduce(
      0,
      share::aggregate::AGG_REDUCE_MIN
          | share::aggregate::AGG_REDUCE_NULL_COUNT,
      reduction));
  EXPECT_NE(0U, reduction.present_ & share::aggregate::AGG_REDUCE_MIN);
  EXPECT_NE(0U, reduction.present_ & share::aggregate::AGG_REDUCE_NULL_COUNT);
  EXPECT_EQ(ROW_COUNT, reduction.null_count_);
  ASSERT_NE(nullptr, reduction.min_);
  EXPECT_TRUE(reduction.min_->is_null());
}

TEST(PushdownAggregateInput, MinorLooseSummaryFallsBackAndReaderRestoresMoreExtremeDefault)
{
  common::ObArenaAllocator allocator;
  common::ObSEArray<ObColDesc, 1> columns;
  ObColDesc column;
  column.col_type_.set_varchar();
  column.col_type_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, columns.push_back(column));
  share::schema::ObColumnParam column_param(allocator);
  column_param.set_column_id(1);
  column_param.set_meta_type(column.col_type_);
  common::ObObj default_value;
  default_value.set_varchar(ObString::make_string("a"));
  default_value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, column_param.set_orig_default_value(default_value));
  common::ObSEArray<share::schema::ObColumnParam *, 1> column_params;
  ASSERT_EQ(OB_SUCCESS, column_params.push_back(&column_param));
  common::ObFixedArray<ObColExtend, common::ObIAllocator> column_extends(&allocator);
  ASSERT_EQ(OB_SUCCESS, column_extends.init(1));
  ObColExtend column_extend;
  column_extend.skip_index_attr_.set_min_max();
  column_extend.skip_index_attr_.set_loose_min_max();
  ASSERT_EQ(OB_SUCCESS, column_extends.push_back(column_extend));
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(
      allocator, 1, 0, columns, nullptr, &column_params, &column_extends));
  common::ObSEArray<int32_t, 1> aggregate_projector;
  ASSERT_EQ(OB_SUCCESS, aggregate_projector.push_back(0));
  ObTableIterParam iter_param;
  iter_param.table_id_ = 1;
  iter_param.read_info_ = &read_info;
  iter_param.agg_cols_project_ = &aggregate_projector;

  blocksstable::ObStorageDatum loose_min;
  loose_min.set_string("z", 1);
  char *agg_buf = nullptr;
  int64_t agg_buf_size = 0;
  ASSERT_EQ(OB_SUCCESS, serialize_extreme(
      allocator,
      loose_min,
      read_info.get_columns_index().at(0),
      true,
      agg_buf,
      agg_buf_size));
  blocksstable::ObIndexBlockRowHeader minor_header;
  minor_header.row_count_ = 2;
  blocksstable::ObMicroIndexInfo index_info;
  index_info.row_header_ = &minor_header;
  index_info.agg_row_buf_ = agg_buf;
  index_info.agg_buf_size_ = agg_buf_size;
  index_info.set_blockscan();
  ObPushdownAggregateInput loose_index_input(iter_param, index_info, false);
  share::aggregate::ObAggregateReduction reduction;
  EXPECT_EQ(OB_NOT_SUPPORTED, loose_index_input.try_reduce(
      0, share::aggregate::AGG_REDUCE_MIN, reduction));

  blocksstable::ObStorageDatum physical_values[2];
  physical_values[0].set_string("z", 1);
  physical_values[1].set_nop();
  TestAggregateColumnReader reader(read_info, physical_values, 2);
  int32_t row_ids[] = {0, 1};
  ObPushdownAggregateInput reader_input(
      iter_param, &reader, row_ids, ARRAYSIZEOF(row_ids), false);
  share::aggregate::ObAggregateValueBatchView values;
  ASSERT_EQ(OB_SUCCESS, reader_input.read_values(0, values));
  ASSERT_EQ(2, values.count_);
  EXPECT_EQ("z", std::string(values.datums_[0].ptr_, values.datums_[0].len_));
  EXPECT_EQ("a", std::string(values.datums_[1].ptr_, values.datums_[1].len_));
}

} // namespace
} // namespace storage
} // namespace oceanbase
