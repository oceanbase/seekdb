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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "observer/scheduler/ob_ddl_count_guard.h"
#include "share/rc/ob_server_runtime.h"

#include "share/ob_server_struct.h"          // GCTX
#include "observer/omt/ob_server_runtime_controller.h"     // omt::ObServerRuntimeController inc/dec_ddl_count (L9 legal downward)

namespace oceanbase
{
namespace share
{
namespace schema
{

int ObDDLCountGuard::try_inc_ddl_count(const int64_t cpu_quota_concurrency)
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeController *runtime_controller = ::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>();
  if (OB_ISNULL(runtime_controller)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime controller is null", KR(ret));
  } else if (OB_FAIL(runtime_controller->inc_ddl_count(cpu_quota_concurrency))) {
    LOG_WARN("fail to increment runtime DDL count", KR(ret));
  } else {
    had_inc_ddl_ = true;
  }
  return ret;
}

ObDDLCountGuard::~ObDDLCountGuard()
{
  int ret = OB_SUCCESS;
  if (had_inc_ddl_) {
    omt::ObServerRuntimeController *runtime_controller = ::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>();
    if (OB_ISNULL(runtime_controller)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("runtime controller is null", KR(ret));
    } else if (OB_FAIL(runtime_controller->dec_ddl_count())) {
      LOG_WARN("fail to decrement runtime DDL count", KR(ret));
    }
  }
}

} // end schema
} // end share
} // end oceanbase
