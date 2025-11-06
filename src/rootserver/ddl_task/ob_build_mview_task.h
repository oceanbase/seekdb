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

#ifndef OCEANBASE_ROOTSERVER_OB_BUILD_MVIEW_TASK_H
#define OCEANBASE_ROOTSERVER_OB_BUILD_MVIEW_TASK_H

#include "rootserver/ddl_task/ob_ddl_task.h"

namespace oceanbase
{
namespace rootserver
{
class ObBuildMViewTask : public ObDDLTask
{
public:
  ObBuildMViewTask();
  virtual ~ObBuildMViewTask();
  int init(const ObDDLTaskRecord &task_record);
  int init(const uint64_t tenant_id,
           const int64_t task_id,
           const share::schema::ObTableSchema *mview_schema,
           const int64_t schema_version,
           const int64_t parallel,
           const int64_t consumer_group_id,
           const obrpc::ObMViewCompleteRefreshArg &mview_complete_refresh_arg,
           const int64_t parent_task_id,
           const int64_t task_status = share::ObDDLTaskStatus::START_REFRESH_MVIEW_TASK,
           const int64_t snapshot_version = 0);
  virtual int process() override;
  virtual int cleanup_impl() override;
  virtual int serialize_params_to_message(char *buf,
                                          const int64_t buf_size,
                                          int64_t &pos) const override;
  virtual int deserialize_params_from_message(const uint64_t tenant_id,
                                              const char *buf,
                                              const int64_t buf_size,
                                              int64_t &pos) override;
  virtual int64_t get_serialize_param_size() const override;
  virtual bool task_can_retry() const { return false; } //build mview task should not retry
  int on_child_task_prepare(const int64_t task_id);

private:
  int start_refresh_mview_task();
  int wait_child_task_finish();
  int enable_mview();
  int succ();
  int clean_on_fail();
  int check_health();
  int mview_complete_refresh(obrpc::ObMViewCompleteRefreshRes &res);
  int update_task_message();
  int set_mview_complete_refresh_task_id(const int64_t task_id);

private:
  static const int64_t OB_BUILD_MVIEW_TASK_VERSION = 1;
  uint64_t &mview_table_id_;
  ObRootService *root_service_;
  obrpc::ObMViewCompleteRefreshArg arg_;
  int64_t mview_complete_refresh_task_id_;
};
} // namespace rootserver
} // namespace oceanbase

#endif
