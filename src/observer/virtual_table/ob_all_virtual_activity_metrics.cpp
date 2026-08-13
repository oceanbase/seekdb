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

#include "observer/virtual_table/ob_all_virtual_activity_metrics.h"
#include "share/rc/ob_server_runtime.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace observer
{

ObAllVirtualActivityMetric::ObAllVirtualActivityMetric()
    : ObVirtualTableScannerIterator(),
      current_pos_(0),
      length_(0)
{
}

ObAllVirtualActivityMetric::~ObAllVirtualActivityMetric()
{
  reset();
}

void ObAllVirtualActivityMetric::reset()
{
  current_pos_ = 0;
  length_ = 0;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualActivityMetric::get_next_freezer_stat_(
    storage::ObMemstoreFreezerStat &stat)
{
  int ret = OB_SUCCESS;
  storage::ObMemstoreFreezer *freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();

  if (current_pos_ < length_) {
    (void)freezer->get_freezer_stat_from_history(current_pos_, stat);
    current_pos_++;
  } else {
    ret = OB_ITER_END;
  }

  return ret;
}

int ObAllVirtualActivityMetric::prepare_start_to_read_()
{
  int ret = OB_SUCCESS;
  storage::ObMemstoreFreezer *freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();

  (void)freezer->get_freezer_stat_history_snapshot(length_);
  current_pos_ = 0;
  start_to_read_ = true;

  return ret;
}

int ObAllVirtualActivityMetric::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  storage::ObMemstoreFreezerStat stat;

  if (NULL == allocator_) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "allocator_ shouldn't be NULL", K(allocator_), K(ret));
  } else if (!start_to_read_ && OB_FAIL(prepare_start_to_read_())) {
    SERVER_LOG(WARN, "prepare start to read", K(allocator_), K(ret));
  } else if (OB_FAIL(get_next_freezer_stat_(stat))) {
    if (OB_ITER_END != ret) {
      SERVER_LOG(WARN, "get next freezer stat failed", KR(ret));
    }
  } else {
    ObObj *cells = NULL;
    // allocator_ is allocator of PageArena type, no need to free
    if (NULL == (cells = cur_row_.cells_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
        case ACTIVITY_TIMESTAMP:
          cells[i].set_timestamp(stat.last_captured_timestamp_);
          break;
        case MODIFICATION_SIZE:
          cells[i].set_int(stat.captured_data_size_);
          break;
        case FREEZE_TIMES:
          cells[i].set_int(stat.captured_freeze_times_);
          break;
        case MINI_MERGE_COST:
          cells[i].set_int(stat.captured_merge_time_cost_[storage::ObMemstoreFreezerStat::ObFreezerMergeType::MINI_MERGE]);
          break;
        case MINI_MERGE_TIMES:
          cells[i].set_int(stat.captured_merge_times_[storage::ObMemstoreFreezerStat::ObFreezerMergeType::MINI_MERGE]);
          break;
        case MINOR_MERGE_COST:
          cells[i].set_int(stat.captured_merge_time_cost_[storage::ObMemstoreFreezerStat::ObFreezerMergeType::MINOR_MERGE]);
          break;
        case MINOR_MERGE_TIMES:
          cells[i].set_int(stat.captured_merge_times_[storage::ObMemstoreFreezerStat::ObFreezerMergeType::MINOR_MERGE]);
          break;
        case MAJOR_MERGE_COST:
          cells[i].set_int(stat.captured_merge_time_cost_[storage::ObMemstoreFreezerStat::ObFreezerMergeType::MAJOR_MERGE]);
          break;
        case MAJOR_MERGE_TIMES:
          cells[i].set_int(stat.captured_merge_times_[storage::ObMemstoreFreezerStat::ObFreezerMergeType::MAJOR_MERGE]);
          break;
        default:
          // abnormal column id
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "unexpected column id", K(ret));
          break;
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }

  return ret;
}

} // namepace observer
} // namespace observer
