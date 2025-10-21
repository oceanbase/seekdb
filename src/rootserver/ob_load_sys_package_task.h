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
#ifndef _OCEANBASE_ROOTSERVER_OB_LOAD_SYS_PACKAGE_TASK_H_
#define _OCEANBASE_ROOTSERVER_OB_LOAD_SYS_PACKAGE_TASK_H_ 1

#include "deps/oblib/src/lib/thread/ob_work_queue.h"
#include "deps/oblib/src/lib/mysqlclient/ob_mysql_proxy.h"
#include "deps/oblib/src/common/ob_timeout_ctx.h"
#include "src/share/ob_define.h"

namespace oceanbase
{
namespace rootserver
{
class ObRootService;
class ObLoadSysPackageTask : public common::ObAsyncTimerTask
{
public:
  explicit ObLoadSysPackageTask(ObRootService &root_service, int64_t fail_count = 0);
  virtual ~ObLoadSysPackageTask() {}
  virtual int64_t get_deep_copy_size() const override { return sizeof(*this); }
  virtual ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const override;
  virtual int process();
  static int wait_sys_package_ready(
      common::ObMySQLProxy &sql_proxy,
      const common::ObTimeoutCtx &ctx,
      ObCompatibilityMode mode);
private:
  int load_package();
private:
  ObRootService &root_service_;
  int64_t fail_count_;
};
}
}

#endif // _OCEANBASE_ROOTSERVER_OB_LOAD_SYS_PACKAGE_TASK_H_
