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

#ifndef OCEANBASE_ROOTSERVER_OB_FORK_TABLE_TASK_H
#define OCEANBASE_ROOTSERVER_OB_FORK_TABLE_TASK_H

#include "rootserver/ddl_task/ob_ddl_task.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace storage
{
enum class ObDDLClogType : int64_t;
class ObTableForkInfo;
}
using namespace share;
using namespace common;
namespace rootserver
{

class ObForkTableTask : public ObDDLTask
{
public:
  ObForkTableTask();
  virtual ~ObForkTableTask();

  int init(
      const uint64_t tenant_id,
      const int64_t task_id,
      const share::ObDDLType &ddl_type,
      const share::schema::ObTableSchema *src_table_schema,
      const share::schema::ObTableSchema *dst_table_schema,
      const int64_t schema_version,
      const int64_t snapshot_version,
      const obrpc::ObForkTableArg &fork_table_arg,
      const int64_t parent_task_id = 0);

  int init(const ObDDLTaskRecord &task_record);

  virtual int process() override;
  virtual bool is_valid() const override;
  virtual int serialize_params_to_message(char *buf, const int64_t buf_size, int64_t &pos) const override;
  virtual int deserialize_params_from_message(const uint64_t tenant_id, const char *buf, const int64_t buf_size, int64_t &pos) override;
  virtual int64_t get_serialize_param_size() const override;

  INHERIT_TO_STRING_KV("ObDDLTask", ObDDLTask, K_(fork_table_arg));

private:
  int wait_freeze_end(const ObDDLTaskStatus next_task_status);
  int build_data(const ObDDLTaskStatus next_task_status);
  int wait_data_complement(const ObDDLTaskStatus next_task_status);
  int fail();
  int succ();
  int finish();
  virtual int cleanup_impl() override;

  int deep_copy_fork_table_arg(const obrpc::ObForkTableArg &arg);
  int get_schema_guard(share::schema::ObSchemaGetterGuard &schema_guard);
  int build_fork_info(
      const ObSEArray<ObTabletID, 4> &src_tablet_ids,
      const ObSEArray<ObTabletID, 4> &dst_tablet_ids,
      storage::ObTableForkInfo &fork_info);

  ObRootService *root_service_;
  obrpc::ObForkTableArg fork_table_arg_;
  bool is_data_complement_;

  DISALLOW_COPY_AND_ASSIGN(ObForkTableTask);
};

}  // namespace rootserver
}  // namespace oceanbase

#endif /* OCEANBASE_ROOTSERVER_OB_FORK_TABLE_TASK_H */
