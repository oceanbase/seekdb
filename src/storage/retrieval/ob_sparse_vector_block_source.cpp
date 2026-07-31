/*
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

#define USING_LOG_PREFIX STORAGE

#include "data_plane/access/ob_table_scan_access.h"
#include "data_plane/retrieval/ob_sparse_retrieval.h"
#include "storage/retrieval/ob_block_max_iter.h"

namespace oceanbase
{
namespace data_plane
{
namespace
{

class ObSparseVectorBlockSource final : public ObISparseRetrievalBlockSource
{
public:
  ObSparseVectorBlockSource()
    : allocator_(nullptr),
      scan_param_(nullptr),
      iter_param_(),
      ranking_param_(),
      block_iter_(),
      max_score_(0.0),
      saved_error_(OB_SUCCESS),
      prepared_(false),
      exhausted_(false),
      is_reset_(true)
  {}
  virtual ~ObSparseVectorBlockSource() = default;

  int init(
      common::ObIAllocator &allocator,
      storage::ObTableScanParam &scan_param,
      const ObSparseVectorBlockSourceSpec &spec)
  {
    int ret = OB_SUCCESS;
    allocator_ = &allocator;
    if (!spec.is_valid() || !scan_param.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid sparse vector block source request", K(ret), K(scan_param));
    } else {
      iter_param_.stat_cols_.set_allocator(&allocator);
      iter_param_.stat_projectors_.set_allocator(&allocator);
      if (OB_FAIL(iter_param_.stat_cols_.init(spec.column_count_))) {
        LOG_WARN("failed to initialize block statistic columns", K(ret));
      } else if (OB_FAIL(iter_param_.stat_projectors_.init(spec.column_count_))) {
        LOG_WARN("failed to initialize block statistic projectors", K(ret));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < spec.column_count_; ++i) {
        const ObSparseVectorBlockColumnSpec &column = spec.columns_[i];
        if (OB_FAIL(iter_param_.stat_cols_.push_back(blocksstable::ObSkipIndexColMeta(
            column.store_index_,
            static_cast<blocksstable::ObSkipIndexColType>(column.statistic_type_))))) {
          LOG_WARN("failed to copy block statistic column", K(ret), K(i));
        } else if (OB_FAIL(iter_param_.stat_projectors_.push_back(column.projector_))) {
          LOG_WARN("failed to copy block statistic projector", K(ret), K(i));
        }
      }
      if (OB_SUCC(ret)) {
        iter_param_.min_domain_id_col_idx_ = spec.min_domain_id_index_;
        iter_param_.max_domain_id_col_idx_ = spec.max_domain_id_index_;
        iter_param_.score_col_idx_ = spec.score_index_;
        iter_param_.domain_id_idx_in_rowkey_ = spec.domain_id_rowkey_index_;
        iter_param_.dim_col_idx_in_rowkey_ = spec.dimension_rowkey_index_;
        iter_param_.domain_id_obj_meta_ = spec.domain_id_meta_;
        iter_param_.dim_obj_meta_ = spec.dimension_meta_;
        iter_param_.scan_allocator_ = &allocator;
        iter_param_.ranking_type_ = storage::OB_MAX_SCORE_INNER_PRODUCT;
        ranking_param_.score_col_idx_ = spec.score_index_;
        ranking_param_.query_value_ = spec.query_value_;
        scan_param_ = &scan_param;
        is_reset_ = false;
      }
    }
    return ret;
  }

  virtual int max_score(double &score) override
  {
    int ret = OB_SUCCESS;
    score = 0.0;
    if (OB_SUCCESS != saved_error_) {
      ret = saved_error_;
    } else if (OB_FAIL(prepare())) {
    } else {
      score = max_score_;
    }
    return remember(ret);
  }

  virtual int advance_to(
      const ObSparseRetrievalIdView &target,
      const bool inclusive,
      ObSparseRetrievalBlockView &block) override
  {
    int ret = OB_SUCCESS;
    const storage::ObMaxScoreTuple *tuple = nullptr;
    block = ObSparseRetrievalBlockView();
    if (OB_SUCCESS != saved_error_) {
      ret = saved_error_;
    } else if (exhausted_) {
      ret = OB_ITER_END;
    } else if (!target.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(prepare())) {
    } else if (OB_FAIL(block_iter_.advance_to(*target.datum_, inclusive))) {
      if (OB_ITER_END == ret) {
        exhausted_ = true;
      } else {
        LOG_WARN("failed to advance sparse vector block source", K(ret), K(inclusive));
      }
    } else if (OB_FAIL(block_iter_.get_curr_max_score_tuple(tuple))) {
      LOG_WARN("failed to read sparse vector block bound", K(ret));
    } else if (OB_ISNULL(tuple) || OB_ISNULL(tuple->min_domain_id_)
        || OB_ISNULL(tuple->max_domain_id_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sparse vector block source returned an invalid bound", K(ret));
    } else {
      block.min_id_ = ObSparseRetrievalIdView(*tuple->min_domain_id_);
      block.max_id_ = ObSparseRetrievalIdView(*tuple->max_domain_id_);
      block.score_upper_bound_ = tuple->max_score_;
    }
    return remember(ret);
  }

  virtual int reuse(const bool switch_source) override
  {
    UNUSED(switch_source);
    int ret = OB_SUCCESS;
    if (is_reset_ || OB_ISNULL(scan_param_)) {
      ret = OB_NOT_INIT;
    } else {
      block_iter_.reset();
      max_score_ = 0.0;
      saved_error_ = OB_SUCCESS;
      prepared_ = false;
      exhausted_ = false;
    }
    return ret;
  }

  virtual void reset() override
  {
    block_iter_.reset();
    iter_param_.reset();
    scan_param_ = nullptr;
    max_score_ = 0.0;
    saved_error_ = OB_SUCCESS;
    prepared_ = false;
    exhausted_ = true;
    is_reset_ = true;
  }

  virtual void destroy() override
  {
    common::ObIAllocator *allocator = allocator_;
    reset();
    this->~ObSparseVectorBlockSource();
    if (OB_NOT_NULL(allocator)) {
      allocator->free(this);
    }
  }

private:
  int prepare()
  {
    int ret = OB_SUCCESS;
    if (prepared_) {
    } else if (is_reset_ || OB_ISNULL(scan_param_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(block_iter_.init(ranking_param_, iter_param_, *scan_param_))) {
      LOG_WARN("failed to initialize sparse vector block scan", K(ret));
    } else {
      max_score_ = 0.0;
      while (OB_SUCC(ret)) {
        const storage::ObMaxScoreTuple *tuple = nullptr;
        if (OB_FAIL(block_iter_.get_next(tuple))) {
        } else if (OB_ISNULL(tuple)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sparse vector block scan returned a null tuple", K(ret));
        } else {
          max_score_ = OB_MAX(max_score_, tuple->max_score_);
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        block_iter_.reset();
        if (OB_FAIL(block_iter_.init(ranking_param_, iter_param_, *scan_param_))) {
          LOG_WARN("failed to rewind sparse vector block scan", K(ret));
        } else {
          prepared_ = true;
          exhausted_ = false;
        }
      } else {
        LOG_WARN("failed to calculate sparse vector global block bound", K(ret));
      }
    }
    return ret;
  }

  int remember(const int error)
  {
    if (OB_SUCCESS != error && OB_ITER_END != error) {
      saved_error_ = error;
    }
    return error;
  }

private:
  common::ObIAllocator *allocator_;
  storage::ObTableScanParam *scan_param_;
  storage::ObBlockMaxScoreIterParam iter_param_;
  storage::ObBlockMaxIPRankingParam ranking_param_;
  storage::ObBlockMaxScoreIterator block_iter_;
  double max_score_;
  int saved_error_;
  bool prepared_;
  bool exhausted_;
  bool is_reset_;
  DISALLOW_COPY_AND_ASSIGN(ObSparseVectorBlockSource);
};

} // namespace

int create_sparse_vector_block_source(
    common::ObIAllocator &allocator,
    storage::ObTableScanParam &scan_param,
    const ObSparseVectorBlockSourceSpec &spec,
    ObISparseRetrievalBlockSource *&source)
{
  int ret = OB_SUCCESS;
  void *buffer = nullptr;
  ObSparseVectorBlockSource *block_source = nullptr;
  source = nullptr;
  if (!spec.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(buffer = allocator.alloc(sizeof(ObSparseVectorBlockSource)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(block_source = new (buffer) ObSparseVectorBlockSource())) {
  } else if (OB_FAIL(block_source->init(allocator, scan_param, spec))) {
    LOG_WARN("failed to initialize sparse vector block source", K(ret));
    block_source->~ObSparseVectorBlockSource();
    allocator.free(buffer);
  } else {
    source = block_source;
  }
  return ret;
}

} // namespace data_plane
} // namespace oceanbase
