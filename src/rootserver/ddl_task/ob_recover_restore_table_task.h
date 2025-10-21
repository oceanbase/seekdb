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

#ifndef OCEANBASE_ROOTSERVER_OB_RECOVER_RESTORE_TABLE_TASK_H
#define OCEANBASE_ROOTSERVER_OB_RECOVER_RESTORE_TABLE_TASK_H

#include "rootserver/ddl_task/ob_ddl_task.h"
#include "rootserver/ddl_task/ob_table_redefinition_task.h"

namespace oceanbase
{
namespace sql
{
  class ObLoadDataStat;
}
namespace rootserver
{
class ObRootService;

class ObRecoverRestoreTableTask final : public ObTableRedefinitionTask
{
public:
  ObRecoverRestoreTableTask();
  virtual ~ObRecoverRestoreTableTask();
  int init(
      const ObTableSchema* src_table_schema,
      const ObTableSchema* dst_table_schema,
      const int64_t task_id,
      const share::ObDDLType &ddl_type,
      const int64_t parallelism,
      const int64_t consumer_group_id,
      const int32_t sub_task_trace_id,
      const obrpc::ObAlterTableArg &alter_table_arg,
      const uint64_t tenant_data_version,
      const int64_t task_status = share::ObDDLTaskStatus::PREPARE,
      const int64_t snapshot_version = 0);
  int init(const ObDDLTaskRecord &task_record);
  virtual int process() override;
  virtual int update_complete_sstable_job_status(
    const common::ObTabletID &tablet_id,
    const ObAddr &addr,
    const int64_t snapshot_version,
    const int64_t execution_id,
    const int ret_code,
    const ObDDLTaskInfo &addition_info) override;
protected:
  virtual int obtain_snapshot(const share::ObDDLTaskStatus next_task_status) override;
  virtual int fail() override;
  virtual int success() override;
private:
  int check_health();
private:
  static const int64_t OB_RECOVER_RESTORE_TABLE_TASK_VERSION = 1L;
};

}  // end namespace rootserver
}  // end namespace oceanbase

#endif  // OCEANBASE_ROOTSERVER_OB_RECOVER_RESTORE_TABLE_TASK_H
