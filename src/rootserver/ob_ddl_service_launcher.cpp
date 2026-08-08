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

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "logservice/ob_log_service.h" // for ObLogService
#include "lib/lock/ob_spin_rwlock.h" // for SpinRWLock
#include "ob_ddl_service_launcher.h"
#include "share/ob_structured_event_logger.h" // for SERVER_EVENT_ADD
#include "share/ob_server_struct.h"     // for GCTX
#include "share/rc/ob_server_runtime.h"    // for SERVER_ID
#include "rootserver/ob_local_management_service.h" // for ObLocalManagementService
#include "query/command/ob_local_command_service.h"

namespace oceanbase
{
namespace rootserver
{
bool ObDDLServiceLauncher::is_ddl_service_started_ = false;
ObDDLServiceLauncher::ObDDLServiceLauncher()
  : inited_(false)
{
}

int ObDDLServiceLauncher::server_module_init(ObDDLServiceLauncher *&ddl_service_launcher)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] begin server_module_init for ddl_service_launcher");
  if (OB_NOT_NULL(ddl_service_launcher)) {
    if (OB_FAIL(ddl_service_launcher->init())) {
      LOG_WARN("failed to init ddl_service_launcher", KR(ret));
    }
  }
  int64_t duration_time = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] finish server_module_init for ddl_service_launcher",
            KR(ret),  K(duration_time));
  return ret;
}

int ObDDLServiceLauncher::init()
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] begin init for ddl_service_launcher");
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    inited_ = true;
  }
  int64_t duration_time = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] finish init for ddl_service_launcher", KR(ret),
             K(duration_time));
  return ret;
}

void ObDDLServiceLauncher::destroy()
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] begin destroy for ddl_service_launcher");
  {
    inited_ = false;
  }
  int64_t duration_time = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] finish destroy for ddl_service_launcher", KR(ret),
             K(duration_time));
}

int ObDDLServiceLauncher::activate()
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] begin switch_to_leader for ddl_service_launcher");
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ddl service launcher is not inited", KR(ret), K_(inited));
  } else if (OB_FAIL(inner_start_ddl_service_with_lock_())) {
    LOG_WARN("fail to inner start ddl service with lock", KR(ret));
  }
  int64_t duration_time = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("[DDL_SERVICE_LAUNCHER] finish switch_to_leader for ddl_service_launcher", KR(ret),
             K(duration_time));
  return ret;
}

int ObDDLServiceLauncher::get_sys_palf_role_and_epoch(
    common::ObRole &role,
    int64_t &proposal_id)
{
  role = LEADER;
  proposal_id = 1;
  return OB_SUCCESS;
}

void ObDDLServiceLauncher::deactivate()
{
  if (inited_) {
    SpinWLockGuard guard(rw_lock_);
    ATOMIC_SET(&is_ddl_service_started_, false);
  }
}

int ObDDLServiceLauncher::init_sequence_id_(const int64_t proposal_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_ID == proposal_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("proposal id not valid", KR(ret), K(proposal_id));
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()));
  } else {
    ObRefreshSchemaInfo schema_info;
    ObSchemaService *schema_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->get_schema_service().get_schema_service();
    if (OB_ISNULL(schema_service)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", KR(ret), KP(schema_service));
    } else if (OB_FAIL(schema_service->init_sequence_id_by_sys_leader_epoch(proposal_id))) {
      LOG_WARN("fail to init sequence id by sys leader epoch", KR(ret), K(proposal_id));
    } else if (OB_FAIL(schema_service->set_refresh_schema_info(schema_info))) {
      LOG_WARN("fail to set refresh schema info", K(ret), K(schema_info));
    }
  }
  return ret;
}

int ObDDLServiceLauncher::inner_start_ddl_service_with_lock_()
{
  int ret = OB_SUCCESS;
  common::ObRole role = FOLLOWER;
  int64_t proposal_id = 0;
  SpinWLockGuard guard(rw_lock_);
  if (OB_FAIL(get_sys_palf_role_and_epoch(role, proposal_id))) {
    LOG_WARN("fail to get role and proposal id", KR(ret));
  } else if (!is_leader_like(role)) {
    // DO NOT use is_strong_leader(), because standby cluster's role is STANDBY_LEADER
    ret = OB_LS_NOT_LEADER;
    LOG_WARN("local is not sys leader", KR(ret), K(role));
  } else if (OB_FAIL(init_sequence_id_(proposal_id))) {
    LOG_WARN("fail to init sequence id", KR(ret), K(proposal_id));
  // Reset the local DDL epoch so the next DDL transaction persists a fresh epoch.
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::rootserver::ObLocalManagementService>()->get_schema_service().get_ddl_epoch_mgr().remove_all_ddl_epoch())) {
    LOG_WARN("fail to remove ddl epoch", KR(ret));
  } else {
    ATOMIC_SET(&is_ddl_service_started_, true);
  }
  return ret;
}
} // end namespace rootserver
} // end namespace oceanbase
