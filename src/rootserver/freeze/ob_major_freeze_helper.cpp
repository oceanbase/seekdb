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

#define USING_LOG_PREFIX RS_COMPACTION

#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_ex_rpc.h"
#include "share/ob_freeze_info_proxy.h"
#include "share/ob_share_util.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "storage/compaction/ob_tablet_scheduler.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace rootserver
{

int ObMajorFreezeHelper::major_freeze(const ObMajorFreezeParam &param)
{
  int ret = OB_SUCCESS;
  bool is_restore = false;
  bool is_primary_server = true;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(param), KR(ret));
  } else if (OB_FAIL(check_runtime_ready(is_restore))) {
    LOG_WARN("database runtime is not ready for major freeze", KR(ret), K(param));
  } else if (is_restore) {
    ret = OB_MAJOR_FREEZE_NOT_ALLOW;
    LOG_WARN("major freeze is not allowed while restoring", KR(ret));
  } else if (OB_FAIL(ObShareUtil::is_primary_server(is_primary_server))) {
    LOG_WARN("failed to read server role", KR(ret));
  } else if (!is_primary_server) {
    ret = OB_MAJOR_FREEZE_NOT_ALLOW;
    LOG_WARN("major freeze is not allowed on a standby server", KR(ret));
  } else if (OB_FAIL(do_local_major_freeze(param.freeze_reason_))) {
    LOG_WARN("failed to launch local major freeze", KR(ret), K(param));
  }
  return ret;
}

int ObMajorFreezeHelper::tablet_major_freeze(const ObTabletMajorFreezeParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else if (!GCONF.enable_major_freeze) {
    ret = OB_MAJOR_FREEZE_NOT_ALLOW;
    LOG_WARN("enable_major_freeze is off, refuse to to major_freeze", K(param), KR(ret));
  } else {
    LOG_INFO("tablet major freeze", K(ret), K(param));
    const int64_t start_time = ObTimeUtility::fast_current_time();
    ret = ex_rpc::sync_call([&]() -> int {
      int ret = OB_SUCCESS;
      SERVER_MODULE_SCOPE {
        if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()->user_request_schedule_medium_merge(
            param.tablet_id_))) {
          LOG_WARN("failed to try schedule tablet major freeze", K(ret), K(param));
        }
      }
      return ret;
    });
    const int64_t cost_time = ObTimeUtility::current_time() - start_time;
    LOG_INFO("tablet major freeze finished", KR(ret), K(param), K(cost_time));
  }
  return ret;
}

int ObMajorFreezeHelper::check_runtime_ready(bool &is_restore)
{
  int ret = OB_SUCCESS;
  is_restore = false;
  share::schema::ObSchemaGetterGuard schema_guard;
  const share::schema::ObSimpleServerRuntimeSchema *runtime_schema = nullptr;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema service is not initialized", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("failed to get runtime schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_server_runtime_info(runtime_schema))) {
    LOG_WARN("failed to get runtime schema", KR(ret));
  } else if (OB_ISNULL(runtime_schema) || !runtime_schema->is_normal()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("database runtime is not normal", KR(ret), KPC(runtime_schema));
  } else {
    is_restore = share::is_restore_role(GCTX.server_role_);
  }
  return ret;
}

int ObMajorFreezeHelper::do_local_major_freeze(const ObMajorFreezeReason freeze_reason)
{
  int ret = OB_SUCCESS;
  const int64_t launch_start_time = ObTimeUtility::current_time();
  SERVER_MODULE_SCOPE {
    ObPrimaryMajorFreezeService *primary_service = nullptr;
    ObRestoreMajorFreezeService *restore_service = nullptr;
    ObMajorFreezeService *major_freeze_service = nullptr;
    bool is_primary_service = true;
    if (OB_ISNULL(primary_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObPrimaryMajorFreezeService>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("primary major freeze service is null", KR(ret));
    } else if (OB_ISNULL(restore_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObRestoreMajorFreezeService>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("restore major freeze service is null", KR(ret));
    } else if (OB_FAIL(ObMajorFreezeUtil::get_major_freeze_service(
        primary_service, restore_service, major_freeze_service, is_primary_service))) {
      LOG_WARN("failed to select major freeze service", KR(ret));
    } else if (OB_ISNULL(major_freeze_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("major freeze service is null", KR(ret));
    } else if (!is_primary_service) {
      ret = OB_MAJOR_FREEZE_NOT_ALLOW;
      LOG_WARN("major freeze is forbidden while restoring", KR(ret));
    } else if (OB_FAIL(major_freeze_service->launch_major_freeze(freeze_reason))) {
      LOG_WARN("failed to launch major freeze", KR(ret));
    }
  }
  const int64_t launch_cost_time = ObTimeUtility::current_time() - launch_start_time;
  LOG_INFO("local major freeze finished", KR(ret), K(launch_cost_time));
  return ret;
}

int ObMajorFreezeHelper::suspend_merge()
{
  return do_admin_merge(AdminMergeType::SUSPEND);
}

int ObMajorFreezeHelper::resume_merge()
{
  return do_admin_merge(AdminMergeType::RESUME);
}

int ObMajorFreezeHelper::clear_merge_error()
{
  return do_admin_merge(AdminMergeType::CLEAR_ERROR);
}

int ObMajorFreezeHelper::do_admin_merge(const AdminMergeType admin_type)
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    ObPrimaryMajorFreezeService *primary_service = nullptr;
    ObRestoreMajorFreezeService *restore_service = nullptr;
    ObMajorFreezeService *major_freeze_service = nullptr;
    bool is_primary_service = true;
    if (OB_ISNULL(primary_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObPrimaryMajorFreezeService>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("major_freeze_service is nullptr", K(ret));
    } else if (OB_ISNULL(restore_service = ::oceanbase::share::server_service<::oceanbase::rootserver::ObRestoreMajorFreezeService>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("restore_major_freeze_service is nullptr", KR(ret));
    } else if (OB_FAIL(ObMajorFreezeUtil::get_major_freeze_service(primary_service,
        restore_service, major_freeze_service, is_primary_service))) {
      LOG_WARN("fail to get major freeze service", KR(ret));
    } else if (OB_ISNULL(major_freeze_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("major_freeze_service is null", KR(ret));
    } else {
      switch (admin_type) {
        case AdminMergeType::SUSPEND:
          if (OB_FAIL(major_freeze_service->suspend_merge())) {
            LOG_WARN("fail to suspend merge", KR(ret), K(is_primary_service));
          }
          break;
        case AdminMergeType::RESUME:
          if (OB_FAIL(major_freeze_service->resume_merge())) {
            LOG_WARN("fail to resume merge", KR(ret), K(is_primary_service));
          }
          break;
        case AdminMergeType::CLEAR_ERROR:
          if (OB_FAIL(major_freeze_service->clear_merge_error())) {
            LOG_WARN("fail to clear merge error", KR(ret), K(is_primary_service));
          }
          break;
        default:
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid merge admin type", KR(ret), K(admin_type));
          break;
      }
      if (OB_SUCC(ret)) {
        LOG_INFO("succeed to execute local merge admin", K(admin_type), K(is_primary_service));
      }
    }
  }

  return ret;
}

int ObMajorFreezeHelper::get_frozen_status(
    const SCN &frozen_scn,
    ObFreezeInfo &frozen_status)
{
  return get_frozen_status(frozen_scn, frozen_status, GCTX.sql_proxy_);
}

int ObMajorFreezeHelper::get_frozen_status(
    const SCN &frozen_scn,
    ObFreezeInfo &frozen_status,
    ObISQLClient *proxy)
{
  int ret = OB_SUCCESS;
  ObFreezeInfoProxy freeze_info_proxy;
  if (OB_ISNULL(proxy)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("SQL proxy is null", KR(ret));
  } else if (OB_FAIL(freeze_info_proxy.get_freeze_info(*proxy, frozen_scn, frozen_status))) {
    if (OB_ITER_END != ret && OB_TABLE_NOT_EXIST != ret) {
      LOG_WARN("get freeze info failed", KR(ret), K(frozen_scn));
    }
  }
  return ret;
}

int ObMajorFreezeHelper::get_frozen_scn(SCN &frozen_scn, ObISQLClient *proxy)
{
  int ret = OB_SUCCESS;
  ObFreezeInfo frozen_status;
  if (OB_FAIL(get_frozen_status(SCN::min_scn(), frozen_status, proxy))) {
    LOG_WARN("get latest freeze info failed", KR(ret));
  } else {
    frozen_scn = frozen_status.frozen_scn_;
  }
  return ret;
}

int ObMajorFreezeHelper::get_frozen_scn(SCN &frozen_scn)
{
  int ret = OB_SUCCESS;
  ObFreezeInfo frozen_status;
  if (OB_FAIL(get_frozen_status(SCN::min_scn(), frozen_status))) {
    if (OB_ITER_END != ret && OB_TABLE_NOT_EXIST != ret) {
      LOG_WARN("get latest freeze info failed", KR(ret));
    }
  } else {
    frozen_scn = frozen_status.frozen_scn_;
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
