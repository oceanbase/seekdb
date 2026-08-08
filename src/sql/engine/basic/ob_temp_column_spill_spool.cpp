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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/basic/ob_temp_column_spill_spool.h"
#include "sql/engine/basic/ob_temp_column_store.h"
#include "query/engine/ob_batch_rows.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
namespace
{

class ObTempColumnSpillSpool final : public query::ObISpillBatchSpool
{
public:
  ObTempColumnSpillSpool()
    : vector_allocator_(ObMemAttr("SpillVectors")), output_vectors_(), store_(),
      iterator_(), options_(), state_(query::SPILL_BATCH_WRITING),
      first_error_(OB_SUCCESS), column_count_(0)
  {}

  ~ObTempColumnSpillSpool() override
  {
    iterator_.reset();
    store_.reset();
    output_vectors_.reset();
    vector_allocator_.reset();
  }

  int init(const ObIArray<query::ObSpillColumnDesc> &columns,
           const query::ObSpillBatchSpoolOptions &options)
  {
    int ret = OB_SUCCESS;
    options_ = options;
    column_count_ = columns.count();
    if (OB_FAIL(ObTempColumnStore::init_vectors(
            columns, vector_allocator_, output_vectors_))) {
    } else if (OB_FAIL(store_.init(output_vectors_,
                                   options_.max_batch_size_,
                                   ObMemAttr("TempColumnSpool"),
                                   options_.resident_memory_limit_,
                                   true /* enable_dump */,
                                   options_.compressor_type_))) {
    } else {
      store_.set_dir_id(options_.dir_id_);
    }
    return ret;
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
               batch.row_count_ > options_.max_batch_size_ ||
               batch.vectors_->count() != column_count_) {
      ret = latch_error(OB_INVALID_ARGUMENT);
      LOG_WARN("invalid spill batch", K(ret), KP(batch.vectors_),
          K(batch.row_count_), K(column_count_), K(options_.max_batch_size_));
    } else {
      ObBatchRows batch_rows;
      int64_t stored_row_count = 0;
      batch_rows.size_ = batch.row_count_;
      batch_rows.set_all_rows_active(true);
      if (OB_FAIL(store_.add_batch(*batch.vectors_, batch_rows, stored_row_count))) {
        ret = latch_error(ret);
        LOG_WARN("failed to append spill batch", K(ret), K(batch.row_count_));
      } else if (OB_UNLIKELY(stored_row_count != batch.row_count_)) {
        ret = latch_error(OB_ERR_UNEXPECTED);
        LOG_WARN("spill store accepted an unexpected row count", K(ret),
            K(stored_row_count), K(batch.row_count_));
      } else {
        result.rotation_recommended_ =
            store_.get_mem_hold() > options_.rotation_threshold_;
      }
    }
    return ret;
  }

  int seal() override
  {
    int ret = OB_SUCCESS;
    if (query::SPILL_BATCH_FAILED == state_) {
      ret = first_error_;
    } else if (query::SPILL_BATCH_WRITING != state_) {
      // Once handed to the reader, seal remains a no-op.
    } else if (OB_FAIL(store_.dump(true /* all_dump */))) {
      ret = latch_error(ret);
      LOG_WARN("failed to dump spill spool", K(ret));
    } else if (OB_FAIL(store_.finish_add_row(true /* need_dump */))) {
      ret = latch_error(ret);
      LOG_WARN("failed to finish spill spool", K(ret));
    } else {
      state_ = query::SPILL_BATCH_SEALED;
    }
    return ret;
  }

  int next_batch(query::ObSpillBatchView &batch) override
  {
    int ret = OB_SUCCESS;
    int64_t read_row_count = 0;
    batch = query::ObSpillBatchView();
    if (query::SPILL_BATCH_FAILED == state_) {
      ret = first_error_;
    } else if (query::SPILL_BATCH_WRITING == state_) {
      ret = latch_error(OB_STATE_NOT_MATCH);
    } else if (query::SPILL_BATCH_EXHAUSTED == state_) {
      ret = OB_ITER_END;
    } else {
      if (query::SPILL_BATCH_SEALED == state_) {
        if (OB_FAIL(store_.begin(iterator_, options_.async_read_))) {
          ret = latch_error(ret);
          LOG_WARN("failed to begin spill iteration", K(ret));
        } else {
          state_ = query::SPILL_BATCH_READING;
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(iterator_.get_next_batch(
              output_vectors_, read_row_count))) {
        if (OB_ITER_END == ret) {
          state_ = query::SPILL_BATCH_EXHAUSTED;
        } else {
          ret = latch_error(ret);
          LOG_WARN("failed to read spill batch", K(ret));
        }
      } else if (OB_SUCC(ret)) {
        batch = query::ObSpillBatchView(output_vectors_, read_row_count);
      }
    }
    return ret;
  }

  query::ObSpillBatchSpoolStats get_stats() const override
  {
    query::ObSpillBatchSpoolStats stats;
    stats.row_count_ = store_.get_row_cnt();
    stats.resident_bytes_ = store_.get_mem_hold();
    stats.spilled_bytes_ = store_.get_file_size();
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
  ObArenaAllocator vector_allocator_;
  ObArray<ObIVector *> output_vectors_;
  ObTempColumnStore store_;
  ObTempColumnStore::Iterator iterator_;
  query::ObSpillBatchSpoolOptions options_;
  query::ObSpillBatchSpoolState state_;
  int first_error_;
  int64_t column_count_;
};

class ObTempColumnSpillSpoolFactory final : public query::ObISpillBatchSpoolFactory
{
public:
  int create(const ObIArray<query::ObSpillColumnDesc> &columns,
             const query::ObSpillBatchSpoolOptions &options,
             query::ObISpillBatchSpool *&spool) override
  {
    int ret = OB_SUCCESS;
    ObTempColumnSpillSpool *new_spool = nullptr;
    spool = nullptr;
    if (OB_UNLIKELY(columns.empty() || options.max_batch_size_ <= 0 ||
                    options.resident_memory_limit_ <= 0 ||
                    options.rotation_threshold_ <= 0 || options.dir_id_ < 0)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid spill spool options", K(ret), K(columns.count()),
          K(options.max_batch_size_), K(options.resident_memory_limit_),
          K(options.rotation_threshold_), K(options.dir_id_));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
      if (OB_UNLIKELY(columns.at(i).type_ >= ObMaxType)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid spill column", K(ret), K(i),
            K(columns.at(i).type_));
      }
    }
    if (OB_SUCC(ret)) {
      new_spool = OB_NEW(ObTempColumnSpillSpool, ObMemAttr("TempColSpool"));
      if (OB_ISNULL(new_spool)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate spill spool", K(ret));
      } else if (OB_FAIL(new_spool->init(columns, options))) {
      } else {
        spool = new_spool;
      }
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(new_spool)) {
      new_spool->~ObTempColumnSpillSpool();
      ob_free(new_spool);
    }
    return ret;
  }

  void destroy(query::ObISpillBatchSpool *&spool) override
  {
    if (OB_NOT_NULL(spool)) {
      ObTempColumnSpillSpool *concrete =
          static_cast<ObTempColumnSpillSpool *>(spool);
      concrete->~ObTempColumnSpillSpool();
      ob_free(concrete);
      spool = nullptr;
    }
  }
};

} // namespace

query::ObISpillBatchSpoolFactory &get_temp_column_spill_spool_factory()
{
  static ObTempColumnSpillSpoolFactory factory;
  return factory;
}

} // namespace sql
} // namespace oceanbase
