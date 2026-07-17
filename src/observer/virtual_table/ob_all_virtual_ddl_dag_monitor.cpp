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

#define USING_LOG_PREFIX SERVER

#include "observer/virtual_table/ob_all_virtual_ddl_dag_monitor.h"
#include "storage/ddl/ob_ddl_dag_monitor_mgr.h"
#include "storage/ddl/ob_ddl_dag_monitor_info.h"

namespace oceanbase
{
namespace observer
{

int ObAllVirtualDDLDagMonitor::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(storage::ObDDLDagMonitorMgr::instance().foreach_node(collect_callback, this))) {
    LOG_WARN("fail to collect monitor nodes", K(ret));
  } else {
    current_idx_ = 0;
    is_inited_ = true;
  }
  return ret;
}

int ObAllVirtualDDLDagMonitor::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (current_idx_ >= nodes_.count()) {
    ret = OB_ITER_END;
  } else {
    const CollectedRow &collected = nodes_.at(current_idx_);
    for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
      uint64_t col_id = output_column_ids_.at(i);
      switch (col_id) {
        case DAG_PTR:
          cur_row_.cells_[i].set_uint64(collected.dag_ptr_);
          break;
        case TRACE_ID:
          cur_row_.cells_[i].set_uint64(collected.trace_id_);
          break;
        case DAG_CREATE_TS:
          cur_row_.cells_[i].set_timestamp(collected.dag_create_ts_);
          break;
        case DAG_FINISH_TS:
          cur_row_.cells_[i].set_timestamp(collected.dag_finish_ts_);
          break;
        case TASK_TYPE:
          cur_row_.cells_[i].set_int(collected.task_type_);
          break;
        case TASK_CREATE_TS:
          cur_row_.cells_[i].set_timestamp(collected.task_create_ts_);
          break;
        case TASK_FINISH_TS:
          cur_row_.cells_[i].set_timestamp(collected.task_finish_ts_);
          break;
        case SCHEDULE_COUNT:
          cur_row_.cells_[i].set_int(collected.schedule_count_);
          break;
        case ACCUM_EXEC_TIME_US:
          cur_row_.cells_[i].set_int(collected.accum_exec_time_us_);
          break;
        case RET_CODE:
          cur_row_.cells_[i].set_int(collected.ret_code_);
          break;
        case DOC_COUNT:
          cur_row_.cells_[i].set_int(collected.doc_count_);
          break;
        case TOKEN_COUNT:
          cur_row_.cells_[i].set_int(collected.token_count_);
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected column id", K(col_id));
          break;
      }
    }
    if (OB_SUCC(ret)) {
      row = &cur_row_;
      current_idx_++;
    }
  }
  return ret;
}

void ObAllVirtualDDLDagMonitor::reset()
{
  nodes_.reset();
  current_idx_ = 0;
  is_inited_ = false;
  ObVirtualTableScannerIterator::reset();
}

int ObAllVirtualDDLDagMonitor::collect_callback(
    const storage::ObDDLDagMonitorNode &node, void *user_data)
{
  int ret = OB_SUCCESS;
  ObAllVirtualDDLDagMonitor *self = static_cast<ObAllVirtualDDLDagMonitor *>(user_data);
  if (OB_ISNULL(self)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const common::ObDList<storage::ObDDLDagMonitorInfo> &info_list = node.get_info_list();
    DLIST_FOREACH_X(info, info_list, OB_SUCC(ret))
    {
      CollectedRow row;
      row.dag_ptr_ = node.get_dag_ptr();
      row.trace_id_ = node.get_trace_id();
      row.dag_create_ts_ = node.get_create_ts();
      row.dag_finish_ts_ = node.get_finish_ts();
      row.task_type_ = static_cast<int32_t>(info->get_task_type());
      row.task_create_ts_ = info->get_create_ts();
      row.task_finish_ts_ = info->get_finish_ts();
      row.schedule_count_ = info->get_schedule_count();
      row.accum_exec_time_us_ = info->get_accum_exec_time_us();
      row.ret_code_ = info->get_ret_code();
      row.doc_count_ = 0;
      row.token_count_ = 0;
      const storage::ObDDLDagMonitorFtsInfo *fts_info =
          dynamic_cast<const storage::ObDDLDagMonitorFtsInfo *>(info);
      if (fts_info != nullptr) {
        row.doc_count_ = fts_info->get_doc_count();
        row.token_count_ = fts_info->get_token_count();
      }
      if (OB_FAIL(self->nodes_.push_back(row))) {
        LOG_WARN("fail to push back collected row", K(ret));
      }
    }
  }
  return ret;
}

} // end namespace observer
} // end namespace oceanbase
