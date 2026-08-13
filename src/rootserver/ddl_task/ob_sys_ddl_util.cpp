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

#define USING_LOG_PREFIX RS

#include "ob_sys_ddl_util.h"
#include "share/rc/ob_server_runtime.h"
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "share/ob_ddl_common.h" // for ObDDLUtil
namespace oceanbase
{
namespace rootserver
{
int ObSysDDLLocalBuilderUtil::push_task(ObAsyncTask &task)
{
  int ret = OB_SUCCESS;
  rootserver::ObDDLScheduler *sys_ddl_scheduler =
      ::oceanbase::share::server_service<::oceanbase::rootserver::ObDDLScheduler>();
  if (OB_ISNULL(sys_ddl_scheduler)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(sys_ddl_scheduler));
  } else if (!ObDDLServiceLauncher::is_ddl_service_started()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("ddl service not started", KR(ret));
  } else {
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(sys_ddl_scheduler->get_ddl_builder().push_task(task))) {
      }
    }
  }
  return ret;
}
} // end name space rootserver
} // end namespace oceanbase
