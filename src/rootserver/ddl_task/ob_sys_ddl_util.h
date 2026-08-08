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

#ifndef __OB_RS_SYS_DDL_SCHEDULER_UTIL_H__
#define __OB_RS_SYS_DDL_SCHEDULER_UTIL_H__

#include "share/rc/ob_server_runtime.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h" // for ObDDLScheduler

namespace oceanbase
{
namespace rootserver
{

int check_local_is_sys_leader();

#define SYS_DDL_SCHEDULER_FUNC(func_name)                                                 \
  template <typename... Args> static int func_name(Args &&...args) {                      \
    int ret = OB_SUCCESS;                                                                 \
    if (OB_FAIL(share::check_server_runtime_ready())) {                                  \
      LOG_WARN("local runtime is unavailable", KR(ret));                                 \
    } else if (OB_FAIL(check_local_is_sys_leader())) {                                    \
      LOG_WARN("local runtime is not ready", KR(ret));                                \
    } else {                                                                              \
      SERVER_MODULE_SCOPE {                                                      \
        rootserver::ObDDLScheduler* sys_ddl_scheduler = ::oceanbase::share::server_service<::oceanbase::rootserver::ObDDLScheduler>(); \
        if (OB_ISNULL(sys_ddl_scheduler)) {                                               \
          ret = OB_ERR_UNEXPECTED;                                                        \
          LOG_WARN("sys ddl scheduler service is null", KR(ret));                         \
        } else if (OB_FAIL(sys_ddl_scheduler->func_name(std::forward<Args>(args)...))) {  \
          LOG_WARN("fail to execute ddl scheduler function", KR(ret));                    \
        }                                                                                 \
      }                                                                                   \
    }                                                                                     \
    return ret;                                                                           \
  }

class ObSysDDLSchedulerUtil
{
public:
  SYS_DDL_SCHEDULER_FUNC(abort_redef_table);
  SYS_DDL_SCHEDULER_FUNC(copy_table_dependents);
  SYS_DDL_SCHEDULER_FUNC(create_ddl_task);
  SYS_DDL_SCHEDULER_FUNC(destroy_task);
  SYS_DDL_SCHEDULER_FUNC(finish_redef_table);
  SYS_DDL_SCHEDULER_FUNC(get_task_record);
  SYS_DDL_SCHEDULER_FUNC(notify_update_autoinc_end);
  SYS_DDL_SCHEDULER_FUNC(modify_redef_task);
  SYS_DDL_SCHEDULER_FUNC(on_column_checksum_calc_reply);
  SYS_DDL_SCHEDULER_FUNC(on_ddl_task_finish);
  SYS_DDL_SCHEDULER_FUNC(on_sstable_complement_job_reply);
  SYS_DDL_SCHEDULER_FUNC(prepare_alter_table_arg);
  SYS_DDL_SCHEDULER_FUNC(recover_task);
  SYS_DDL_SCHEDULER_FUNC(remove_inactive_ddl_task);
  SYS_DDL_SCHEDULER_FUNC(schedule_ddl_task);
  SYS_DDL_SCHEDULER_FUNC(start_redef_table);
  SYS_DDL_SCHEDULER_FUNC(update_ddl_task_active_time);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSysDDLSchedulerUtil);
};// end ObSysDDLSchedulerUtil

class ObSysDDLLocalBuilderUtil
{
public:
  static int push_task(ObAsyncTask &task);
private:
  DISALLOW_COPY_AND_ASSIGN(ObSysDDLLocalBuilderUtil);
};// end ObSysDDLLocalBuilderUtil

}
}
#endif /* __OB_RS_SYS_DDL_SCHEDULER_UTIL_H__ */
