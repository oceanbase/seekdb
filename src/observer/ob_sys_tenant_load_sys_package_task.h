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
#ifndef _OCEANBASE_ROOTSERVER_OB_SYS_TENANT_LOAD_SYS_PACKAGE_TASK_H_
#define _OCEANBASE_ROOTSERVER_OB_SYS_TENANT_LOAD_SYS_PACKAGE_TASK_H_ 1

#include "common/ob_timeout_ctx.h"
#include "lib/task/ob_timer.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace rootserver
{
class ObSysTenantLoadSysPackageTask : public common::ObTimerTask
{
public:
  ObSysTenantLoadSysPackageTask();
  virtual ~ObSysTenantLoadSysPackageTask() {}

  int init(const uint64_t tenant_id);
  bool is_inited() const { return inited_; }
  int start(const int tg_id);
  void stop(const int tg_id);
  void destroy();

  virtual void runTimerTask() override;

  static int wait_sys_package_ready(const common::ObTimeoutCtx &ctx, ObCompatibilityMode mode);

private:
  int do_sys_tenant_load_sys_package_();

private:
  bool inited_;
  int64_t fail_count_;
  static const int64_t SCHEDULE_INTERVAL_US = 3 * 1000 * 1000L; // 3s

private:
  DISALLOW_COPY_AND_ASSIGN(ObSysTenantLoadSysPackageTask);
};
}
}

#endif // _OCEANBASE_ROOTSERVER_OB_SYS_TENANT_LOAD_SYS_PACKAGE_TASK_H_
