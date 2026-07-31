/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#define USING_LOG_PREFIX STORAGE

#include <gtest/gtest.h>
#include <new>
#include "storage/ddl/ob_ddl_row_tmp_file.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
namespace
{

class FakeSpillBatchSpool final : public query::ObISpillBatchSpool
{
public:
  FakeSpillBatchSpool(const int64_t column_count, const int injected_next_error)
    : batch_rows_(), output_vectors_(), read_idx_(0), row_count_(0),
      injected_next_error_(injected_next_error), state_(query::SPILL_BATCH_WRITING),
      first_error_(OB_SUCCESS)
  {
    EXPECT_EQ(OB_SUCCESS, output_vectors_.prepare_allocate(column_count));
    for (int64_t i = 0; i < output_vectors_.count(); ++i) {
      output_vectors_.at(i) = nullptr;
    }
  }

  int append_batch(const query::ObSpillBatchView &batch,
                   query::ObSpillBatchAppendResult &result) override
  {
    int ret = OB_SUCCESS;
    result = query::ObSpillBatchAppendResult();
    if (query::SPILL_BATCH_FAILED == state_) {
      ret = first_error_;
    } else if (query::SPILL_BATCH_WRITING != state_) {
      ret = latch_error(OB_STATE_NOT_MATCH);
    } else if (OB_ISNULL(batch.vectors_) || batch.row_count_ <= 0 ||
               batch.vectors_->count() != output_vectors_.count()) {
      ret = latch_error(OB_INVALID_ARGUMENT);
    } else if (OB_FAIL(batch_rows_.push_back(batch.row_count_))) {
      ret = latch_error(ret);
    } else {
      row_count_ += batch.row_count_;
      result.rotation_recommended_ = batch_rows_.count() >= 2;
    }
    return ret;
  }

  int seal() override
  {
    int ret = OB_SUCCESS;
    if (query::SPILL_BATCH_FAILED == state_) {
      ret = first_error_;
    } else if (query::SPILL_BATCH_WRITING == state_) {
      state_ = query::SPILL_BATCH_SEALED;
    }
    return ret;
  }

  int next_batch(query::ObSpillBatchView &batch) override
  {
    int ret = OB_SUCCESS;
    batch = query::ObSpillBatchView();
    if (query::SPILL_BATCH_FAILED == state_) {
      ret = first_error_;
    } else if (query::SPILL_BATCH_WRITING == state_) {
      ret = latch_error(OB_STATE_NOT_MATCH);
    } else if (query::SPILL_BATCH_EXHAUSTED == state_) {
      ret = OB_ITER_END;
    } else if (OB_SUCCESS != injected_next_error_ && 0 == read_idx_) {
      ret = latch_error(injected_next_error_);
    } else if (read_idx_ >= batch_rows_.count()) {
      state_ = query::SPILL_BATCH_EXHAUSTED;
      ret = OB_ITER_END;
    } else {
      state_ = query::SPILL_BATCH_READING;
      batch = query::ObSpillBatchView(
          output_vectors_, batch_rows_.at(read_idx_++));
    }
    return ret;
  }

  query::ObSpillBatchSpoolStats get_stats() const override
  {
    query::ObSpillBatchSpoolStats stats;
    stats.row_count_ = row_count_;
    stats.state_ = state_;
    stats.first_error_ = first_error_;
    return stats;
  }

private:
  int latch_error(const int error)
  {
    if (OB_SUCCESS == first_error_) {
      first_error_ = error;
    }
    state_ = query::SPILL_BATCH_FAILED;
    return first_error_;
  }

private:
  ObArray<int64_t> batch_rows_;
  ObArray<ObIVector *> output_vectors_;
  int64_t read_idx_;
  int64_t row_count_;
  int injected_next_error_;
  query::ObSpillBatchSpoolState state_;
  int first_error_;
};

class FakeSpillBatchSpoolFactory final : public query::ObISpillBatchSpoolFactory
{
public:
  explicit FakeSpillBatchSpoolFactory(const int injected_next_error = OB_SUCCESS)
    : injected_next_error_(injected_next_error), create_count_(0),
      destroy_count_(0), active_count_(0)
  {}

  int create(const ObIArray<query::ObSpillColumnDesc> &columns,
             const query::ObSpillBatchSpoolOptions &options,
             query::ObISpillBatchSpool *&spool) override
  {
    int ret = OB_SUCCESS;
    spool = nullptr;
    if (columns.empty() || options.max_batch_size_ <= 0) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      FakeSpillBatchSpool *fake =
          new (std::nothrow) FakeSpillBatchSpool(columns.count(), injected_next_error_);
      if (nullptr == fake) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        spool = fake;
        ++create_count_;
        ++active_count_;
      }
    }
    return ret;
  }

  void destroy(query::ObISpillBatchSpool *&spool) override
  {
    if (nullptr != spool) {
      delete static_cast<FakeSpillBatchSpool *>(spool);
      spool = nullptr;
      ++destroy_count_;
      --active_count_;
    }
  }

  int injected_next_error_;
  int64_t create_count_;
  int64_t destroy_count_;
  int64_t active_count_;
};

int prepare_row_file(ObDDLRowFile &row_file,
                     FakeSpillBatchSpoolFactory &factory)
{
  int ret = OB_SUCCESS;
  ObArray<ObColumnSchemaItem> columns;
  ObColumnSchemaItem column;
  column.is_valid_ = true;
  column.col_type_.set_type(ObIntType);
  if (OB_FAIL(columns.push_back(column))) {
  } else {
    // DDL's multi-version columns are intentionally schema-invalid but carry
    // a usable type.  The stable spill descriptor must not retain that flag.
    column.is_valid_ = false;
    column.col_type_.set_type(ObIntType);
    ret = columns.push_back(column);
  }
  if (OB_FAIL(ret)) {
  } else {
    ret = row_file.open(columns, ObTabletID(200001), 0, 16, factory, 1024);
  }
  return ret;
}

int append_rows(ObDDLRowFile &row_file, const int64_t row_count)
{
  blocksstable::ObBatchDatumRows batch;
  int ret = batch.vectors_.push_back(nullptr);
  if (OB_SUCC(ret)) {
    ret = batch.vectors_.push_back(nullptr);
  }
  if (OB_SUCC(ret)) {
    batch.row_count_ = row_count;
    ret = row_file.append_batch(batch);
  }
  return ret;
}

TEST(TestDDLRowTmpFile, fifo_seal_iter_end_and_destroy_exactly_once)
{
  FakeSpillBatchSpoolFactory factory;
  {
    ObDDLRowFile row_file;
    ASSERT_EQ(OB_SUCCESS, prepare_row_file(row_file, factory));
    ASSERT_EQ(OB_SUCCESS, append_rows(row_file, 2));
    ASSERT_FALSE(row_file.should_rotate());
    ASSERT_EQ(OB_SUCCESS, append_rows(row_file, 3));
    ASSERT_TRUE(row_file.should_rotate());
    ASSERT_EQ(OB_SUCCESS, row_file.seal());
    ASSERT_EQ(OB_SUCCESS, row_file.seal());

    blocksstable::ObBatchDatumRows *batch = nullptr;
    ASSERT_EQ(OB_SUCCESS, row_file.get_next_batch(batch));
    ASSERT_NE(nullptr, batch);
    ASSERT_EQ(2, batch->row_count_);
    ASSERT_EQ(2, batch->vectors_.count());
    blocksstable::ObBatchDatumRows *borrowed_batch = batch;

    ASSERT_EQ(OB_SUCCESS, row_file.get_next_batch(batch));
    ASSERT_EQ(borrowed_batch, batch);
    ASSERT_EQ(3, batch->row_count_);
    ASSERT_EQ(OB_ITER_END, row_file.get_next_batch(batch));
    ASSERT_EQ(nullptr, batch);
    ASSERT_EQ(OB_ITER_END, row_file.get_next_batch(batch));

    ASSERT_EQ(OB_SUCCESS, row_file.close());
    ASSERT_EQ(OB_NOT_INIT, row_file.close());
    ASSERT_EQ(1, factory.destroy_count_);
  }
  ASSERT_EQ(1, factory.create_count_);
  ASSERT_EQ(1, factory.destroy_count_);
  ASSERT_EQ(0, factory.active_count_);
}

TEST(TestDDLRowTmpFile, first_error_is_latched_and_propagated)
{
  FakeSpillBatchSpoolFactory factory(OB_TIMEOUT);
  {
    ObDDLRowFile row_file;
    ASSERT_EQ(OB_SUCCESS, prepare_row_file(row_file, factory));
    ASSERT_EQ(OB_SUCCESS, append_rows(row_file, 1));
    ASSERT_EQ(OB_SUCCESS, row_file.seal());

    blocksstable::ObBatchDatumRows *batch = nullptr;
    ASSERT_EQ(OB_TIMEOUT, row_file.get_next_batch(batch));
    ASSERT_EQ(nullptr, batch);
    ASSERT_EQ(OB_TIMEOUT, row_file.get_next_batch(batch));
    ASSERT_EQ(OB_TIMEOUT, append_rows(row_file, 1));
    ASSERT_EQ(OB_TIMEOUT, row_file.seal());
    const query::ObSpillBatchSpoolStats stats = row_file.get_stats();
    ASSERT_EQ(query::SPILL_BATCH_FAILED, stats.state_);
    ASSERT_EQ(OB_TIMEOUT, stats.first_error_);
    // Destructor owns the only destroy call in this path.
  }
  ASSERT_EQ(1, factory.create_count_);
  ASSERT_EQ(1, factory.destroy_count_);
  ASSERT_EQ(0, factory.active_count_);
}

} // namespace
} // namespace storage
} // namespace oceanbase
