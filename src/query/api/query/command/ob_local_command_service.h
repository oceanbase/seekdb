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

#ifndef OCEANBASE_QUERY_COMMAND_OB_LOCAL_COMMAND_SERVICE_H_
#define OCEANBASE_QUERY_COMMAND_OB_LOCAL_COMMAND_SERVICE_H_

#include "share/ob_define.h"

namespace oceanbase
{
namespace obcall
{
struct ObDebugSyncActionArg;
struct ObFlushOptStatArg;
struct ObSetTracepointParam;
struct ObUpdateStatCacheArg;
}
namespace common
{
class ObTimeoutCtx;
}
namespace query
{

class ObILocalCommandService
{
public:
  virtual ~ObILocalCommandService() = default;
  virtual int refresh_memory_stat() = 0;
  virtual int clear_expired_deadlock_events() = 0;
  virtual int set_tracepoint(const obcall::ObSetTracepointParam &arg) = 0;
  virtual int cancel_sys_task(const share::ObTaskId &task_id) = 0;
  virtual int load_all_special_system_packages() = 0;
  virtual int wait_system_package_ready(const common::ObTimeoutCtx &ctx) = 0;
  virtual int get_build_version(char *buf, int64_t buf_len) = 0;
  virtual int set_ds_action(const obcall::ObDebugSyncActionArg &arg) = 0;
  virtual int refresh_stat_cache(const obcall::ObUpdateStatCacheArg &arg) = 0;
  virtual int update_opt_stat_monitoring_info(
      const obcall::ObFlushOptStatArg &arg) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_COMMAND_OB_LOCAL_COMMAND_SERVICE_H_
