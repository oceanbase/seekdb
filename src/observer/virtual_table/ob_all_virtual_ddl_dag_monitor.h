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

#ifndef OB_ALL_VIRTUAL_DDL_DAG_MONITOR_H_
#define OB_ALL_VIRTUAL_DDL_DAG_MONITOR_H_

#include "lib/container/ob_se_array.h"
#include "observer/virtual_table/ob_virtual_table_scanner_iterator.h"
#include "storage/ddl/ob_ddl_dag_monitor_node.h"

namespace oceanbase
{
namespace observer
{

class ObAllVirtualDDLDagMonitor : public common::ObVirtualTableScannerIterator
{
public:
  ObAllVirtualDDLDagMonitor()
    : is_inited_(false), nodes_()
  {
  }

  virtual ~ObAllVirtualDDLDagMonitor() {}

  int init();
  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset() override;

private:
  enum ColumnId
  {
    DAG_PTR = common::OB_APP_MIN_COLUMN_ID,
    TRACE_ID,
    DAG_CREATE_TS,
    DAG_FINISH_TS,
    TASK_TYPE,
    TASK_CREATE_TS,
    TASK_FINISH_TS,
    SCHEDULE_COUNT,
    ACCUM_EXEC_TIME_US,
    RET_CODE,
    DOC_COUNT,
    TOKEN_COUNT,
  };

  static int collect_callback(const storage::ObDDLDagMonitorNode &node, void *user_data);

  bool is_inited_;
  int64_t current_idx_;
  struct CollectedRow
  {
    uint64_t dag_ptr_;
    uint64_t trace_id_;
    int64_t dag_create_ts_;
    int64_t dag_finish_ts_;
    int32_t task_type_;
    int64_t task_create_ts_;
    int64_t task_finish_ts_;
    int32_t schedule_count_;
    int64_t accum_exec_time_us_;
    int ret_code_;
    int64_t doc_count_;
    int64_t token_count_;
  };
  common::ObSEArray<CollectedRow, 128> nodes_;
};

} // end namespace observer
} // end namespace oceanbase

#endif /* OB_ALL_VIRTUAL_DDL_DAG_MONITOR_H_ */
