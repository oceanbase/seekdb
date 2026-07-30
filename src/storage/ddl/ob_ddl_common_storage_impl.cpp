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
// this file was  share/ob_ddl_common.cpp created by function-level splitting from:these ObDDLUtil static methods
// implementation depends on this module,callers are all in upper layers;declaration remains in share/ob_ddl_common.h。
#define USING_LOG_PREFIX SHARE
#include "share/rc/ob_module_provider.h"

#include "storage/ob_tablet_autoinc_seq_service.h"
#include "share/ob_ddl_common.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/ddl/ob_ddl_independent_dag.h"  // ObDDLIndependentDag complete type
#include "share/ob_rpc_struct.h"
#include "share/ob_ddl_checksum.h"
#include "share/ob_ddl_sim_point.h"
#include "common/object/ob_object.h"
#include "share/tablet/ob_tablet_table_operator.h"
#include "share/storage/ob_tablet_local_checksum_table_storage.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_ddl_merge_task.h"
#include "storage/ddl/ob_ddl_macro_block_writer.h"
#include "storage/ddl/ob_lob_macro_block_writer.h"
#include "sql/engine/vector/ob_continuous_base.h"
#include "sql/engine/vector/ob_discrete_format.h"
#include "sql/engine/vector/ob_fixed_length_base.h"
#include "sql/engine/vector/ob_uniform_base.h"
#include "sql/engine/vector/type_traits.h"

#include "sql/das/ob_das_utils.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_tablet_slice_writer.h"
#include "storage/ddl/ob_ddl_batch_rows.h"
#include "storage/tablet/ob_tablet_obj_load_helper.h"
#include "storage/tablet/ob_tablet.h"
#include "lib/worker.h"
#include "storage/ddl/ob_ddl_write_stat_util.h"
#include "share/ob_ddl_error_message_table_operator.h"

using namespace oceanbase::share;
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

namespace
{
constexpr int64_t MACRO_STEP_SIZE = 0x1 << 25;
}

// lob-column handling free function(moved together from share/ob_ddl_common.cpp;must be defined before use)
OB_INLINE int check_lob_column_inrow(
    char *ptr,
    uint32_t len,
    const int64_t lob_inrow_threshold,
    bool &is_inrow)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == ptr || len <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected lob column is empty data", K(ret), KP(ptr), K(len));
  } else {
    oceanbase::common::ObLobLocatorV2 locator(ptr, len, true /*has_lob_header*/);
    is_inrow = (locator.is_inrow_disk_lob_locator() && (len - sizeof(ObLobCommon) <= lob_inrow_threshold));
  }
  return ret;
}
OB_INLINE int check_skip_handle_lob_column(
    const ObDatum &datum,
    const int64_t lob_inrow_threshold,
    bool &can_skip)
{
  int ret = OB_SUCCESS;
  can_skip = true;
  if (datum.is_null() || datum.is_nop()) {
  } else if (OB_FAIL(check_lob_column_inrow(const_cast<char *>(datum.ptr_), datum.len_, lob_inrow_threshold, can_skip))) {
    LOG_WARN("fail to check lob can skip", K(ret), K(lob_inrow_threshold), K(datum));
  }
  return ret;
}

int check_skip_handle_lob_column(
    ObIVector *vector,
    const int64_t row_count,
    const int64_t lob_inrow_threshold,
    bool &can_skip)
{
  int ret = OB_SUCCESS;
  can_skip = true;
  const VectorFormat format = vector->get_format();
  switch (format) {
    case VEC_CONTINUOUS: {
      ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
      ObBitVector *nulls = continuous_vec->get_nulls();
      char *data = continuous_vec->get_data();
      uint32_t *offsets = continuous_vec->get_offsets();
      if (!nulls->is_all_true(row_count)) {
        for (int64_t j = 0; OB_SUCC(ret) && can_skip && j < row_count; ++j) {
          if (!nulls->at(j)) {
            if (OB_FAIL(check_lob_column_inrow(data + offsets[j],
                                               offsets[j + 1] - offsets[j],
                                               lob_inrow_threshold,
                                               can_skip))) {
              LOG_WARN("fail to check lob column inrow", K(ret), K(j), KP(data), K(offsets[j]), K(offsets[j + 1]));
            }
          }
        }
      }
      break;
    }
    case VEC_DISCRETE: {
      ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
      ObBitVector *nulls = discrete_vec->get_nulls();
      char **ptrs = discrete_vec->get_ptrs();
      int32_t *lens = discrete_vec->get_lens();
      if (!nulls->is_all_true(row_count)) {
        for (int64_t j = 0; OB_SUCC(ret) && can_skip && j < row_count; ++j) {
          if (!nulls->at(j)) {
            if (OB_FAIL(check_lob_column_inrow(ptrs[j], lens[j], lob_inrow_threshold, can_skip))) {
              LOG_WARN("fail to check lob column inrow", K(ret), K(j), KP(ptrs[j]), K(lens[j]));
            }
          }
        }
      }
      break;
    }
    case VEC_UNIFORM: {
      ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
      ObDatum *datums = uniform_vec->get_datums();
      for (int64_t j = 0; OB_SUCC(ret) && can_skip && j < row_count; ++j) {
        const ObDatum &datum = datums[j];
        if (!datum.is_null()) {
          if (OB_FAIL(check_lob_column_inrow(const_cast<char *>(datum.ptr_),
                                             datum.len_,
                                             lob_inrow_threshold,
                                             can_skip))) {
            LOG_WARN("fail to check lob column inrow", K(ret), K(j), K(datum));
          }
        }
      }
      break;
    }
    case VEC_UNIFORM_CONST: {
      ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
      const ObDatum &datum = uniform_vec->get_datums()[0];
      if (!datum.is_null()) {
        if (OB_FAIL(check_lob_column_inrow(const_cast<char *>(datum.ptr_),
                                           datum.len_,
                                           lob_inrow_threshold,
                                           can_skip))) {
          LOG_WARN("fail to check lob column inrow", K(ret), K(datum));
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected vector format in lob column", K(ret), K(format));
      break;
    }
  }
  return ret;
}

int ObDDLUtil::report_ddl_checksum_from_major_sstable(
      const ObTabletID &tablet_id,
      const uint64_t target_table_id,
      const int64_t execution_id,
      const int64_t ddl_task_id,
      const int64_t data_format_version)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObLSService *ls_service = share::g_mp->ls_service();
  ObTabletHandle tablet_handle;
  if (OB_UNLIKELY(!tablet_id.is_valid() || OB_INVALID_ID == target_table_id || execution_id < 0 || ddl_task_id < 0 || data_format_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(target_table_id), K(execution_id), K(ddl_task_id), K(data_format_version));
  } else if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", K(ret));
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
    LOG_WARN("get local ls failed", K(ret));
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local ls is null", K(ret));
  } else if (OB_FAIL(ObDDLUtil::ddl_get_tablet(ls, tablet_id, tablet_handle))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id));
  } else {
    ObSSTable *first_major_sstable = nullptr;
    ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
    if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
      LOG_WARN("fetch table store failed", K(ret));
    } else if (OB_ISNULL(first_major_sstable = static_cast<ObSSTable *>(table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(false/*first*/)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("no major after wait merge success", K(ret), K(tablet_id));
    } else if (OB_FAIL(report_ddl_sstable_checksum(tablet_id, target_table_id, execution_id, ddl_task_id, data_format_version, tablet_handle, first_major_sstable))) {
      LOG_WARN("report ddl sstable checksum failed", K(ret), K(tablet_id), K(target_table_id), K(execution_id), K(ddl_task_id), K(data_format_version));
    }
  }
  return ret;
}

int ObDDLUtil::report_ddl_sstable_checksum(
      const ObTabletID &tablet_id,
      const uint64_t target_table_id,
      const int64_t execution_id,
      const int64_t ddl_task_id,
      const int64_t data_format_version,
      ObTabletHandle &tablet_handle,
      ObSSTable *first_major_sstable)
{
  int ret = OB_SUCCESS;
  ObSSTableMetaHandle sst_meta_hdl;
  if (OB_UNLIKELY(!tablet_id.is_valid() || OB_INVALID_ID == target_table_id ||
                   execution_id < 0 || ddl_task_id < 0 || data_format_version < 0 || nullptr == first_major_sstable ||
                   !tablet_handle.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(target_table_id), K(execution_id), K(ddl_task_id), K(data_format_version), KPC(first_major_sstable), K(tablet_handle));
  } else if (OB_FAIL(first_major_sstable->get_meta(sst_meta_hdl))) {
    LOG_WARN("fail to get sstable meta handle", K(ret));
  } else {
    const int64_t *column_checksums = sst_meta_hdl.get_sstable_meta().get_col_checksum();
    int64_t column_count = sst_meta_hdl.get_sstable_meta().get_col_checksum_cnt();
    for (int64_t retry_cnt = 10; retry_cnt > 0; retry_cnt--) { // overwrite ret
      if (OB_FAIL(ObTabletDDLUtil::report_ddl_checksum(tablet_id,
                                                      target_table_id,
                                                      execution_id,
                                                      ddl_task_id,
                                                      column_checksums,
                                                      column_count,
                                                      data_format_version))) {
        LOG_WARN("report ddl column checksum failed", K(ret), K(tablet_id), K(ddl_task_id));
      } else {
        break;
      }
    }
    ob_usleep(100L * 1000L);
  }
  return ret;
}

int ObDDLUtil::init_macro_block_writer(
    const ObWriteMacroParam &param,
    ObIAllocator &allocator,
    ObDDLMacroBlockWriter *&macro_block_writer)
{
  int ret = OB_SUCCESS;
  macro_block_writer = nullptr;
  ObMacroDataSeq start_seq;
  const int64_t row_offset = 0;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else if (OB_ISNULL(param.tablet_param_.storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("storage schema is null", K(ret), K(param));
  } else if (OB_FAIL(ObDDLStorageUtil::init_macro_block_seq(param.slice_idx_,
                                                     start_seq))) {
    LOG_WARN("init start seq failed", K(ret), K(param.direct_load_type_),
                                      K(param.tablet_id_), K(param.slice_idx_));
  } else {
    ObITable::TableKey table_key;
    table_key.tablet_id_ = param.tablet_id_;
    table_key.version_range_.snapshot_version_ = param.snapshot_version_;
    table_key.table_type_ = ObITable::MAJOR_SSTABLE;
    if (OB_ISNULL(macro_block_writer = OB_NEWx(ObDDLMacroBlockWriter, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to allocate memory", K(ret));
    } else if (OB_FAIL(macro_block_writer->init(param, table_key, start_seq, row_offset))) {
      LOG_WARN("fail to initialize macro block writer", K(ret), K(table_key));
    }
    if (OB_FAIL(ret)) {
      OB_DELETEx(ObDDLMacroBlockWriter, &allocator, macro_block_writer);
    }
  }
  return ret;
}

int ObDDLUtil::prepare_lob_writer(const ObTabletID &tablet_id, const int64_t slice_idx, const ObWriteMacroParam &param, ObLobMacroBlockWriter *&lob_writer)
{
  int ret = OB_SUCCESS;
  if (nullptr == lob_writer) {
    if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0 || !param.is_valid())) {
      LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx), K(param));
    } else if (OB_ISNULL(lob_writer = OB_NEW(ObLobMacroBlockWriter, ObMemAttr("lob_writer")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else {
      ObMacroDataSeq start_seq;
      if (OB_FAIL(ObDDLStorageUtil::init_macro_block_seq(slice_idx,
                                                  start_seq))) {
        LOG_WARN("init start seq failed", K(ret), K(param.direct_load_type_),
                                          K(tablet_id), K(slice_idx));
      } else if (OB_FAIL(lob_writer->init(param, tablet_id, start_seq))) {
        LOG_WARN("init lob writer failed", K(ret), K(tablet_id), K(param), K(start_seq));
      }
    }
  }
  return ret;
}

int ObDDLUtil::handle_lob_columns(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    ObWriteMacroParam &param,
    ObLobMacroBlockWriter *&lob_writer,
    ObArenaAllocator &allocator,
    ObBatchDatumRows &batch_rows)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() ||
                  slice_idx < 0 ||
                  !param.is_valid() ||
                  batch_rows.row_count_ <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx), K(param), K(batch_rows.row_count_));
  } else if (param.ddl_table_schema_.table_item_.is_skip_lob()) {
  } else {
    const ObDDLTableSchema &ddl_table_schema = param.ddl_table_schema_;
    const int64_t row_count = batch_rows.row_count_;
    const int64_t lob_inrow_threshold = ddl_table_schema.table_item_.lob_inrow_threshold_;
    ObBatchSelector selector(static_cast<int64_t>(0), batch_rows.row_count_);
    ObArray<std::pair<char **, uint32_t *>> lob_cells;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_table_schema.lob_column_idxs_.count(); i++) {
      const int64_t idx = ddl_table_schema.lob_column_idxs_.at(i);
      if (OB_UNLIKELY(idx < 0 || idx >= batch_rows.vectors_.count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid column index", K(ret), K(idx), K(batch_rows.vectors_.count()));
      } else {
        ObIVector *&vector = batch_rows.vectors_.at(idx);
        bool can_skip = true;
        selector.rescan();
        if (OB_FAIL(check_skip_handle_lob_column(vector, row_count, lob_inrow_threshold, can_skip))) {
          LOG_WARN("fail to check skip handle lob column", K(ret));
        } else if (!can_skip) {
          const ObColumnSchemaItem &column_schema_item = ddl_table_schema.column_items_.at(idx);
          if (OB_FAIL(ObDDLStorageUtil::handle_lob_column(tablet_id,
                                                   slice_idx,
                                                   param,
                                                   false, // need_all_cells
                                                   lob_cells,
                                                   allocator,
                                                   column_schema_item,
                                                   selector,
                                                   vector))) {
            LOG_WARN("fail to check skip handle lob column", K(ret));
          } else if (lob_cells.count() > 0) {
            if (OB_FAIL(prepare_lob_writer(tablet_id, slice_idx, param, lob_writer))) {
              LOG_WARN("prepare lob writer failed", K(ret), K(tablet_id), K(slice_idx), K(param));
            } else if (OB_ISNULL(lob_writer)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("lob writer is null", K(ret), KP(lob_writer));
            }
            ObStorageDatum temp_datum;
            for (int64_t i = 0; OB_SUCC(ret) && i < lob_cells.count(); ++i) {
              std::pair<char **, uint32_t *> &cur_cell = lob_cells.at(i);
              if (OB_UNLIKELY(nullptr == cur_cell.first || nullptr == cur_cell.second)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("current cell is null", K(ret), K(i), K(cur_cell.first), K(cur_cell.second));
              } else {
                temp_datum.ptr_ = *cur_cell.first;
                temp_datum.pack_ = *cur_cell.second;
                if (OB_UNLIKELY(temp_datum.is_null() || temp_datum.is_nop())) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("temp datum should not be null or nop", K(ret));
                } else if (OB_FAIL(lob_writer->write(column_schema_item, allocator, temp_datum))) {
                  LOG_WARN("fill lob into macro block failed", K(ret));
                } else {
                  *cur_cell.first = const_cast<char *>(temp_datum.ptr_);
                  *cur_cell.second = temp_datum.len_;
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::convert_to_storage_row(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    const ObWriteMacroParam &param,
    ObLobMacroBlockWriter *&lob_writer,
    ObArenaAllocator &row_arena,
    blocksstable::ObDatumRow &current_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0 || !param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx), K(param));
  } else {
    row_arena.reuse();
  }

  // fill lob columns
  if (OB_SUCC(ret) && !param.ddl_table_schema_.table_item_.is_skip_lob()) {
    const ObDDLTableSchema &ddl_table_schema = param.ddl_table_schema_;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_table_schema.lob_column_idxs_.count(); i++) {
      const int64_t idx = ddl_table_schema.lob_column_idxs_.at(i);
      if (OB_UNLIKELY(idx < 0 || idx >= current_row.get_column_count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid column index", K(ret), K(idx), K(current_row.get_column_count()));
      } else {
        ObStorageDatum &datum = current_row.storage_datums_[idx];
        const ObColumnSchemaItem &column_schema_item = ddl_table_schema.column_items_.at(idx);
        if (datum.is_null() || datum.is_nop()) {
          // skip
        } else {
          if (nullptr == lob_writer) {
            if (OB_ISNULL(lob_writer = OB_NEW(ObLobMacroBlockWriter, ObMemAttr("lob_writer")))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("allocate memory failed", K(ret));
            } else {
              ObMacroDataSeq start_seq;
              if (OB_FAIL(ObDDLStorageUtil::init_macro_block_seq(slice_idx,
                                                          start_seq))) {
                LOG_WARN("init start seq failed", K(ret), K(param.direct_load_type_),
                                                  K(tablet_id), K(slice_idx));
              } else if (OB_FAIL(lob_writer->init(param, tablet_id, start_seq))) {
                LOG_WARN("init lob writer failed", K(ret), K(param));
              }
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(lob_writer)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("lob writer is null", K(ret), KP(lob_writer));
          } else if (OB_FAIL(lob_writer->write(column_schema_item, row_arena, datum))) {
            LOG_WARN("fill lob into macro block failed", K(ret), K(idx), K(tablet_id));
          }
        }
      }
    }
  }

  // reshape necessary columns
  if (OB_SUCC(ret)) {
    const ObDDLTableSchema &ddl_table_schema = param.ddl_table_schema_;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_table_schema.reshape_column_idxs_.count(); ++i) {
      const int64_t idx = ddl_table_schema.reshape_column_idxs_.at(i);
      if (idx < 0 || idx >= current_row.get_column_count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid column index", K(ret), K(idx), K(current_row.get_column_count()));
      } else {
        ObStorageDatum &datum = current_row.storage_datums_[idx];
        const bool need_reshape = !datum.is_null() && !datum.is_nop();
        const ObColumnSchemaItem &column_item = ddl_table_schema.column_items_.at(idx);
        if (need_reshape && OB_FAIL(ObDASUtils::reshape_datum_value(column_item.col_type_,
                                                                    column_item.col_accuracy_,
                                                                    row_arena,
                                                                    datum))) {
          LOG_WARN("reshape storage datum failed", K(ret), K(column_item), K(datum));
        }
      }
    }
  }

  // check row
  if (OB_SUCC(ret)) {
    const ObDDLTableSchema &ddl_table_schema = param.ddl_table_schema_;
    if (OB_FAIL(ObDDLStorageUtil::check_null_and_length(ddl_table_schema.table_item_.is_index_table_,
                                                 ddl_table_schema.table_item_.has_lob_rowkey_,
                                                 ddl_table_schema.table_item_.rowkey_column_num_,
                                                 current_row))) {
      LOG_WARN("fail to check rowkey null value and length in row", KR(ret), K(current_row));
    }
  }
  return ret;
}

int ObDDLUtil::get_task_ranges(
    const int64_t task_id,
    const common::ObTabletID &tablet_id,
    const int64_t tablet_size,
    const int64_t hint_parallelism,
    common::ObArenaAllocator &allocator,
    ObArray<blocksstable::ObDatumRange> &report_ranges)
{
  int ret = OB_SUCCESS;
  ObFreezeInfo frozen_status;
  const bool allow_not_ready = false;
  ObLS *ls = nullptr;
  ObTabletTableIterator iterator;
  ObLSTabletService *tablet_service = nullptr;
  if (OB_UNLIKELY(task_id <= 0 || !tablet_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(task_id), K(tablet_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("fail to get log stream", K(ret));
  } else if (OB_ISNULL(tablet_service = ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet service is nullptr", K(ret));
  } else if (OB_FAIL(DDL_SIM(task_id, COMPLEMENT_DATA_TASK_SPLIT_RANGE_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(task_id));
  } else {
    int64_t total_size = 0;
    int64_t expected_task_count = 0;
    ObStoreRange range;
    range.set_whole_range();
    ObSEArray<common::ObStoreRange, 32> ranges;
    ObArrayArray<ObStoreRange> multi_range_split_array;
    ObParallelBlockRangeTaskParams params;
    params.parallelism_ = hint_parallelism;
    params.expected_task_load_ = tablet_size / 1024 <= 0 ? sql::OB_EXPECTED_TASK_LOAD : tablet_size / 1024;
    if (OB_FAIL(ranges.push_back(range))) {
      LOG_WARN("push back range failed", K(ret));
    } else if (OB_FAIL(tablet_service->get_multi_ranges_cost(tablet_id,
                                                            ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
                                                            ranges,
                                                            total_size))) {
      LOG_WARN("get multi ranges cost failed", K(ret));
      if (OB_REPLICA_NOT_READABLE == ret) {
        ret = OB_EAGAIN;
      }
    } else if (OB_FALSE_IT(total_size = total_size / 1024 /* Byte -> KB */)) {
    } else if (OB_FAIL(ObGranuleUtil::compute_total_task_count(params,
                                                              total_size,
                                                              expected_task_count))) {
      LOG_WARN("compute total task count failed", K(ret));
    } else if (OB_FAIL(tablet_service->split_multi_ranges(tablet_id,
                                                          ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
                                                          ranges,
                                                          min(min(max(expected_task_count, 1), hint_parallelism), ObMacroDataSeq::MAX_PARALLEL_IDX + 1),
                                                          allocator,
                                                          multi_range_split_array))) {
      LOG_WARN("split multi ranges failed", K(ret));
      if (OB_REPLICA_NOT_READABLE == ret) {
        ret = OB_EAGAIN;
      }
    } else if (multi_range_split_array.count() <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected range split arr", K(ret), K(total_size), K(hint_parallelism),
        K(expected_task_count), K(params), K(multi_range_split_array));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < multi_range_split_array.count(); i++) {
        ObIArray<ObStoreRange> &storage_task_ranges = multi_range_split_array.at(i);
        for (int64_t j = 0; OB_SUCC(ret) && j < storage_task_ranges.count(); j++) {
          const ObStoreRange &store_range = storage_task_ranges.at(j);
          blocksstable::ObDatumRange datum_range;
          if (OB_FAIL(datum_range.from_range(store_range, allocator))) {
            LOG_WARN("failed to transfer datum range", K(ret), K(store_range));
          } else if (OB_FAIL(report_ranges.push_back(datum_range))) {
            LOG_WARN("push back failed", K(ret));
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      FLOG_INFO("succeed to get range", K(ret), K(task_id), K(tablet_id), K(total_size),
      K(hint_parallelism), K(expected_task_count), K(params), K(multi_range_split_array), K(report_ranges));
    }
  }
  return ret;
}

int ObDDLUtil::get_tablet_physical_row_cnt(
  const ObTabletID &tablet_id,
  const bool calc_sstable,
  const bool calc_memtable,
  int64_t &physical_row_count /*OUT*/)
{
  int ret = OB_SUCCESS;

  // get total rows of the table; physical
  // src_tablet_id -> tablet -> sstables -> sstable_metas -> row_count
  //                         -> memtables -> physical_row_cnt
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObTablet *tablet = nullptr;
  ObTableStoreIterator table_store_iter;

  physical_row_count = 0;

  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(tablet_id));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(ls))) {
    LOG_WARN("get ls failed", K(ret));
  } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_10_S, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
    LOG_WARN("fail to get tablet", K(ret), K(tablet_id));
  } else if (OB_UNLIKELY(!tablet_handle.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpecter error", K(ret), K(tablet_handle));
  } else if (OB_ISNULL(tablet = tablet_handle.get_obj())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet is nullptr", K(ret), K(tablet_handle));
  } else if (OB_FAIL(tablet->get_all_tables(table_store_iter))) {
    LOG_WARN("get all tables failed", K(ret));
  } else if (!table_store_iter.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table_store_iter is invalid", K(ret), K(table_store_iter), KPC(tablet));
  } else {
    table_store_iter.resume();
    while (OB_SUCC(ret)) {
      ObITable *table = nullptr;
      ObSSTable *sstable = nullptr;
      memtable::ObMemtable* memtable = nullptr;
      ObSSTableMetaHandle sstable_meta_hdl;
      if (OB_FAIL(table_store_iter.get_next(table))) {
        if (OB_UNLIKELY(OB_ITER_END == ret)) {
          ret = OB_SUCCESS;
          break;
        } else {
          LOG_WARN("get next table failed", K(ret));
        }
      } else if (OB_UNLIKELY(OB_ISNULL(table))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected err", K(ret), KPC(table));
      } else if (calc_sstable && table->is_sstable()) {
        if (OB_FALSE_IT(sstable = static_cast<ObSSTable*>(table))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sstable static_cast failed", K(ret), KPC(table));
        } else if (OB_ISNULL(sstable) || !sstable->is_valid()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("the sstable is null or invalid", K(ret));
        } else if (OB_FAIL(sstable->get_meta(sstable_meta_hdl))) {
          LOG_WARN("get sstable meta failed", K(ret), KPC(sstable));
        } else {
          physical_row_count += sstable_meta_hdl.get_sstable_meta().get_row_count();
        }
      } else if (calc_memtable && table->is_memtable()) {
        if (OB_FALSE_IT(memtable = static_cast<memtable::ObMemtable*>(table))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("memtable static_cast failed", K(ret), KPC(table));
        } else if (OB_ISNULL(memtable)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get memtable meta failed", K(ret), KPC(memtable));
        } else {
          physical_row_count += memtable->get_physical_row_cnt();
        }
      }
    } // end while
  }
  if (OB_FAIL(ret)) {
    physical_row_count = 0;
  }
  return ret;
}

int ObDDLUtil::is_major_exist(const common::ObTabletID &tablet_id, bool &is_major_exist)
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  ObTabletHandle tablet_handle;
  ObLSService* ls_svr = share::g_mp->ls_service();
  is_major_exist = false;
  if (!tablet_id.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", K(ret), K(tablet_id));
  } else if (OB_ISNULL(ls_svr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service should not be null", K(ret));
  } else if (OB_FAIL(ls_svr->get_ls(ls))) {
    LOG_WARN("failed to get ls", K(ret));
  } else if (OB_FAIL(ddl_get_tablet(ls, tablet_id, tablet_handle))) {
    LOG_WARN("failed to get tablet id", K(ret), K(tablet_id));
  } else {
    is_major_exist = tablet_handle.get_obj()->get_major_table_count() > 0
                  || tablet_handle.get_obj()->get_tablet_meta().table_store_flag_.with_major_sstable();
  }
  return ret;
}


int ObDDLUtil::handle_lob_columns(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    ObWriteMacroParam &param,
    ObLobMacroBlockWriter *&lob_writer,
    ObArenaAllocator &allocator,
    ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() ||
                  slice_idx < 0 ||
                  !param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(tablet_id), K(slice_idx), K(param));
  } else if (param.ddl_table_schema_.table_item_.is_skip_lob()) {
  } else {
    const ObDDLTableSchema &ddl_table_schema = param.ddl_table_schema_;
    const int64_t lob_inrow_threshold = ddl_table_schema.table_item_.lob_inrow_threshold_;
    for (int64_t i = 0; OB_SUCC(ret) && i < ddl_table_schema.lob_column_idxs_.count(); i++) {
      const int64_t idx = ddl_table_schema.lob_column_idxs_.at(i);
      if (OB_UNLIKELY(idx < 0 || idx >= datum_row.get_column_count())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid column index", K(ret), K(idx), K(datum_row.get_column_count()));
      } else {
        bool can_skip = true;
        ObStorageDatum &datum = datum_row.storage_datums_[idx];
        const ObColumnSchemaItem &column_schema_item = ddl_table_schema.column_items_.at(idx);
        if (OB_FAIL(check_skip_handle_lob_column(datum, lob_inrow_threshold, can_skip))) {
          LOG_WARN("fail to check skip handle lob column", K(ret));
        } else if (!can_skip) {
          if (nullptr == lob_writer) {
            if (OB_ISNULL(lob_writer = OB_NEW(ObLobMacroBlockWriter, ObMemAttr("lob_writer")))) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("allocate memory failed", K(ret));
            } else {
              ObMacroDataSeq start_seq;
              if (OB_FAIL(ObDDLStorageUtil::init_macro_block_seq(slice_idx,
                                                          start_seq))) {
                LOG_WARN("init start seq failed", K(ret), K(param.direct_load_type_),
                                                  K(tablet_id), K(slice_idx));
              } else if (OB_FAIL(lob_writer->init(param, tablet_id, start_seq))) {
                LOG_WARN("init lob writer failed", K(ret), K(param));
              }
            }
          }
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(lob_writer)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("lob writer is null", K(ret), KP(lob_writer));
          } else if (OB_FAIL(lob_writer->write(column_schema_item, allocator, datum))) {
            LOG_WARN("fill lob into macro block failed", K(ret), K(idx), K(tablet_id));
          }
        }
      }
    }
  }
  return ret;
}

int ObDDLUtil::fill_writer_param(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    ObDDLIndependentDag *dag,
    const int64_t max_batch_size,
    ObWriteMacroParam &param)
{
  int ret = OB_SUCCESS;
  ObDDLTabletContext *tablet_context = nullptr;
  if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0 || nullptr == dag)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(tablet_id), K(slice_idx), KP(dag));
  } else if (OB_FAIL(dag->get_tablet_context(tablet_id, tablet_context))) {
    LOG_WARN("get ddl tablet context", K(ret), K(tablet_id), K(slice_idx));
  } else {
    const ObDDLTaskParam &ddl_task_param = dag->get_ddl_task_param();
    param.tablet_id_ = tablet_id;
    param.lob_meta_tablet_id_ = tablet_context->lob_meta_tablet_id_;
    param.data_format_version_ = ddl_task_param.data_format_version_;
    param.schema_version_ = ddl_task_param.schema_version_;
    param.slice_idx_ = slice_idx;
    param.slice_count_ = tablet_context->slice_count_;
    param.ddl_thread_count_ = dag->get_ddl_thread_count();
    param.snapshot_version_ = ddl_task_param.snapshot_version_;
    param.direct_load_type_ = dag->get_direct_load_type();
    param.task_id_ = ddl_task_param.ddl_task_id_;
    param.tablet_param_ = tablet_context->tablet_param_;
    param.lob_meta_tablet_param_ = tablet_context->lob_meta_tablet_param_;
    param.is_index_table_ = dag->get_ddl_table_schema().table_item_.is_index_table_;
    param.ddl_dag_ = dag;
    param.tablet_context_ = tablet_context;
    param.max_batch_size_ = max_batch_size;
    if (OB_FAIL(param.ddl_table_schema_.assign(dag->get_ddl_table_schema()))) {
      LOG_WARN("get ddl table schema failed", K(ret));
    }
  }
  return ret;
}

int ObDDLUtil::init_batch_rows(
    const ObDDLTableSchema &ddl_table_schema,
    const int64_t batch_size,
    ObDDLBatchRows &batch_rows)
{
  int ret = OB_SUCCESS;
  batch_rows.reset();
  if (OB_UNLIKELY(ddl_table_schema.column_items_.empty() || batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_table_schema), K(batch_size));
  } else {
    ObArray<ObColumnSchemaItem> sql_column_items;
    const int64_t sql_column_count = ddl_table_schema.column_items_.count() - ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt();
    const int64_t rowkey_column_count = ddl_table_schema.table_item_.rowkey_column_num_;
    const ObIArray<ObColumnSchemaItem> &storage_column_items = ddl_table_schema.column_items_;
    if (OB_FAIL(sql_column_items.reserve(sql_column_count))) {
      LOG_WARN("reserve sql column item array failed", K(ret), K(sql_column_count));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < storage_column_items.count(); ++i) {
      if (i >= rowkey_column_count && i < rowkey_column_count + ObMultiVersionRowkeyHelpper::get_extra_rowkey_col_cnt()) {
        // skip multi version column
      } else if (OB_FAIL(sql_column_items.push_back(storage_column_items.at(i)))) {
        LOG_WARN("push back column schema item failed", K(ret), K(i));
      }
    }
    if (OB_SUCC(ret)) {
      ObDDLRowFlag default_row_flag;
      if (OB_FAIL(batch_rows.init(sql_column_items, batch_size, default_row_flag))) {
        LOG_WARN("batch rows init failed", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLUtil::ddl_get_tablet(
    ObLS *ls,
    const ObTabletID &tablet_id,
    storage::ObTabletHandle &tablet_handle,
    storage::ObMDSGetTabletMode mode)
{
  int ret = OB_SUCCESS;
  const int64_t DDL_GET_TABLET_RETRY_TIMEOUT = 30 * 1000 * 1000; // 30s
  const int64_t timeout_ts = ObTimeUtility::current_time() + DDL_GET_TABLET_RETRY_TIMEOUT;
  if (OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ls", K(ret), KP(ls), K(tablet_id));
  } else if (OB_FAIL(ls->get_tablet_svr()->get_tablet_with_timeout(tablet_id,
                                                                  tablet_handle,
                                                                  timeout_ts,
                                                                  mode))) {
    LOG_WARN("fail to get tablet handle", K(ret), K(tablet_id));
    if (OB_ALLOCATE_MEMORY_FAILED == ret) {
      ret = OB_TIMEOUT;
    }
  }
  return ret;
}

int ObDDLUtil::alloc_storage_macro_block_writer(
    const ObWriteMacroParam &param,
    ObIAllocator &allocator,
    ObITabletSliceWriter *&tablet_slice_writer)
{
  int ret = OB_SUCCESS;
  tablet_slice_writer = nullptr;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the are invalid arguments", K(ret), K(param));
  } else {
    tablet_slice_writer = OB_NEWx(ObTabletSliceWriter, &allocator);
  }
  if (OB_UNLIKELY(nullptr == tablet_slice_writer)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory for the storage macro block writer", K(ret));
  } else if (OB_FAIL(tablet_slice_writer->init(param))) {
    LOG_WARN("fail to initialize storage tablet slice writer", K(ret), K(param));
  }
  if (OB_FAIL(ret) && nullptr != tablet_slice_writer) {
    tablet_slice_writer->~ObITabletSliceWriter();
    allocator.free(tablet_slice_writer);
    tablet_slice_writer = nullptr;
  }
  return ret;
}

int oceanbase::storage::ObDDLStorageWriteUtil::get_ddl_write_stat(
    const ObWriteMacroParam &param,
    const ObITable::TableKey &table_key,
    ObDDLWriteStat *&ddl_write_stat)
{
  int ret = OB_SUCCESS;
  ddl_write_stat = nullptr;
  if (OB_UNLIKELY(!param.is_valid() || OB_ISNULL(param.tablet_context_) || !table_key.is_valid() )) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_key), K(param), KP(param.tablet_context_));
  } else if (param.lob_meta_tablet_id_ == table_key.tablet_id_) {
    bool need_write_stat = ObITable::MAJOR_SSTABLE == table_key.table_type_;
    if (need_write_stat) {
      ddl_write_stat = &param.tablet_context_->lob_write_stat_;
    }
  } else if (param.tablet_id_ == table_key.tablet_id_) {
    bool need_write_stat = ObITable::MAJOR_SSTABLE == table_key.table_type_;
    if (need_write_stat) {
      ddl_write_stat = &param.tablet_context_->write_stat_;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpcted tablet id", K(ret), K(table_key), K(param));
  }
  return ret;
}

// ===== definition moved from share/ob_ddl_common.cpp: accesses blocksstable::ObDatumRow/ObMacroDataSeq members =====
// check_null_and_length moved to ob_ddl_common_storage_impl.cpp end of file (ObDDLStorageUtil)

// init_datum_row_with_snapshot moved to ob_ddl_common_storage_impl.cpp end of file (ObDDLStorageUtil)

// init_macro_block_seq moved to ob_ddl_common_storage_impl.cpp end of file (ObDDLStorageUtil)

// get_parallel_idx moved to ob_ddl_common_storage_impl.cpp end of file (ObDDLStorageUtil)

// ===== storage-clean static methods from ObDDLUtil demoted to storage::ObDDLStorageUtil members (A-set member-split cleanup)=====
#include "storage/ddl/ob_ddl_storage_util.h"
int ObDDLUtil::set_tablet_autoinc_seq(const ObTabletID &tablet_id, const int64_t seq_value)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid() || seq_value < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(seq_value));
  } else {
    ObSEArray<ObTabletAutoincSeqCopyParam, 1> params;
    ObTabletAutoincSeqCopyParam tablet_autoinc_param;
    tablet_autoinc_param.src_tablet_id_ = tablet_id;
    tablet_autoinc_param.dest_tablet_id_ = tablet_id;
    tablet_autoinc_param.autoinc_seq_ = seq_value;
    if (OB_FAIL(params.push_back(tablet_autoinc_param))) {
      LOG_WARN("push back tablet autoinc param failed", K(ret), K(tablet_autoinc_param));
    } else if (OB_FAIL(ObTabletAutoincSeqService::get_instance().batch_set_tablet_autoinc_seq(
        params))) {
      LOG_WARN("set tablet auto inc seq failed", K(ret));
    } else if (1 != params.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected sync tablet autoinc result", K(ret), K(params));
    } else if (OB_FAIL(params.at(0).ret_code_)) {
      LOG_WARN("sync tablet autoinc failed", K(ret), K(params.at(0)));
    }
  }
  return ret;
}

namespace oceanbase
{
namespace storage
{
int ObDDLStorageUtil::extract_index_key(const ObTableSchema &index_schema,
    const blocksstable::ObDatumRowkey &index_key, char *buffer, const int64_t buffer_len)
{
  int ret = OB_SUCCESS;
  if (!index_schema.is_valid() || !index_key.is_valid() || OB_ISNULL(buffer) || buffer_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(index_schema), K(index_key), KP(buffer), K(buffer_len));
  } else {
    const int64_t index_size = index_schema.get_index_column_num();
    int64_t pos = 0;
    MEMSET(buffer, 0, buffer_len);
    for (int64_t i = 0; OB_SUCC(ret) && i < index_size; i++) {
      const ObRowkeyColumn *column = index_schema.get_index_info().get_column(i);
      if (OB_ISNULL(column)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Failed to get index column description", K(i), K(ret));
      } else if (IS_SHADOW_COLUMN(column->column_id_)) {
        break;
      } else {
        const blocksstable::ObStorageDatum &datum = index_key.get_datum(i);
        ObObj obj;
        if (OB_FAIL(datum.to_obj(obj, column->get_meta_type()))) {
          LOG_WARN("convert datum to obj failed", K(ret));
        } else if (OB_FAIL(obj.print_plain_str_literal(buffer, buffer_len, pos))) {
          LOG_WARN("fail to print_plain_str_literal", K(ret), KP(buffer));
        } else if (OB_FAIL(databuff_printf(buffer,  buffer_len, pos, "-"))) {
          LOG_WARN("databuff print failed", K(ret));
        }
      }
    }
    if (OB_SUCC(ret) && pos > 0) {
      buffer[pos - 1] = '\0'; // overwrite the tail '-'
    }
    if (OB_SIZE_OVERFLOW == ret) {
      buffer[buffer_len - 1] = '\0';
      LOG_WARN("the index key length is larger than OB_TMP_BUF_SIZE_256", K(index_key), KP(buffer));
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

// file-local helper needed by handle_lob_column(this helper has copies in several DDL .cpp files)
static int new_discrete_vector(VecValueTypeClass value_tc,
                               const int64_t max_batch_size,
                               ObIAllocator &allocator,
                               ObDiscreteBase *&result_vec)
{
  int ret = OB_SUCCESS;
  result_vec = nullptr;
  ObIVector *vector = nullptr;
  switch (value_tc) {
#define DISCRETE_VECTOR_INIT_SWITCH(value_tc)                           \
  case value_tc: {                                                      \
    using VecType = RTVectorType<VEC_DISCRETE, value_tc>;               \
    static_assert(sizeof(VecType) <= ObIVector::MAX_VECTOR_STRUCT_SIZE, \
                  "vector size exceeds MAX_VECTOR_STRUCT_SIZE");        \
    vector = OB_NEWx(VecType, &allocator, nullptr, nullptr, nullptr);   \
    break;                                                              \
  }
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_NUMBER);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_EXTEND);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_STRING);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ENUM_SET_INNER);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_RAW);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_ROWID);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_LOB);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_JSON);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_GEO);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_UDT);
    DISCRETE_VECTOR_INIT_SWITCH(VEC_TC_COLLECTION);
#undef DISCRETE_VECTOR_INIT_SWITCH
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected discrete vector value type class", KR(ret), K(value_tc));
      break;
  }
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(vector)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc vecttor", KR(ret));
  } else {
    ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
    const int64_t nulls_size = ObBitVector::memory_size(max_batch_size);
    const int64_t lens_size = sizeof(int32_t) * max_batch_size;
    const int64_t ptrs_size = sizeof(char *) * max_batch_size;
    ObBitVector *nulls = nullptr;
    int32_t *lens = nullptr;
    char **ptrs = nullptr;
    if (OB_ISNULL(nulls = to_bit_vector(allocator.alloc(nulls_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc mem", KR(ret), K(nulls_size));
    } else if (OB_ISNULL(lens = static_cast<int32_t *>(allocator.alloc(lens_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc mem", KR(ret), K(lens_size));
    } else if (OB_ISNULL(ptrs = static_cast<char **>(allocator.alloc(ptrs_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc mem", KR(ret), K(ptrs_size));
    } else {
      nulls->reset(max_batch_size);
      discrete_vec->set_nulls(nulls);
      discrete_vec->set_lens(lens);
      discrete_vec->set_ptrs(ptrs);
      result_vec = discrete_vec;
    }
  }
  return ret;
}


int ObDDLStorageUtil::check_null_and_length(
    const bool is_index_table,
    const bool has_lob_rowkey,
    const int64_t rowkey_column_num,
    const blocksstable::ObDatumRow &row_val)
{
  int ret = OB_SUCCESS;
  if (is_index_table && !has_lob_rowkey) {
    // index table is index-organized but can have null values in index column
  } else if (OB_UNLIKELY(rowkey_column_num > row_val.get_column_count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid rowkey column number", KR(ret), K(rowkey_column_num), K(row_val));
  } else {
    int64_t rowkey_length = 0;
    bool has_null = false;
    for (int64_t i = 0; i < rowkey_column_num; i++) {
      const ObStorageDatum &cell = row_val.storage_datums_[i];
      rowkey_length += cell.len_;
      has_null |= cell.is_null();
    }
    if (!is_index_table && has_null) {
      ret = OB_ER_INVALID_USE_OF_NULL;
      LOG_WARN("invalid null cell for row key column", KR(ret), K(row_val));
    }
    if (OB_SUCC(ret) && has_lob_rowkey && rowkey_length > OB_MAX_VARCHAR_LENGTH_KEY) {
      ret = OB_ERR_TOO_LONG_KEY_LENGTH;
      LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
      STORAGE_LOG(WARN, "rowkey is too long", K(ret), K(rowkey_length), K(rowkey_column_num), K(row_val));
    }
  }
  return ret;
}

int ObDDLStorageUtil::init_datum_row_with_snapshot(
    const int64_t request_column_count,
    const int64_t rowkey_column_count,
    const int64_t snapshot_version,
    blocksstable::ObDatumRow &datum_row)
{
  int ret = OB_SUCCESS;
  datum_row.reset();
  if (OB_UNLIKELY(request_column_count <= 0 || rowkey_column_count <= 0 || request_column_count < rowkey_column_count || snapshot_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(request_column_count), K(rowkey_column_count), K(snapshot_version));
  } else if (OB_FAIL(datum_row.init(request_column_count))) {
    LOG_WARN("init datum row failed", K(ret), K(request_column_count));
  } else {
    datum_row.storage_datums_[rowkey_column_count].set_int(-snapshot_version);
    datum_row.storage_datums_[rowkey_column_count + 1].set_int(0);
    datum_row.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
  }
  return ret;
}

int ObDDLStorageUtil::init_macro_block_seq(const int64_t parallel_idx, blocksstable::ObMacroDataSeq &start_seq)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(parallel_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(parallel_idx));
  } else {
    start_seq.macro_data_seq_ = parallel_idx * MACRO_STEP_SIZE;
  }
  return ret;
}

int64_t ObDDLStorageUtil::get_parallel_idx(const blocksstable::ObMacroDataSeq &start_seq)
{
  int64_t parallel_idx = start_seq.get_parallel_idx();
  parallel_idx = start_seq.macro_data_seq_ / MACRO_STEP_SIZE;
  return parallel_idx;
}

int ObDDLStorageUtil::check_null_and_length(
    const bool is_index_table,
    const bool has_lob_rowkey,
    const int64_t rowkey_column_num,
    ObBatchDatumRows &batch_rows)
{
  int ret = OB_SUCCESS;
  if (is_index_table && !has_lob_rowkey) {
    // index table is index-organized but can have null values in index column
  } else if (OB_UNLIKELY(batch_rows.row_count_ <= 0 ||
                         batch_rows.vectors_.count() < rowkey_column_num)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(rowkey_column_num), K(batch_rows));
  } else {
    int64_t *rowkey_len = nullptr;
    int64_t row_count = batch_rows.row_count_;
    if (OB_ISNULL(rowkey_len = static_cast<int64_t *>(ob_malloc(sizeof(int64_t) * row_count, "DDL_CheckRK")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret), K(rowkey_len));
    } else {
      memset(rowkey_len, 0, sizeof(int64_t) * row_count);
      for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < rowkey_column_num; col_idx++) {
        bool has_null = false;
        const ObIVector *vector = batch_rows.vectors_[col_idx];
        const VectorFormat format = vector->get_format();
        switch (format) {
          case VEC_FIXED:
          {
            const ObFixedLengthBase *vec = static_cast<const ObFixedLengthBase *>(vector);
            has_null = !vec->get_nulls()->is_all_false(row_count);
            for (int64_t row_idx = 0; row_idx < row_count; row_idx++) {
              rowkey_len[row_idx] += vec->get_length();
            }
            break;
          }
          case VEC_CONTINUOUS:
          {
            const ObContinuousBase *vec = static_cast<const ObContinuousBase *>(vector);
            has_null = !vec->get_nulls()->is_all_false(row_count);
            for (int64_t row_idx = 0; row_idx < row_count; row_idx++) {
              rowkey_len[row_idx] += vec->get_offsets()[row_idx + 1] - vec->get_offsets()[row_idx];
            }
            break;
          }
          case VEC_DISCRETE:
          {
            const ObDiscreteBase *vec = static_cast<const ObDiscreteBase *>(vector);
            has_null = !vec->get_nulls()->is_all_false(row_count);
            for (int64_t row_idx = 0; row_idx < row_count; row_idx++) {
              rowkey_len[row_idx] += vec->get_lens()[row_idx];
            }
            break;
          }
          case VEC_UNIFORM:
          {
            const ObUniformBase *vec = static_cast<const ObUniformBase *>(vector);
            for (int64_t row_idx = 0; row_idx < row_count; row_idx++) {
              const ObDatum &datum = vec->get_datums()[row_idx];
              has_null |= datum.is_null();
              rowkey_len[row_idx] += datum.len_;
            }
            break;
          }
          case VEC_UNIFORM_CONST:
          {
            const ObUniformBase *vec = static_cast<const ObUniformBase *>(vector);
            for (int64_t row_idx = 0; row_idx < row_count; row_idx++) {
              const ObDatum &datum = vec->get_datums()[0];
              has_null |= datum.is_null();
              rowkey_len[row_idx] += datum.len_;
            }
            break;
          }
          default:
          {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected vector format", KR(ret), K(format));
            break;
          }
        }
        if (OB_SUCC(ret)) {
          if (!is_index_table && has_null) {
            ret = OB_ER_INVALID_USE_OF_NULL;
            LOG_WARN("invalid null cell for row key column", KR(ret), K(col_idx));
          }
        }
      }
      for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < row_count; row_idx++) {
        if (rowkey_len[row_idx] > OB_MAX_VARCHAR_LENGTH_KEY) {
          ret = OB_ERR_TOO_LONG_KEY_LENGTH;
          LOG_USER_ERROR(OB_ERR_TOO_LONG_KEY_LENGTH, OB_MAX_VARCHAR_LENGTH_KEY);
          LOG_WARN("rowkey is too long", K(ret), K(row_idx), K(rowkey_len[row_idx]));
        }
      }
    }
    if (OB_NOT_NULL(rowkey_len)) {
      ob_free(rowkey_len);
    }
  }
  return ret;
}

int ObDDLStorageUtil::handle_lob_column(
    const ObTabletID &tablet_id,
    const int64_t slice_idx,
    ObWriteMacroParam &param,
    const bool output_invalid_lob_cells,
    ObIArray<std::pair<char **, uint32_t *>> &lob_cells,
    ObArenaAllocator &allocator,
    const ObColumnSchemaItem &column_schema_item,
    ObBatchSelector &selector,
    ObIVector *&vector)
{
  int ret = OB_SUCCESS;
  lob_cells.reuse();
  if (OB_UNLIKELY(!tablet_id.is_valid() ||
                  slice_idx < 0 ||
                  !param.is_valid() ||
                  !selector.is_valid() ||
                  nullptr == vector)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx), K(param), K(selector), KP(vector));
  } else {
    ObDatum temp_datum;
    const ObObjMeta &col_type = column_schema_item.col_type_;
    const VectorFormat format = vector->get_format();
    switch (format) {
      case VEC_CONTINUOUS:
      {
        ObContinuousBase *continuous_vec = static_cast<ObContinuousBase *>(vector);
        ObDiscreteBase *discrete_vec = nullptr;
        char *data = continuous_vec->get_data();
        uint32_t *offsets = continuous_vec->get_offsets();
        char **ptrs = nullptr;
        ObLength *lens = nullptr;
        VecValueTypeClass value_tc = get_vec_value_tc(col_type.get_type(),
                                                      col_type.get_scale(),
                                                      PRECISION_UNKNOWN_YET);
        if (OB_FAIL(new_discrete_vector(value_tc, selector.get_max(), allocator, discrete_vec))) {
          LOG_WARN("fail to new discrete vector", KR(ret));
        } else {
          ptrs = discrete_vec->get_ptrs();
          lens = discrete_vec->get_lens();
        }
        int64_t j = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(j))) {
          if (continuous_vec->is_null(j)) {
            discrete_vec->set_null(j);
            temp_datum.set_null();
          } else {
            temp_datum.reset();
            temp_datum.ptr_ = data + offsets[j];
            temp_datum.len_ = offsets[j + 1] - offsets[j];
          }
          if (output_invalid_lob_cells || (!temp_datum.is_null() && !temp_datum.is_nop())) {
            ptrs[j] = const_cast<char *>(temp_datum.ptr_);
            lens[j] = temp_datum.pack_;
            if (OB_FAIL(lob_cells.push_back(std::make_pair(&ptrs[j], reinterpret_cast<uint32_t *>(&lens[j]))))) {
              LOG_WARN("push back lob cells failed", K(ret));
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        if (OB_SUCC(ret)) {
          vector = discrete_vec;
        }
        break;
      }
      case VEC_DISCRETE:
      {
        ObDiscreteBase *discrete_vec = static_cast<ObDiscreteBase *>(vector);
        char **ptrs = discrete_vec->get_ptrs();
        ObLength *lens =discrete_vec->get_lens();
        int64_t j = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(j))) {
          if (discrete_vec->is_null(j)) {
            temp_datum.set_null();
          } else {
            temp_datum.reset();
            temp_datum.ptr_ = ptrs[j];
            temp_datum.len_ = lens[j];
          }
          if (output_invalid_lob_cells || (!temp_datum.is_null() && !temp_datum.is_nop())) {
            ptrs[j] = const_cast<char *>(temp_datum.ptr_);
            lens[j] = temp_datum.pack_;
            if (OB_FAIL(lob_cells.push_back(std::make_pair(&ptrs[j], reinterpret_cast<uint32_t *>(&lens[j]))))) {
              LOG_WARN("push back lob cells failed", K(ret));
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      case VEC_UNIFORM:
      {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        ObDatum *datums = uniform_vec->get_datums();
        int64_t j = 0;
        while (OB_SUCC(ret) && OB_SUCC(selector.get_next(j))) {
          ObDatum &datum = datums[j];
          if (output_invalid_lob_cells || (!datum.is_null() && !datum.is_nop())) {
            if (OB_FAIL(lob_cells.push_back(std::make_pair(const_cast<char **>(&datum.ptr_), reinterpret_cast<uint32_t *>(&datum.pack_))))) {
              LOG_WARN("push back lob cells failed", K(ret));
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      case VEC_UNIFORM_CONST:
      {
        ObUniformBase *uniform_vec = static_cast<ObUniformBase *>(vector);
        ObDatum *datums = uniform_vec->get_datums();
        ObDatum &datum = datums[0];
        if (datum.is_null_or_nop() && output_invalid_lob_cells) {
          int64_t j = 0;
          while (OB_SUCC(ret) && OB_SUCC(selector.get_next(j))) {
            if (OB_FAIL(lob_cells.push_back(std::make_pair(static_cast<char **>(nullptr), static_cast<uint32_t *>(nullptr))))) {
              LOG_WARN("push back lob cells failed", K(ret));
            }
          }
        } else if (!datum.is_null_or_nop()) {
          ObDiscreteBase *discrete_vec = nullptr;
          char **ptrs = nullptr;
          ObLength *lens = nullptr;
          VecValueTypeClass value_tc = get_vec_value_tc(col_type.get_type(),
                                                        col_type.get_scale(),
                                                        PRECISION_UNKNOWN_YET);
          if (OB_FAIL(new_discrete_vector(value_tc, selector.get_max(), allocator, discrete_vec))) {
            LOG_WARN("fail to new discrete vector", KR(ret));
          } else {
            ptrs = discrete_vec->get_ptrs();
            lens = discrete_vec->get_lens();
            vector = discrete_vec;
          }
          int64_t j = 0;
          while (OB_SUCC(ret) && OB_SUCC(selector.get_next(j))) {
            ptrs[j] = const_cast<char *>(datum.ptr_);
            lens[j] = datum.len_;
            if (OB_FAIL(lob_cells.push_back(std::make_pair(&ptrs[j], reinterpret_cast<uint32_t *>(&lens[j]))))) {
              LOG_WARN("push back lob cells failed", K(ret));
            }
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
        break;
      }
      default:
      {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected lob vector format", KR(ret), K(format));
        break;
      }
    }
  }
  return ret;
}

int ObDDLStorageUtil::convert_to_storage_schema(
  const ObTableSchema *table_schema,
  ObIAllocator &allocator,
  ObStorageSchema *&storage_schema)
{
  int ret = OB_SUCCESS;
  storage_schema = nullptr;
  if (OB_UNLIKELY(nullptr == table_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(table_schema));
  } else {
    if (OB_FAIL(ObTabletObjLoadHelper::alloc_and_new(allocator, storage_schema))) {
      LOG_WARN("alloc and new failed", K(ret));
    } else if (OB_FAIL(storage_schema->init(allocator, *table_schema))) {
      LOG_WARN("failed to copy storage schema", K(ret));
    }
    if (OB_FAIL(ret)) {
      ObTabletObjLoadHelper::free(allocator, storage_schema);
    }
  }
  return ret;
}

}  // namespace storage
}  // namespace oceanbase
