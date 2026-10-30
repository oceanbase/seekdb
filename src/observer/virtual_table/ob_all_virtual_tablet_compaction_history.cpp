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

#include "ob_all_virtual_tablet_compaction_history.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace storage;
using namespace common;
using namespace compaction;
namespace observer
{
ObAllVirtualTabletCompactionHistory::ObAllVirtualTabletCompactionHistory()
    : ip_buf_(),
      parallel_merge_info_buf_(),
      dag_id_buf_(),
      participant_table_str_(),
      macro_id_list_(),
      other_info_(),
      comment_(),
      merge_history_(),
      major_merge_info_iter_(),
      minor_merge_info_iter_()
{
}

ObAllVirtualTabletCompactionHistory::~ObAllVirtualTabletCompactionHistory()
{
  reset();
}

int ObAllVirtualTabletCompactionHistory::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  const int64_t col_count = output_column_ids_.count();
  DatumValue *cells = cur_row_.cells_;
  int64_t compression_ratio = 0;
  int n = 0;
  if (!major_merge_info_iter_.is_opened() && !minor_merge_info_iter_.is_opened()) {
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObSSTableMergeInfoMgr>()->open_iter(major_merge_info_iter_, minor_merge_info_iter_))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (FALSE_IT(MEMSET(comment_, '\0', sizeof(comment_)))) {
    } else if (FALSE_IT(MEMSET(other_info_, '\0', sizeof(other_info_)))) {
    } else if (OB_FAIL(ObSSTableMergeInfoMgr::get_next_info(major_merge_info_iter_,
                minor_merge_info_iter_,
                merge_history_, other_info_, sizeof(other_info_)))) {
      if (OB_ITER_END != ret) {
        STORAGE_LOG(WARN, "fail to get next sstable merge info", K(ret));
      }
    }
  }
  const ObMergeStaticInfo &static_info = merge_history_.static_info_;
  const ObMergeRunningInfo &running_info = merge_history_.running_info_;
  const ObMergeBlockInfo &block_info = merge_history_.block_info_;
  const ObMergeDiagnoseInfo &diagnose_info = merge_history_.diagnose_info_;
  for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
    uint64_t col_id = output_column_ids_.at(i);
    switch (col_id) {
    case TABLET_ID:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, static_info.tablet_id_.id()) : ret);
      break;
    case MERGE_TYPE: {
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, merge_type_to_str(static_info.merge_type_)) : ret);
      cells[i].set_collation_type(
          ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    }
    case MERGE_VERSION:
      //TODO:SCN
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cells, cur_row_.count_, i, static_info.compaction_scn_ < 0 ? 0 : static_info.compaction_scn_) : ret);
      break;
    case MERGE_START_TIME:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, i, running_info.merge_start_time_) : ret);
      break;
    case MERGE_FINISH_TIME:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, i, running_info.merge_finish_time_) : ret);
      break;
    case TASK_ID:
      //dag_id
      n = running_info.dag_id_.to_string(dag_id_buf_, sizeof(dag_id_buf_));
      if (n < 0 || n >= sizeof(dag_id_buf_)) {
        ret = OB_BUF_NOT_ENOUGH;
        SERVER_LOG(WARN, "buffer not enough", K(ret));
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, dag_id_buf_) : ret);
        cells[i].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
      }
      break;
    case OCCUPY_SIZE:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.occupy_size_) : ret);
      break;
    case MACRO_BLOCK_COUNT:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.macro_block_count_) : ret);
      break;
    case MULTIPLEXED_MACRO_BLOCK_COUNT:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.multiplexed_macro_block_count_) : ret);
      break;
    case NEW_MICRO_COUNT_IN_NEW_MACRO:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.new_micro_count_in_new_macro_) : ret);
      break;
    case MULTIPLEXED_MICRO_COUNT_IN_NEW_MACRO:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.multiplexed_micro_count_in_new_macro_) : ret);
      break;
    case TOTAL_ROW_COUNT:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.total_row_count_) : ret);
      break;
    case INCREMENTAL_ROW_COUNT:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.incremental_row_count_) : ret);
      break;
    case COMPRESSION_RATIO:
      if (0 == block_info.original_size_) {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_double(cells, cur_row_.count_, i, 1) : ret);
      } else {
        compression_ratio = block_info.compressed_size_ * 1.0 / block_info.original_size_ * 100;
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_double(cells, cur_row_.count_, i, compression_ratio * 1.0 / 100) : ret);
      }
      break;
    case NEW_FLUSH_DATA_RATE:
      // calc flush data speed
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, block_info.new_flush_data_rate_) : ret);
      break;
    case PROGRESSIVE_MREGE_ROUND:
      if (static_info.is_full_merge_) {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, -1) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, static_info.progressive_merge_round_) : ret);
      }
      break;
    case PROGRESSIVE_MREGE_NUM:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, static_info.progressive_merge_num_) : ret);
      break;
    case PARALLEL_DEGREE:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, static_info.concurrent_cnt_) : ret);
      break;
    case PARALLEL_INFO:
      if (static_info.concurrent_cnt_ > 1) {
        running_info.parallel_merge_info_.to_paral_info_string(
            parallel_merge_info_buf_,
            common::OB_PARALLEL_MERGE_INFO_LENGTH);
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, parallel_merge_info_buf_) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, "-") : ret);
      }
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case PARTICIPANT_TABLE_INFO:
      MEMSET(participant_table_str_, '\0', sizeof(participant_table_str_));
      static_info.participant_table_info_.fill_info(participant_table_str_, sizeof(participant_table_str_));
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, participant_table_str_) : ret);
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case MACRO_ID_LIST:
      MEMSET(macro_id_list_, '\0', sizeof(macro_id_list_));
      MEMCPY(macro_id_list_, block_info.macro_id_list_, strlen(block_info.macro_id_list_));
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, macro_id_list_) : ret);
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case COMMENT:
      merge_history_.fill_comment(comment_, sizeof(comment_), other_info_);
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, comment_) : ret);
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case KEPT_SNAPSHOT:
      if (static_info.kept_snapshot_info_.is_valid()) {
        MEMSET(kept_snapshot_info_, '\0', sizeof(kept_snapshot_info_));
        static_info.kept_snapshot_info_.to_string(kept_snapshot_info_, sizeof(kept_snapshot_info_));
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, kept_snapshot_info_) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, "") : ret);
      }
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case MERGE_LEVEL:
      if (is_valid_merge_level(static_info.merge_level_)) {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, merge_level_to_str(static_info.merge_level_)) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, "") : ret);
      }
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case IS_FULL_MERGE:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_bool(cells, cur_row_.count_, i, static_info.is_full_merge_) : ret);
      break;
    case IO_COST_TIME_PERCENTAGE:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, running_info.io_percentage_) : ret);
      break;
    case MERGE_REASON:
      if (ObAdaptiveMergePolicy::is_valid_merge_reason(static_info.merge_reason_)) {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, ObAdaptiveMergePolicy::merge_reason_to_str(static_info.merge_reason_)) : ret);
      } else {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, "") : ret);
      }
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case BASE_MAJOR_STATUS:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, "") : ret);
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case MDS_FILTER_INFO:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, i, static_info.mds_filter_info_str_) : ret);
      cells[i].set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset()));
      break;
    case EXECUTE_TIME:
      (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, i, running_info.execute_time_) : ret);
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid column id", K(ret), K(col_id));
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}
void ObAllVirtualTabletCompactionHistory::reset()
{
  major_merge_info_iter_.reset();
  minor_merge_info_iter_.reset();
  ObVirtualTableScannerIterator::reset();
  memset(ip_buf_, 0, sizeof(ip_buf_));
  memset(parallel_merge_info_buf_, 0, sizeof(parallel_merge_info_buf_));
  memset(dag_id_buf_, 0, sizeof(dag_id_buf_));
  memset(participant_table_str_, 0, sizeof(participant_table_str_));
  memset(macro_id_list_, 0, sizeof(macro_id_list_));
  memset(comment_, 0, sizeof(comment_));
  memset(other_info_, 0, sizeof(other_info_));
}

} /* namespace observer */
} /* namespace oceanbase */
