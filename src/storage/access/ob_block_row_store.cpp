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

#define USING_LOG_PREFIX STORAGE
#include "ob_block_row_store.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
#include "storage/blocksstable/ob_micro_block_row_scanner.h"
#include "storage/truncate_info/ob_truncate_partition_filter.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace blocksstable;
namespace storage
{

ObBlockRowStore::ObBlockRowStore(ObTableAccessContext &context)
    : is_inited_(false),
      pd_filter_info_(),
      context_(context),
      iter_param_(nullptr),
      disabled_(false),
      is_aggregated_in_prefetch_(false),
      where_optimizer_(nullptr)
{}

ObBlockRowStore::~ObBlockRowStore()
{
}

void ObBlockRowStore::reset()
{
  is_inited_ = false;
  pd_filter_info_.reset();
  disabled_ = false;
  is_aggregated_in_prefetch_ = false;
  iter_param_ = nullptr;
  if (where_optimizer_ != nullptr) {
    where_optimizer_->~ObWhereOptimizer();
    context_.stmt_allocator_->free(where_optimizer_);
    where_optimizer_ = nullptr;
  }
}

void ObBlockRowStore::reuse()
{
  disabled_ = false;
  is_aggregated_in_prefetch_ = false;
}

int ObBlockRowStore::init(const ObTableAccessParam &param, common::hash::ObHashSet<int32_t> *agg_col_mask)
{
  UNUSED(agg_col_mask);
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObBlockRowStore init twice", K(ret));
  } else if (OB_ISNULL(context_.stmt_allocator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to init store pushdown filter", K(ret));
  } else if (OB_FAIL(pd_filter_info_.init(param.iter_param_, *context_.stmt_allocator_))) {
  } else if (nullptr != context_.sample_filter_ 
              && OB_FAIL(context_.sample_filter_->combine_to_filter_tree(pd_filter_info_.filter_))) {
      LOG_WARN("Failed to combine sample filter to filter tree", K(ret), K_(pd_filter_info), KP_(context_.sample_filter));
  } else if (nullptr != pd_filter_info_.filter_ && param.iter_param_.enable_pd_filter_reorder()) {
    if (OB_UNLIKELY(nullptr != where_optimizer_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected where optimizer", K(ret), KP_(where_optimizer));
    } else if (OB_ISNULL(where_optimizer_ = OB_NEWx(ObWhereOptimizer, context_.stmt_allocator_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc memory for ObWhereOptimizer", K(ret));
    } else if (OB_FAIL(where_optimizer_->init(&param.iter_param_, pd_filter_info_.filter_))) {
    }
  }
  if (OB_SUCC(ret)) {
    is_inited_ = true;
    iter_param_ = &param.iter_param_;
  } else {
    reset();
  }
  return ret;
}

int ObBlockRowStore::open(ObTableIterParam &iter_param)
{
  int ret = OB_SUCCESS;
  const bool need_padding = is_pad_char_to_full_length(context_.sql_mode_);
  bool filter_valid = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("Not init", K(ret));
  } else if (OB_UNLIKELY(!iter_param.is_valid() ||
        nullptr == iter_param.get_col_params() ||
        nullptr == iter_param.out_cols_project_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to init store pushdown filter", K(ret), K(iter_param));
  } else if (nullptr != context_.truncate_part_filter_
             && context_.truncate_part_filter_->need_combined_to_pd_filter()
             && OB_FAIL(context_.truncate_part_filter_->combine_to_filter_tree(pd_filter_info_.filter_))) {
    LOG_WARN("Failed to combine truncate filter to filter tree", K(ret), KP_(context_.truncate_part_filter));
  } else if (nullptr == pd_filter_info_.filter_) {
    // nothing to do
  } else if (OB_FAIL(pd_filter_info_.filter_->init_evaluated_datums(filter_valid))) {
  } else {
    if (OB_UNLIKELY(!filter_valid)) {
      iter_param.disable_pd_filter();
      pd_filter_info_.is_pd_filter_ = false;
    }
    if (OB_FAIL(iter_param.build_index_filter_for_row_store(context_.allocator_))) {
    } else if (OB_FAIL(pd_filter_info_.filter_->init_filter_param(
            *iter_param.get_col_params(), *iter_param.out_cols_project_, need_padding))) {
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(on_scan_start())) {
    LOG_WARN("failed to start block row store scan", K(ret));
  }
  return ret;
}

} // namespace storage

namespace sql
{
int PushdownFilterInfo::init(const storage::ObTableIterParam &iter_param,
                             common::ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  void *len_array_buf = nullptr;
  const int64_t out_col_cnt = iter_param.get_out_col_cnt();
  is_pd_filter_ = iter_param.enable_pd_filter();
  allocator_ = &alloc;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("Init twice", K(ret));
  } else if (OB_UNLIKELY(!iter_param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument to init store pushdown filter", K(ret), K(iter_param));
  } else if ((orig_filter_is_null_ = nullptr == iter_param.pushdown_filter_)) {
    // Nothing to allocate when Storage has no pushdown tree.
  } else if (OB_ISNULL(buf = alloc.alloc(
                 sizeof(blocksstable::ObStorageDatum) * out_col_cnt))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Fail to allocate memory for pushdown filter col buf", K(ret), K(out_col_cnt));
  } else if (FALSE_IT(datum_buf_ =
                 new (buf) blocksstable::ObStorageDatum[out_col_cnt]())) {
  } else if (OB_ISNULL(buf = alloc.alloc(
                 sizeof(blocksstable::ObStorageDatum) * out_col_cnt))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Fail to allocate memory for pushdown filter col buf", K(ret), K(out_col_cnt));
  } else if (FALSE_IT(tmp_datum_buf_ =
                 new (buf) blocksstable::ObStorageDatum[out_col_cnt]())) {
  } else {
    filter_ = iter_param.pushdown_filter_;
    col_capacity_ = out_col_cnt;
  }

  if (OB_SUCC(ret)
      && (iter_param.vectorized_enabled_ || iter_param.enable_pd_aggregate())) {
    batch_size_ = iter_param.vectorized_enabled_
        ? iter_param.op_->get_batch_size()
        : storage::AGGREGATE_STORE_BATCH_SIZE;
    if (OB_FAIL(col_datum_buf_.init(batch_size_, alloc))) {
    } else if (OB_ISNULL(buf = alloc.alloc(sizeof(char *) * batch_size_))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc cell data ptr", K(ret), K(batch_size_));
    } else if (FALSE_IT(cell_data_ptrs_ = reinterpret_cast<const char **>(buf))) {
    } else if (OB_ISNULL(skip_bit_ =
                 to_bit_vector(alloc.alloc(ObBitVector::memory_size(batch_size_))))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc skip bit", K(ret), K_(batch_size));
    } else if (OB_ISNULL(buf = alloc.alloc(sizeof(int32_t) * batch_size_))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc row_ids", K(ret), K(batch_size_));
    } else if (OB_ISNULL(len_array_buf = alloc.alloc(sizeof(uint32_t) * batch_size_))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc len_array_buf", K(ret), K_(batch_size));
    } else {
      skip_bit_->init(batch_size_);
      row_ids_ = reinterpret_cast<int32_t *>(buf);
      len_array_ = reinterpret_cast<uint32_t *>(len_array_buf);
    }
  }

  if (OB_FAIL(ret)) {
    reset();
  } else {
    is_inited_ = true;
  }
  return ret;
}
} // namespace sql
} // namespace oceanbase
