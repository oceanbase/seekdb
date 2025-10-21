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

#include "storage/direct_load/ob_direct_load_partition_merge_task.h"
#include "observer/table_load/ob_table_load_table_ctx.h"
#include "storage/direct_load/ob_direct_load_compare.h"
#include "storage/direct_load/ob_direct_load_conflict_check.h"
#include "storage/direct_load/ob_direct_load_dag_insert_table_row_iterator.h"
#include "storage/direct_load/ob_direct_load_data_fuse.h"
#include "storage/direct_load/ob_direct_load_data_insert.h"
#include "storage/direct_load/ob_direct_load_data_with_origin_query.h"
#include "storage/direct_load/ob_direct_load_external_table.h"
#include "storage/direct_load/ob_direct_load_insert_table_row_iterator.h"
#include "storage/direct_load/ob_direct_load_insert_table_row_writer.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
using namespace blocksstable;
using namespace share;
using namespace table;
using namespace observer;

/**
 * ObDirectLoadPartitionMergeTask
 */

ObDirectLoadPartitionMergeTask::ObDirectLoadPartitionMergeTask()
  : ctx_(nullptr),
    merge_param_(nullptr),
    merge_ctx_(nullptr),
    insert_tablet_ctx_(nullptr),
    parallel_idx_(-1),
    affected_rows_(0),
    need_handle_dml_row_(false),
    is_stop_(false),
    allocator_("TLD_MergeExec"),
    is_inited_(false)
{
  allocator_.set_tenant_id(MTL_ID());
  row_iters_.set_block_allocator(ModulePageAllocator(allocator_));
}

ObDirectLoadPartitionMergeTask::~ObDirectLoadPartitionMergeTask()
{
  for (int64_t i = 0; i < row_iters_.count(); ++i) {
    ObDirectLoadIStoreRowIterator *row_iter = row_iters_.at(i);
    if (row_iter != nullptr) {
      row_iter->~ObDirectLoadIStoreRowIterator();
      allocator_.free(row_iter);
      row_iter = nullptr;
    }
  }
  row_iters_.reset();
  allocator_.reset();
}

int ObDirectLoadPartitionMergeTask::inner_init(ObDirectLoadTabletMergeCtx *merge_ctx,
                                               int64_t parallel_idx,
                                               bool need_handle_dml_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == merge_ctx || !merge_ctx->is_valid() || parallel_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KPC(merge_ctx), K(parallel_idx));
  } else {
    ctx_ = merge_ctx->get_param()->ctx_;
    merge_param_ = merge_ctx->get_param();
    merge_ctx_ = merge_ctx;
    insert_tablet_ctx_ = merge_ctx->get_insert_tablet_ctx();
    parallel_idx_ = parallel_idx;
    need_handle_dml_row_ = need_handle_dml_row;
  }
  return ret;
}

int ObDirectLoadPartitionMergeTask::process()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadPartitionMergeTask not init", KR(ret), KP(this));
  } else {
    const ObTabletID &tablet_id = merge_ctx_->get_tablet_id();
    int64_t slice_id = 0;
    ObMacroDataSeq block_start_seq;
    ObArenaAllocator allocator("TLD_MergeExec");
    ObArray<ObDirectLoadIStoreRowIterator *> row_iters;
    allocator.set_tenant_id(MTL_ID());
    row_iters.set_block_allocator(ModulePageAllocator(allocator));
    ObDirectLoadMgrAgent ddl_agent;
    if (OB_FAIL(merge_param_->insert_table_ctx_->get_tablet_context(tablet_id, insert_tablet_ctx_))) {
      LOG_WARN("fail to get tablet context ", KR(ret), K(tablet_id));
    } else if (OB_FAIL(construct_row_iters(row_iters, allocator))) {
      LOG_WARN("fail to construct row iters", KR(ret));
    } else if (OB_FAIL(insert_tablet_ctx_->get_ddl_agent(ddl_agent))) {
      LOG_WARN("fail to init tmp agent", K(ret));
    } else if (!ddl_agent.is_inited()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl agent should not be null", K(ret));
    }
    // For full import, regardless of whether a partition has data or not, a ddl object needs to be created to create a major sstable for that partition
    else if (OB_FAIL(ObDDLUtil::init_macro_block_seq(parallel_idx_, block_start_seq))) {
      LOG_WARN("fail to set parallel degree", KR(ret), K(parallel_idx_));
    } else if (OB_FAIL(insert_tablet_ctx_->open_sstable_slice(block_start_seq, parallel_idx_/*slice_idx*/, slice_id, ddl_agent))) {
      LOG_WARN("fail to open sstable slice ", KR(ret), K(block_start_seq));
    } else if (row_iters.empty()) {
      // do nothing
      LOG_INFO("skip empty sstable slice", K(tablet_id), K(parallel_idx_), K(block_start_seq),
               K(slice_id));
    } else {
      const bool use_batch_mode = merge_param_->use_batch_mode_;
      LOG_INFO("add sstable slice begin", K(tablet_id), K(parallel_idx_), K(block_start_seq),
               K(slice_id), K(row_iters.count()), K(use_batch_mode));
      if (OB_UNLIKELY(is_stop_)) {
        ret = OB_CANCELED;
        LOG_WARN("merge task canceled", KR(ret));
      } else {
        // batch mode does not support writing insert and delete lines simultaneously
        if (use_batch_mode) {
          if (OB_FAIL(fill_sstable_slice_batch(slice_id, row_iters))) {
            LOG_WARN("fail to fill sstable slice batch", KR(ret), K(slice_id));
          }
        } else {
          if (OB_FAIL(fill_sstable_slice(slice_id, row_iters, ddl_agent))) {
            LOG_WARN("fail to fill sstable slice", KR(ret), K(slice_id), K(ddl_agent));
          }
        }
      }
      LOG_INFO("add sstable slice end", KR(ret), K(tablet_id), K(parallel_idx_), K(affected_rows_));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(insert_tablet_ctx_->close_sstable_slice(slice_id, parallel_idx_/*slice_idx*/, ddl_agent))) {
        LOG_WARN("fail to close writer", KR(ret));
      }
    }
    // release row iters
    for (int64_t i = 0; i < row_iters.count(); ++i) {
      ObDirectLoadIStoreRowIterator *row_iter = row_iters.at(i);
      row_iter->~ObDirectLoadIStoreRowIterator();
      allocator.free(row_iter);
    }
    row_iters.reset();
    allocator.reset();
  }
  return ret;
}

int ObDirectLoadPartitionMergeTask::fill_sstable_slice(
  const int64_t slice_id,
  const ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
  ObDirectLoadMgrAgent &ddl_agent)
{
  int ret = OB_SUCCESS;
  ObDirectLoadInsertTableRowIterator insert_table_row_iter;
  if (OB_FAIL(insert_table_row_iter.init(insert_tablet_ctx_,
                                         row_iters,
                                         need_handle_dml_row_ ? merge_param_->dml_row_handler_ : nullptr,
                                         ctx_->job_stat_))) {
    LOG_WARN("fail to init insert table row iter", KR(ret));
  } else if (OB_FAIL(insert_tablet_ctx_->fill_sstable_slice(slice_id, insert_table_row_iter,
                                                            ddl_agent, affected_rows_))) {
    LOG_WARN("fail to fill sstable slice", KR(ret));
  } else if (OB_FAIL(insert_table_row_iter.close())) {
    LOG_WARN("fail to close insert table row iter", KR(ret));
  }
  return ret;
}

int ObDirectLoadPartitionMergeTask::fill_sstable_slice_batch(
  const int64_t slice_id,
  const ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters)
{
  int ret = OB_SUCCESS;
  ObDirectLoadInsertTableBatchRowStoreWriter batch_writer;
  ObDirectLoadInsertTableRowInfo row_info;
  if (OB_FAIL(insert_tablet_ctx_->get_row_info(row_info))) {
    LOG_WARN("fail to get row info", KR(ret));
  } else if (OB_FAIL(batch_writer.init(insert_tablet_ctx_,
                                       row_info,
                                       slice_id,
                                       need_handle_dml_row_ ? merge_param_->dml_row_handler_ : nullptr,
                                       ctx_->job_stat_))) {
    LOG_WARN("fail to init buffer writer", KR(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < row_iters.count(); ++i) {
    ObDirectLoadIStoreRowIterator *row_iter = row_iters.at(i);
    if (OB_FAIL(batch_writer.write(row_iter))) {
      LOG_WARN("fail to write", KR(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(batch_writer.close())) {
    LOG_WARN("fail to close writer", KR(ret));
  } else {
    affected_rows_ = batch_writer.get_row_count();
  }
  return ret;
}

void ObDirectLoadPartitionMergeTask::stop()
{
  is_stop_ = true;
  if (OB_NOT_NULL(insert_tablet_ctx_)) {
    insert_tablet_ctx_->cancel();
  }
}

int ObDirectLoadPartitionMergeTask::init_iterator(ObITabletSliceRowIterator *&row_iterator)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadPartitionMergeTask not init", KR(ret), KP(this));
  } else {
    row_iterator = nullptr;
    ObDirectLoadDagInsertTableRowIterator *iter = nullptr;
    ObMemAttr attr(MTL_ID(), "TLD_SliceIter");
    if (OB_UNLIKELY(!row_iters_.empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected row iters not empty", KR(ret));
    } else if (OB_FAIL(construct_row_iters(row_iters_, allocator_))) {
      LOG_WARN("fail to construct row iters", KR(ret));
    } else if (OB_ISNULL(iter = OB_NEW(ObDirectLoadDagInsertTableRowIterator, attr))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", KR(ret));
    } else if (OB_FAIL(iter->init(insert_tablet_ctx_, parallel_idx_ /*slice_idx*/, row_iters_,
                                  need_handle_dml_row_ ? merge_param_->dml_row_handler_ : nullptr,
                                  ObDirectLoadMergeMode::NORMAL == merge_param_->merge_mode_))) {
      LOG_WARN("fail to init insert table row iter", KR(ret));
    } else {
      row_iterator = iter;
    }
    if (OB_FAIL(ret)) {
      OB_DELETE(ObDirectLoadDagInsertTableRowIterator, attr, iter);
    }
  }
  return ret;
}

/**
 * ObDirectLoadPartitionEmptyMergeTask
 */

int ObDirectLoadPartitionEmptyMergeTask::init(ObDirectLoadTabletMergeCtx *merge_ctx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionEmptyMergeTask init twice", KR(ret), KP(this));
  } else if (OB_FAIL(inner_init(merge_ctx, 0 /*parallel_idx*/))) {
    LOG_WARN("fail to inner init", KR(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

/**
 * ObDirectLoadPartitionOriginDataMergeTask
 */

ObDirectLoadPartitionOriginDataMergeTask::ObDirectLoadPartitionOriginDataMergeTask()
  : origin_table_(nullptr), range_(nullptr)
{
}

ObDirectLoadPartitionOriginDataMergeTask::~ObDirectLoadPartitionOriginDataMergeTask() {}

int ObDirectLoadPartitionOriginDataMergeTask::init(ObDirectLoadTabletMergeCtx *merge_ctx,
                                                   ObDirectLoadOriginTable &origin_table,
                                                   const ObDatumRange &range,
                                                   int64_t parallel_idx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionOriginDataMergeTask init twice", KR(ret), KP(this));
  } else if (OB_FAIL(inner_init(merge_ctx, parallel_idx))) {
    LOG_WARN("fail to inner init", KR(ret));
  } else if (OB_UNLIKELY(!origin_table.is_valid() || !range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(origin_table), K(range));
  } else {
    origin_table_ = &origin_table;
    range_ = &range;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionOriginDataMergeTask::construct_row_iters(
  ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
  ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  row_iters.reset();
  ObDirectLoadOriginTableScanner *origin_scanner = nullptr;
  if (OB_FAIL(origin_table_->scan(*range_, allocator, origin_scanner, false /*skip_read_lob*/))) {
    LOG_WARN("fail to scan origin table", KR(ret));
  } else if (OB_FAIL(row_iters.push_back(origin_scanner))) {
    LOG_WARN("fail to push back", KR(ret));
  }
  if (OB_FAIL(ret)) {
    if (nullptr != origin_scanner) {
      origin_scanner->~ObDirectLoadOriginTableScanner();
      allocator.free(origin_scanner);
      origin_scanner = nullptr;
    }
  }
  return ret;
}

/**
 * ObDirectLoadPartitionOriginDataUnrescanMergeTask
 */

ObDirectLoadPartitionOriginDataUnrescanMergeTask::ObDirectLoadPartitionOriginDataUnrescanMergeTask()
  : ctx_(nullptr),
    merge_param_(nullptr),
    merge_ctx_(nullptr),
    insert_tablet_ctx_(nullptr),
    origin_table_(nullptr),
    range_(nullptr),
    parallel_idx_(-1),
    affected_rows_(0),
    is_stop_(false),
    is_inited_(false)
{
}

ObDirectLoadPartitionOriginDataUnrescanMergeTask::~ObDirectLoadPartitionOriginDataUnrescanMergeTask()
{
}

int ObDirectLoadPartitionOriginDataUnrescanMergeTask::init(
    ObDirectLoadTabletMergeCtx *merge_ctx,
    ObDirectLoadOriginTable &origin_table,
    const ObDatumRange &range,
    int64_t parallel_idx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionOriginDataUnrescanMergeTask init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(nullptr == merge_ctx || !merge_ctx->is_valid() ||
                         !origin_table.is_valid() || !range.is_valid() || parallel_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KPC(merge_ctx), K(origin_table), K(range), K(parallel_idx));
  } else {
    ctx_ = merge_ctx->get_param()->ctx_;
    merge_param_ = merge_ctx->get_param();
    merge_ctx_ = merge_ctx;
    insert_tablet_ctx_ = merge_ctx->get_insert_tablet_ctx();
    origin_table_ = &origin_table;
    range_ = &range;
    parallel_idx_ = parallel_idx;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionOriginDataUnrescanMergeTask::process()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDirectLoadPartitionOriginDataUnrescanMergeTask not init", KR(ret), KP(this));
  } else {
    const ObTabletID &tablet_id = merge_ctx_->get_tablet_id();
    ObDirectLoadInsertTableBatchRowDirectWriter direct_writer;
    ObDirectLoadInsertTableRowInfo row_info;
    ObArenaAllocator allocator("TLD_UODMerge");
    allocator.set_tenant_id(MTL_ID());
    ObDirectLoadOriginTableScanner *row_iter = nullptr;
    if (OB_FAIL(origin_table_->scan(*range_, allocator, row_iter, false /*skip_read_lob*/))) {
      LOG_WARN("fail to scan origin table", KR(ret));
    } else if (OB_FAIL(insert_tablet_ctx_->get_row_info(row_info))) {
      LOG_WARN("fail to get row info", KR(ret));
    } else if (OB_FAIL(direct_writer.init(insert_tablet_ctx_,
                                          row_info,
                                          merge_param_->dml_row_handler_,
                                          nullptr,
                                          ctx_->job_stat_))) {
      LOG_WARN("fail to init direct writer", KR(ret));
    } else {
      LOG_INFO("add sstable slice begin", K(tablet_id), K(parallel_idx_));
      ObDirectLoadDatumRow datum_row;
      ObDirectLoadRowFlag row_flag;
      row_flag.uncontain_hidden_pk_ = true;
      const ObDirectLoadDatumRow *datum_row_ptr = nullptr;
      while (OB_SUCC(ret)) {
        if (OB_UNLIKELY(is_stop_)) {
          ret = OB_CANCELED;
          LOG_WARN("merge task canceled", KR(ret));
        } else if (OB_FAIL(row_iter->get_next_row(datum_row_ptr))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("fail to get next row", KR(ret));
          } else {
            ret = OB_SUCCESS;
            break;
          }
        } else {
          datum_row.storage_datums_ = datum_row_ptr->storage_datums_ + 1;
          datum_row.count_ = datum_row_ptr->count_ - 1;
          if (OB_FAIL(direct_writer.append_row(datum_row, row_flag))) {
            LOG_WARN("fail to append row", KR(ret), K(datum_row));
          } else {
            affected_rows_++;
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(direct_writer.close())) {
          LOG_WARN("fail to close direct writer", KR(ret));
        }
      }
      LOG_INFO("add sstable slice end", KR(ret), K(tablet_id), K(parallel_idx_), K(affected_rows_));
    }
    if (row_iter != nullptr) {
      row_iter->~ObDirectLoadOriginTableScanner();
      allocator.free(row_iter);
      row_iter = nullptr;
    }
    allocator.reset();
  }
  return ret;
}

void ObDirectLoadPartitionOriginDataUnrescanMergeTask::stop()
{
  is_stop_ = true;
  if (OB_NOT_NULL(insert_tablet_ctx_)) {
    insert_tablet_ctx_->cancel();
  }
}

/**
 * ObDirectLoadPartitionRangeMultipleMergeTask
 */

ObDirectLoadPartitionRangeMultipleMergeTask::ObDirectLoadPartitionRangeMultipleMergeTask()
  : origin_table_(nullptr), range_(nullptr)
{
}

ObDirectLoadPartitionRangeMultipleMergeTask::~ObDirectLoadPartitionRangeMultipleMergeTask() {}

int ObDirectLoadPartitionRangeMultipleMergeTask::init(
  ObDirectLoadTabletMergeCtx *merge_ctx,
  ObDirectLoadOriginTable &origin_table,
  const ObDirectLoadTableDataDesc &table_data_desc,
  const ObDirectLoadTableHandleArray &sstable_array,
  const ObDatumRange &range,
  int64_t parallel_idx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionRangeMultipleMergeTask init twice", KR(ret), KP(this));
  } else if (OB_FAIL(inner_init(merge_ctx, parallel_idx))) {
    LOG_WARN("fail to inner init", KR(ret));
  } else if (OB_UNLIKELY(!table_data_desc.is_valid() || sstable_array.empty() ||
                         !range.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_data_desc), K(sstable_array), K(range));
  } else if (OB_FAIL(sstable_array_.assign(sstable_array))) {
    LOG_WARN("fail to assign sstable_array", KR(ret));
  } else {
    origin_table_ = &origin_table;
    table_data_desc_ = table_data_desc;
    range_ = &range;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionRangeMultipleMergeTask::construct_row_iters(
  ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
  ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  row_iters.reset();
  ObDirectLoadIStoreRowIterator *data_iter = nullptr;
  if (merge_ctx_->merge_with_origin_data()) {
    ObDirectLoadDataFuseParam data_fuse_param;
    data_fuse_param.tablet_id_ = merge_ctx_->get_tablet_id();
    data_fuse_param.table_data_desc_ = table_data_desc_;
    data_fuse_param.datum_utils_ = merge_param_->datum_utils_;
    data_fuse_param.dml_row_handler_ = merge_param_->dml_row_handler_;
    ObDirectLoadMultipleSSTableDataFuse *data_fuse = nullptr;
    if (OB_ISNULL(data_iter = data_fuse =
                    OB_NEWx(ObDirectLoadMultipleSSTableDataFuse, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to new ObDirectLoadMultipleSSTableDataFuse", KR(ret));
    } else if (OB_FAIL(data_fuse->init(data_fuse_param, origin_table_, sstable_array_, *range_))) {
      LOG_WARN("fail to init data fuse", KR(ret));
    }
  } else if (merge_ctx_->merge_with_origin_query_for_data()) {
    ObDirectLoadDataWithOriginQueryParam data_param;
    data_param.tablet_id_ = merge_ctx_->get_tablet_id();
    data_param.origin_table_ = origin_table_;
    data_param.rowkey_count_ = merge_param_->rowkey_column_num_;
    data_param.store_column_count_ = merge_param_->column_count_;
    data_param.table_data_desc_ = table_data_desc_;
    data_param.col_descs_ = merge_param_->col_descs_;
    data_param.datum_utils_ = merge_param_->datum_utils_;
    data_param.dml_row_handler_ = merge_param_->dml_row_handler_;
    ObDirectLoadMultipleSSTableDataWithOriginQuery *data_query = nullptr;
    if (OB_ISNULL(data_iter = data_query =
                    OB_NEWx(ObDirectLoadMultipleSSTableDataWithOriginQuery, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory", K(ret));
    } else if (OB_FAIL(data_query->init(data_param, sstable_array_, *range_))) {
      LOG_WARN("fail to init ObDirectLoadMultipleSSTableDataWithOriginQuery", K(ret));
    }
  } else if (merge_ctx_->merge_with_conflict_check()) {
    ObDirectLoadConflictCheckParam conflict_check_param;
    conflict_check_param.tablet_id_ = merge_ctx_->get_tablet_id();
    conflict_check_param.table_data_desc_ = table_data_desc_;
    conflict_check_param.origin_table_ = origin_table_;
    conflict_check_param.range_ = range_;
    conflict_check_param.col_descs_ = merge_param_->col_descs_;
    conflict_check_param.datum_utils_ = merge_param_->datum_utils_;
    conflict_check_param.dml_row_handler_ = merge_param_->dml_row_handler_;
    ObDirectLoadMultipleSSTableConflictCheck *conflict_check = nullptr;
    if (OB_ISNULL(data_iter = conflict_check =
                           OB_NEWx(ObDirectLoadMultipleSSTableConflictCheck, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObDirectLoadMultipleSSTableConflictCheck", KR(ret));
    } else if (OB_FAIL(conflict_check->init(conflict_check_param, sstable_array_))) {
      LOG_WARN("fail to init conflict check", KR(ret));
    }
  } else {
    ObDirectLoadDataInsertParam data_insert_param;
    data_insert_param.tablet_id_ = merge_ctx_->get_tablet_id();
    data_insert_param.table_data_desc_ = table_data_desc_;
    data_insert_param.datum_utils_ = merge_param_->datum_utils_;
    data_insert_param.dml_row_handler_ = merge_param_->dml_row_handler_;
    ObDirectLoadMultipleSSTableDataInsert *data_insert = nullptr;
    if (OB_ISNULL(data_iter = data_insert =
                    OB_NEWx(ObDirectLoadMultipleSSTableDataInsert, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to new ObDirectLoadMultipleSSTableDataInsert", KR(ret));
    } else if (OB_FAIL(data_insert->init(data_insert_param, sstable_array_, *range_))) {
      LOG_WARN("fail to init data insert", KR(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(row_iters.push_back(data_iter))) {
      LOG_WARN("fail to push back", KR(ret));
    }
  }
  if (OB_FAIL(ret)) {
    if (nullptr != data_iter) {
      data_iter->~ObDirectLoadIStoreRowIterator();
      allocator.free(data_iter);
      data_iter = nullptr;
    }
  }
  return ret;
}

/**
 * ObDirectLoadPartitionHeapTableMergeTask
 */

ObDirectLoadPartitionHeapTableMergeTask::RowIterator::RowIterator()
  : pk_interval_(nullptr), is_inited_(false)
{
}

ObDirectLoadPartitionHeapTableMergeTask::RowIterator::~RowIterator() {}

int ObDirectLoadPartitionHeapTableMergeTask::RowIterator::init(
  const ObDirectLoadTableHandle &external_table,
  const ObDirectLoadTableDataDesc &table_data_desc,
  ObTabletCacheInterval &pk_interval)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("RowIterator init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!external_table.is_valid() ||
                         !external_table.get_table()->is_external_table() ||
                         !table_data_desc.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(external_table), K(table_data_desc));
  } else {
    ObDirectLoadExternalTable *table =
      static_cast<ObDirectLoadExternalTable *>(external_table.get_table());
    if (OB_FAIL(scanner_.init(table_data_desc.external_data_block_size_,
                              table_data_desc.compressor_type_, table->get_fragments()))) {
      LOG_WARN("fail to init fragment scanner", KR(ret));
    } else if (OB_FAIL(datum_row_.init(table_data_desc.column_count_))) {
      LOG_WARN("fail to init datum row", KR(ret));
    } else {
      pk_interval_ = &pk_interval;
      // set parent params
      row_flag_.uncontain_hidden_pk_ = true;
      row_flag_.has_delete_row_ = false;
      column_count_ = table_data_desc.column_count_;
      is_inited_ = true;
    }
  }
  return ret;
}

int ObDirectLoadPartitionHeapTableMergeTask::RowIterator::get_next_row(
  const ObDirectLoadDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  datum_row = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("RowIterator not init", KR(ret), KP(this));
  } else {
    const ObDirectLoadExternalRow *external_row = nullptr;
    if (OB_FAIL(scanner_.get_next_item(external_row))) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to get next item", KR(ret));
      }
    } else if (OB_FAIL(external_row->to_datum_row(datum_row_))) {
      LOG_WARN("fail to transfer datum row", KR(ret));
    } else {
      datum_row = &datum_row_;
    }
  }
  return ret;
}

ObDirectLoadPartitionHeapTableMergeTask::ObDirectLoadPartitionHeapTableMergeTask() {}

ObDirectLoadPartitionHeapTableMergeTask::~ObDirectLoadPartitionHeapTableMergeTask() {}


int ObDirectLoadPartitionHeapTableMergeTask::construct_row_iters(
  ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
  ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  row_iters.reset();
  RowIterator *row_iter = nullptr;
  if (OB_ISNULL(row_iter = OB_NEWx(RowIterator, &allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new RowIterator", KR(ret));
  } else if (OB_FAIL(row_iter->init(external_table_, table_data_desc_, pk_interval_))) {
    LOG_WARN("fail to init row iter", KR(ret));
  } else if (OB_FAIL(row_iters.push_back(row_iter))) {
    LOG_WARN("fail to push back", KR(ret));
  }
  if (OB_FAIL(ret)) {
    if (nullptr != row_iter) {
      row_iter->~RowIterator();
      allocator.free(row_iter);
      row_iter = nullptr;
    }
  }
  return ret;
}

/**
 * ObDirectLoadPartitionHeapTableMultipleMergeTask
 */

ObDirectLoadPartitionHeapTableMultipleMergeTask::RowIterator::RowIterator()
  : heap_table_array_(nullptr), pk_interval_(nullptr), pos_(-1), is_inited_(false)
{
}

ObDirectLoadPartitionHeapTableMultipleMergeTask::RowIterator::~RowIterator() {}

int ObDirectLoadPartitionHeapTableMultipleMergeTask::RowIterator::init(
  const ObDirectLoadTableHandleArray &heap_table_array,
  const ObTabletID &tablet_id,
  const ObDirectLoadTableDataDesc &table_data_desc,
  ObTabletCacheInterval &pk_interval)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("RowIterator init twice", KR(ret), KP(this));
  } else if (OB_UNLIKELY(heap_table_array.empty() || !tablet_id.is_valid() ||
                         !table_data_desc.is_valid() || 0 == pk_interval.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(heap_table_array), K(tablet_id), K(table_data_desc),
             K(pk_interval));
  } else if (OB_FAIL(datum_row_.init(table_data_desc.column_count_))) {
    LOG_WARN("fail to init datum row", KR(ret));
  } else {
    heap_table_array_ = &heap_table_array;
    tablet_id_ = tablet_id;
    table_data_desc_ = table_data_desc;
    pk_interval_ = &pk_interval;
    // set parent params
    row_flag_.uncontain_hidden_pk_ = true;
    row_flag_.has_delete_row_ = false;
    column_count_ = table_data_desc.column_count_;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionHeapTableMultipleMergeTask::RowIterator::get_next_row(
  const ObDirectLoadDatumRow *&datum_row)
{
  int ret = OB_SUCCESS;
  datum_row = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("RowIterator not init", KR(ret), KP(this));
  } else {
    const ObDirectLoadMultipleExternalRow *external_row = nullptr;
    if (pos_ < 0 && OB_FAIL(switch_next_heap_table())) {
      if (OB_UNLIKELY(OB_ITER_END != ret)) {
        LOG_WARN("fail to switch next heap table", KR(ret));
      }
    }
    while (OB_SUCC(ret) && datum_row == nullptr) {
      if (OB_FAIL(scanner_.get_next_row(external_row))) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to get next row", KR(ret));
        } else {
          // switch next heap table
          ret = OB_SUCCESS;
          if (OB_FAIL(switch_next_heap_table())) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("fail to switch next heap table", KR(ret));
            }
          }
        }
      } else if (OB_FAIL(external_row->to_datum_row(datum_row_))) {
        LOG_WARN("fail to transfer datum row", KR(ret));
      } else {
        datum_row = &datum_row_;
      }
    }
  }
  return ret;
}

int ObDirectLoadPartitionHeapTableMultipleMergeTask::RowIterator::switch_next_heap_table()
{
  int ret = OB_SUCCESS;
  ++pos_;
  if (pos_ >= heap_table_array_->count()) {
    ret = OB_ITER_END;
  } else {
    ObDirectLoadTableHandle heap_table;
    // restructure scanner
    scanner_.~ObDirectLoadMultipleHeapTableTabletWholeScanner();
    new (&scanner_) ObDirectLoadMultipleHeapTableTabletWholeScanner();
    if (OB_FAIL(heap_table_array_->get_table(pos_, heap_table))) {
      LOG_WARN("fail to get table", KR(ret), K(pos_));
    } else if (OB_FAIL(scanner_.init(heap_table, tablet_id_, table_data_desc_))) {
      LOG_WARN("fail to init scanner", KR(ret));
    }
  }
  return ret;
}

ObDirectLoadPartitionHeapTableMultipleMergeTask::ObDirectLoadPartitionHeapTableMultipleMergeTask()
{
}

ObDirectLoadPartitionHeapTableMultipleMergeTask::~ObDirectLoadPartitionHeapTableMultipleMergeTask()
{
}

int ObDirectLoadPartitionHeapTableMultipleMergeTask::init(
  ObDirectLoadTabletMergeCtx *merge_ctx,
  const ObDirectLoadTableDataDesc &table_data_desc,
  const ObDirectLoadTableHandle &heap_table,
  const ObTabletCacheInterval &pk_interval,
  int64_t parallel_idx)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionHeapTableMultipleMergeTask init twice", KR(ret), KP(this));
  } else if (OB_FAIL(inner_init(merge_ctx, parallel_idx, true /*need_handle_dml_row*/))) {
    LOG_WARN("fail to inner init", KR(ret));
  } else if (OB_UNLIKELY(!table_data_desc.is_valid() || !heap_table.is_valid() ||
                         0 == pk_interval.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_data_desc), K(heap_table), K(pk_interval));
  } else if (OB_FAIL(heap_table_array_.add(heap_table))) {
    LOG_WARN("fail to add table", KR(ret));
  } else {
    table_data_desc_ = table_data_desc;
    pk_interval_ = pk_interval;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionHeapTableMultipleMergeTask::init(
  ObDirectLoadTabletMergeCtx *merge_ctx,
  const ObDirectLoadTableDataDesc &table_data_desc,
  const ObDirectLoadTableHandleArray &heap_table_array,
  const share::ObTabletCacheInterval &pk_interval)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionHeapTableMultipleMergeTask init twice", KR(ret), KP(this));
  } else if (OB_FAIL(inner_init(merge_ctx, 0 /*parallel_idx*/, true /*need_handle_dml_row*/))) {
    LOG_WARN("fail to inner init", KR(ret));
  } else if (OB_UNLIKELY(!table_data_desc.is_valid() || heap_table_array.empty() ||
                         0 == pk_interval.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(table_data_desc), K(heap_table_array), K(pk_interval));
  } else if (OB_FAIL(heap_table_array_.assign(heap_table_array))) {
    LOG_WARN("fail to assign tables", KR(ret));
  } else {
    table_data_desc_ = table_data_desc;
    pk_interval_ = pk_interval;
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionHeapTableMultipleMergeTask::construct_row_iters(
  ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
  ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  row_iters.reset();
  RowIterator *row_iter = nullptr;
  if (OB_ISNULL(row_iter = OB_NEWx(RowIterator, &allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to new RowIterator", KR(ret));
  } else if (OB_FAIL(row_iter->init(heap_table_array_,
                                    merge_ctx_->get_tablet_id(),
                                    table_data_desc_,
                                    pk_interval_))) {
    LOG_WARN("fail to init row iter", KR(ret));
  } else if (OB_FAIL(row_iters.push_back(row_iter))) {
    LOG_WARN("fail to push back", KR(ret));
  }
  if (OB_FAIL(ret)) {
    if (nullptr != row_iter) {
      row_iter->~RowIterator();
      allocator.free(row_iter);
      row_iter = nullptr;
    }
  }
  return ret;
}

/**
 * ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask
 */

ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask::
  ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask()
  : origin_table_(nullptr)
{
}

ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask::
  ~ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask()
{
}

int ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask::init(
  ObDirectLoadTabletMergeCtx *merge_ctx,
  ObDirectLoadOriginTable &origin_table,
  const ObDirectLoadTableDataDesc &table_data_desc,
  const ObDirectLoadTableHandleArray &heap_table_array,
  const ObTabletCacheInterval &pk_interval)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask init twice", KR(ret),
             KP(this));
  } else if (OB_FAIL(inner_init(merge_ctx, 0 /*parallel_idx*/, true /*need_handle_dml_row*/))) {
    LOG_WARN("fail to inner init", KR(ret));
  } else if (OB_UNLIKELY(!origin_table.is_valid() || !table_data_desc.is_valid() ||
                         heap_table_array.empty() || 0 == pk_interval.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(origin_table), K(table_data_desc), K(heap_table_array),
             K(pk_interval));
  } else if (OB_FAIL(heap_table_array_.assign(heap_table_array))) {
    LOG_WARN("fail to assign heap table array", KR(ret));
  } else {
    origin_table_ = &origin_table;
    table_data_desc_ = table_data_desc;
    pk_interval_ = pk_interval;
    whole_range_.set_whole_range();
    is_inited_ = true;
  }
  return ret;
}

int ObDirectLoadPartitionHeapTableMultipleAggregateMergeTask::construct_row_iters(
  ObIArray<ObDirectLoadIStoreRowIterator *> &row_iters,
  ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  row_iters.reset();
  // 1. construct origin iter
  ObDirectLoadOriginTableScanner *origin_scanner = nullptr;
  if (OB_FAIL(origin_table_->scan(whole_range_, allocator, origin_scanner, false /*skip_read_lob*/))) {
    LOG_WARN("fail to scan origin table", KR(ret));
  } else if (OB_FAIL(row_iters.push_back(origin_scanner))) {
    LOG_WARN("fail to push back", KR(ret));
  }
  if (OB_FAIL(ret)) {
    if (nullptr != origin_scanner) {
      origin_scanner->~ObDirectLoadOriginTableScanner();
      allocator.free(origin_scanner);
      origin_scanner = nullptr;
    }
  }
  // 2. construct multiple_heap_table iter
  if (OB_SUCC(ret)) {
    RowIterator *row_iter = nullptr;
    if (OB_ISNULL(row_iter = OB_NEWx(RowIterator, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new RowIterator", KR(ret));
    } else if (OB_FAIL(row_iter->init(heap_table_array_,
                                      merge_ctx_->get_tablet_id(),
                                      table_data_desc_,
                                      pk_interval_))) {
      LOG_WARN("fail to init row iter", KR(ret));
    } else if (OB_FAIL(row_iters.push_back(row_iter))) {
      LOG_WARN("fail to push back", KR(ret));
    }
    if (OB_FAIL(ret)) {
      if (nullptr != row_iter) {
        row_iter->~RowIterator();
        allocator.free(row_iter);
        row_iter = nullptr;
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
