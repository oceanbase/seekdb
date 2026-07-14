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
#include "share/rc/ob_module_provider.h"
#include "share/ob_freeze_info_proxy.h"
#include "share/location_cache/ob_location_service.h"
#include "src/observer/ob_srv_network_frame.h"
#include "share/ob_ex_rpc.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"

namespace oceanbase
{
using namespace share;
namespace rootserver
{

int ObMajorFreezeParam::add_freeze_info(
    )
{
  int ret = OB_SUCCESS;
  obcall::ObSimpleFreezeInfo info;
  if (OB_FAIL(freeze_info_array_.push_back(info))) {
    LOG_WARN("fail to push_back", K(info));
  }

  return ret;
}

///////////////////////////////////////////////////////////////////////////////

int ObMajorFreezeHelper::major_freeze(const ObMajorFreezeParam &param)
{
  ObArray<int> merge_results;
  return major_freeze(param, merge_results);
}

int ObMajorFreezeHelper::major_freeze(
    const ObMajorFreezeParam &param,
    ObIArray<int> &merge_results)
{
  int ret = OB_SUCCESS;
  ObSEArray<obcall::ObSimpleFreezeInfo, 32> freeze_info_array;
  bool want_to_freeze_all = (param.freeze_all_ || param.freeze_all_user_ || param.freeze_all_meta_);
  if (OB_UNLIKELY(!param.is_valid()
                  || (!want_to_freeze_all && param.freeze_info_array_.empty()))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(param), KR(ret));
  } else if (OB_FAIL(get_freeze_info(param, freeze_info_array))) {
    LOG_WARN("fail to get tenant id", KR(ret), K(param));
  } else if (!freeze_info_array.empty()) { // may be empty due to skipping restore and standby tenants
    if (OB_FAIL(do_major_freeze(param.freeze_reason_, freeze_info_array, merge_results))) {
      LOG_WARN("fail to do major freeze", KR(ret), K(freeze_info_array));
    }
  }
  return ret;
}

int ObMajorFreezeHelper::tablet_major_freeze(const ObTabletMajorFreezeParam &param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(param));
  } else if (OB_UNLIKELY(nullptr == GCTX.location_service_ || nullptr == GCTX.net_frame_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid GCTX", KR(ret));
  } else if (!GCONF.enable_major_freeze) {
    ret = OB_MAJOR_FREEZE_NOT_ALLOW;
    LOG_WARN("enable_major_freeze is off, refuse to to major_freeze", K(param), KR(ret));
  } else {
    LOG_INFO("tablet major freeze", K(ret), K(param));
    const int64_t start_time = ObTimeUtility::fast_current_time();
    
    share::ObLSID ls_id;
    bool is_cache_hit = false;

    if (OB_FAIL(GCTX.location_service_->get(
      param.tablet_id_, MAX_PROCESS_TIME_US, is_cache_hit, ls_id))) {
      LOG_WARN("failed to get ls_id", K(ret), K(param.tablet_id_));
    } else {
      // RPC removed: leader is self on single replica; dispatch in-process.
      // Mirrors ObService::tablet_major_freeze local logic.
      ret = ex_rpc::sync_call([&]() -> int {
        int ret = OB_SUCCESS;
        MOD_SCOPE {
          if (OB_FAIL(share::g_mp->tenant_tablet_scheduler()->user_request_schedule_medium_merge(
              ls_id, param.tablet_id_, param.is_rebuild_column_group_))) {
            LOG_WARN("failed to try schedule tablet major freeze", K(ret),
                K(ls_id), K(param));
          }
        }
        return ret;
      });
    }
    const int64_t cost_time = ObTimeUtility::current_time() - start_time;
    LOG_INFO("do tenant major freeze", KR(ret), K(ls_id), K(param), K(cost_time));
  }
  return ret;
}

int ObMajorFreezeHelper::get_freeze_info(
    const ObMajorFreezeParam &param,
    ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array)
{
  int ret = OB_SUCCESS;

  ObArray<obcall::ObSimpleFreezeInfo> tmp_info_array;
  bool is_primary_cluster = true;
  bool want_to_freeze_all = param.freeze_all_ || param.freeze_all_user_ || param.freeze_all_meta_;
  if (want_to_freeze_all) {
    if (OB_FAIL(get_specific_tenant_freeze_info(param.freeze_all_, param.freeze_all_user_, 
                                                param.freeze_all_meta_, tmp_info_array))) {
      LOG_WARN("fail to get specific tenant freeze info", KR(ret));
    }
  } else {
    if (OB_FAIL(tmp_info_array.assign(param.freeze_info_array_))) {
      LOG_WARN("fail to assign", K(param), KR(ret));
    }
  }

  if (OB_FAIL(ret)) {
  } else if (tmp_info_array.empty() && !want_to_freeze_all) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("freeze info array should not be empty", KR(ret), K(param));
  } else if (OB_FAIL(ObShareUtil::is_primary_cluster(is_primary_cluster))) {
    LOG_WARN("fail to check whether is primary cluster", KR(ret), K(is_primary_cluster));
  } else {
    const int64_t info_cnt = tmp_info_array.count();
    for (int64_t i = 0; OB_SUCC(ret) && (i < info_cnt); ++i) {
      bool is_restore = false;
      
      if (OB_FAIL(check_tenant_is_restore(is_restore))) {
        LOG_WARN("fail to check tenant is restore", KR(ret), K(i), "freeze_info", tmp_info_array.at(i));
      } else if (is_restore) {
        LOG_INFO("skip restoring tenant to do major freeze");
        const char *warn_buf = "tenant is in restore, major freeze is not allowed now";
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(add_user_warning(warn_buf))) {
          LOG_WARN("fail to add user warning", KR(tmp_ret));
        }
      }
      // Skip major freeze for standby tenants and thus avoid OB_MAJOR_FREEZE_NOT_ALLOW incurred by
      // standby tenants, only when launching major freeze on more than one tenant or all_user or all.
      else if (!is_primary_cluster && ((info_cnt > 1) || param.freeze_all_user_ || param.freeze_all_)) {
        LOG_INFO("skip major freeze for standby tenant", K(is_primary_cluster));
        const char *warn_buf = "standby tenant sync freeze info from primary tenant, not allowed to launch major freeze";
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(add_user_warning(warn_buf))) {
          LOG_WARN("fail to add user warning", KR(tmp_ret));
        }
      } else if (OB_FAIL(freeze_info_array.push_back(tmp_info_array.at(i)))) {
        LOG_WARN("fail to push back freeze info", KR(ret), K(i), "freeze_info", tmp_info_array.at(i));
      }
    }
  }

  return ret;
}

int ObMajorFreezeHelper::check_tenant_is_restore(
    bool &is_restore)
{
  int ret = OB_SUCCESS;
  is_restore = false;

  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().check_tenant_is_restore(NULL, is_restore))) {
    LOG_WARN("fail to check tenant restore", KR(ret));
  }
  return ret;
}

int ObMajorFreezeHelper::get_all_tenant_freeze_info(
    ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array)
{
  int ret = OB_SUCCESS;
  share::schema::ObSchemaGetterGuard schema_guard;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid GCTX", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else {
    
    // only launch major freeze for tenant whose status is normal, and skip major freeze for
    // tenant whose status is not normal.
    const ObSimpleTenantSchema *tenant_schema = nullptr;
    if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
      LOG_WARN("fail to get simple tenant schema", KR(ret));
    } else if (OB_ISNULL(tenant_schema)) {
      ret = OB_INNER_STAT_ERROR;
      LOG_WARN("tenant schema is null", KR(ret));
    } else if (OB_UNLIKELY(!tenant_schema->is_normal())) {
      LOG_WARN("tenant status is not normal, skip major freeze for this tenant",
               "status", tenant_schema->get_status());
    } else { // tenant_schema->is_normal()
      obcall::ObSimpleFreezeInfo info;
      if(OB_FAIL(freeze_info_array.push_back(info))) {
        LOG_WARN("fail to push back", KR(ret));
      }
    }
  }
  return ret;
}

int ObMajorFreezeHelper::get_specific_tenant_freeze_info( 
    bool freeze_all, 
    bool freeze_all_user, 
    bool freeze_all_meta, 
    common::ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array)
{
  int ret = OB_SUCCESS;
  ObSEArray<obcall::ObSimpleFreezeInfo, 32> tmp_freeze_info_array;
  if (OB_FAIL(get_all_tenant_freeze_info(tmp_freeze_info_array))) {
    LOG_WARN("fail to get all tenant freeze info", KR(ret));
  } else {
    using FUNC_TYPE = bool (*) ();
    FUNC_TYPE func = nullptr;
    // caller guarantees that at most one of freeze_all/freeze_all_user/freeze_all_meta is true.
    if (freeze_all || freeze_all_user) {
      func = is_user_tenant;
    } else {
      func = is_meta_tenant;
    }
    freeze_info_array.reset();
    for (int64_t i = 0; OB_SUCC(ret) && (i < tmp_freeze_info_array.count()); ++i) {
      if (func()) {
        if (OB_FAIL(freeze_info_array.push_back(tmp_freeze_info_array.at(i)))) {
          LOG_WARN("fail to push back freeze info", 
                    KR(ret), K(i), "freeze_info", tmp_freeze_info_array.at(i));
        }
      }
    }
  }
  return ret;
}

int ObMajorFreezeHelper::do_major_freeze(
    const ObMajorFreezeReason freeze_reason,
    const ObIArray<obcall::ObSimpleFreezeInfo> &freeze_info_array,
    ObIArray<int> &merge_results)
{
  int ret = OB_SUCCESS;
  int final_ret = OB_SUCCESS;
  if (freeze_info_array.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else {
    const int64_t tenant_count = freeze_info_array.count();
    for (int i = 0; (i < tenant_count) && OB_SUCC(ret); ++i) {
      
      if (OB_FAIL(do_one_tenant_major_freeze(freeze_reason, freeze_info_array.at(i)))) {
        if ((OB_MAJOR_FREEZE_NOT_FINISHED != ret) && (OB_FROZEN_INFO_ALREADY_EXIST != ret)) {
          final_ret = ret;
          LOG_ERROR("fail do tenant major freeze", KR(ret), K(tenant_count),
            "freeze_reason", major_freeze_reason_to_str(freeze_reason),
            "freeze_info", freeze_info_array.at(i));
        }
      }

      // push ret of 'do_one_tenant_major_freeze' into array. if fail, finish loop.
      if (OB_SUCCESS != (ret = merge_results.push_back(ret))) {
        final_ret = ret;
        LOG_WARN("fail to push back", KR(ret), K(tenant_count));
      }
    }
    ret = final_ret;
  }

  return ret;
}

int ObMajorFreezeHelper::do_one_tenant_major_freeze(
  const ObMajorFreezeReason freeze_reason,
  const obcall::ObSimpleFreezeInfo &freeze_info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!freeze_info.is_valid() || !is_valid_major_freeze_reason(freeze_reason))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(freeze_info), K(freeze_reason));
  } else {
    const int64_t launch_start_time = ObTimeUtility::current_time();

    // RPC removed: leader is self on single replica; dispatch in-process.
    // Mirrors ObTenantMajorFreezeP::process local logic.
    ret = ex_rpc::sync_call([&]() -> int {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        ObPrimaryMajorFreezeService *primary_service = nullptr;
        ObRestoreMajorFreezeService *restore_service = nullptr;
        ObMajorFreezeService *major_freeze_service = nullptr;
        bool is_primary_service = true;
        if (OB_ISNULL(primary_service = share::g_mp->primary_major_freeze_service())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("primary_major_freeze_service is nullptr", KR(ret), K(freeze_info));
        } else if (OB_ISNULL(restore_service = share::g_mp->restore_major_freeze_service())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("restore_major_freeze_service is nullptr", KR(ret), K(freeze_info));
        } else if (OB_FAIL(ObMajorFreezeUtil::get_major_freeze_service(primary_service,
            restore_service, major_freeze_service, is_primary_service))) {
          LOG_WARN("fail to get major freeze service", KR(ret), K(freeze_info));
        } else if (OB_ISNULL(major_freeze_service)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("major_freeze_service is null", KR(ret), K(freeze_info));
        } else if (is_primary_service) {
          if (OB_FAIL(major_freeze_service->launch_major_freeze(freeze_reason))) {
            if (OB_MAJOR_FREEZE_NOT_FINISHED != ret && (OB_FROZEN_INFO_ALREADY_EXIST != ret)) {
              LOG_WARN("fail to launch_major_freeze", KR(ret), K(freeze_info), K(is_primary_service));
            }
          } else {
            LOG_INFO("launch_major_freeze succ", K(freeze_info), K(is_primary_service));
          }
        } else {
          ret = OB_MAJOR_FREEZE_NOT_ALLOW;
          LOG_ERROR("fail to launch_major_freeze, forbidden in restore_major_freeze_service",
              KR(ret), K(freeze_info), K(is_primary_service));
        }
      }
      return ret;
    });

    const int64_t launch_cost_time = ObTimeUtility::current_time() - launch_start_time;
    LOG_INFO("do tenant major freeze", KR(ret), K(freeze_info), K(launch_cost_time));
  }
  // TODO oushen
  // 1. parallel major_freeze
  return ret;
}

int ObMajorFreezeHelper::suspend_merge(const ObTenantAdminMergeParam &param)
{
  return do_tenant_admin_merge(param, obcall::ObTenantAdminMergeType::SUSPEND_MERGE);
}

int ObMajorFreezeHelper::resume_merge(const ObTenantAdminMergeParam &param)
{
  return do_tenant_admin_merge(param, obcall::ObTenantAdminMergeType::RESUME_MERGE);
}

int ObMajorFreezeHelper::clear_merge_error(const ObTenantAdminMergeParam &param)
{
  return do_tenant_admin_merge(param, obcall::ObTenantAdminMergeType::CLEAR_MERGE_ERROR);
}

int ObMajorFreezeHelper::do_tenant_admin_merge(
    const ObTenantAdminMergeParam &param,
    const obcall::ObTenantAdminMergeType &admin_type)
{
  int ret = OB_SUCCESS;
  ObSEArray<obcall::ObSimpleFreezeInfo, 32> freeze_info_array;
  bool want_to_freeze_all = false;
  if (OB_UNLIKELY(!param.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(param), KR(ret));
  } else {
    want_to_freeze_all = param.need_all_ || param.need_all_user_ || param.need_all_meta_;
    if (want_to_freeze_all) {
      if (OB_FAIL(get_specific_tenant_freeze_info(param.need_all_, param.need_all_user_, 
                                                  param.need_all_meta_, freeze_info_array))) {
        LOG_WARN("fail to get specific tenant freeze info", KR(ret));
      }
    } else {
      if (param.specified_) {
        obcall::ObSimpleFreezeInfo freeze_info;
        if (OB_FAIL(freeze_info_array.push_back(freeze_info))) {
          LOG_WARN("fail to push back freeze info", KR(ret), K(param));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(freeze_info_array.empty() && !want_to_freeze_all)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get valid tenant freeze info", KR(ret), K(admin_type));
  } else {
    int final_ret = OB_SUCCESS;
    for (int i = 0; (i < freeze_info_array.count()) && OB_SUCC(ret); ++i) {
      
      if (OB_FAIL(do_one_tenant_admin_merge(admin_type))) {
        LOG_WARN("fail do tenant admin merge", KR(ret), K(admin_type));
      }
      if (OB_FAIL(ret) && (OB_SUCCESS == final_ret)) {
        final_ret = ret;
      }
      // ignore ret, continue
      ret = OB_SUCCESS;
    }

    ret = final_ret;
  }

  return ret;
}

int ObMajorFreezeHelper::do_one_tenant_admin_merge(const obcall::ObTenantAdminMergeType &admin_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(false)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(admin_type));
  } else {
    // RPC removed: leader is self on single replica; dispatch in-process.
    // Mirrors ObTenantAdminMergeP::process local logic.
    ret = ex_rpc::sync_call([&]() -> int {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        ObPrimaryMajorFreezeService *primary_service = nullptr;
        ObRestoreMajorFreezeService *restore_service = nullptr;
        ObMajorFreezeService *major_freeze_service = nullptr;
        bool is_primary_service = true;
        if (OB_ISNULL(primary_service = share::g_mp->primary_major_freeze_service())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("major_freeze_service is nullptr", K(ret));
        } else if (OB_ISNULL(restore_service = share::g_mp->restore_major_freeze_service())) {
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
            case obcall::ObTenantAdminMergeType::SUSPEND_MERGE:
              if (OB_FAIL(major_freeze_service->suspend_merge())) {
                LOG_WARN("fail to suspend merge", KR(ret), K(is_primary_service));
              }
              break;
            case obcall::ObTenantAdminMergeType::RESUME_MERGE:
              if (OB_FAIL(major_freeze_service->resume_merge())) {
                LOG_WARN("fail to resume merge", KR(ret), K(is_primary_service));
              }
              break;
            case obcall::ObTenantAdminMergeType::CLEAR_MERGE_ERROR:
              if (OB_FAIL(major_freeze_service->clear_merge_error())) {
                LOG_WARN("fail to clear merge error", KR(ret), K(is_primary_service));
              }
              break;
            default:
              break;
          }
          if (OB_SUCC(ret)) {
            LOG_INFO("succ to execute tenant admin merge", K(admin_type), K(is_primary_service));
          }
        }
      }
      return ret;
    });

    LOG_INFO("finish to do tenant admin mrege", K(admin_type), KR(ret));
  }
  return ret;
}

int ObMajorFreezeHelper::get_frozen_status(
    const share::SCN &frozen_scn,
    share::ObFreezeInfo &frozen_status)
{
  return get_frozen_status(frozen_scn, frozen_status, GCTX.sql_proxy_);
}

int ObMajorFreezeHelper::get_frozen_status(
    const SCN &frozen_scn, 
    share::ObFreezeInfo &frozen_status,
    ObISQLClient *proxy)
{
  int ret = OB_SUCCESS;
  share::ObFreezeInfoProxy freeze_info_proxy{};

  if (OB_ISNULL(proxy) || !true) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid GCTX", KR(ret));
  } else if (OB_FAIL(freeze_info_proxy.get_freeze_info(*proxy, frozen_scn, frozen_status))) {
    if (OB_ITER_END != ret && OB_TABLE_NOT_EXIST != ret) {
      LOG_WARN("fail to get freeze info", KR(ret), K(frozen_scn));
    }
  }

  return ret;
}

int ObMajorFreezeHelper::get_frozen_scn(
    SCN &frozen_scn,
    ObISQLClient *proxy)
{
  int ret = OB_SUCCESS;
  share::ObFreezeInfo frozen_status;

  // use min_scn to get frozen_status, means get one with biggest frozen_scn
  if (OB_FAIL(get_frozen_status(SCN::min_scn(), frozen_status, proxy))) {
    LOG_WARN("fail to get frozen info", KR(ret));
  } else {
    frozen_scn = frozen_status.frozen_scn_;
  }

  return ret;
}

int ObMajorFreezeHelper::get_frozen_scn(
    SCN &frozen_scn)
{
  int ret = OB_SUCCESS;
  share::ObFreezeInfo frozen_status;

  // use min_scn to get frozen_status, means get one with biggest frozen_scn
  if (OB_FAIL(get_frozen_status(SCN::min_scn(), frozen_status))) {
    if (OB_ITER_END != ret && OB_TABLE_NOT_EXIST != ret) {
      LOG_WARN("fail to get frozen info", KR(ret));
    }
  } else {
    frozen_scn = frozen_status.frozen_scn_;
  }

  return ret;
}

int ObMajorFreezeHelper::add_user_warning(const char *buf)
{
  int ret = OB_SUCCESS;
  const int64_t MAX_WARNING_LEN = 1000;
  char warn_buf[1024] = { 0 };
  if (OB_UNLIKELY(!true) || OB_ISNULL(buf)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(buf));
  } else if (STRLEN(buf) > MAX_WARNING_LEN) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("buf is too long", KR(ret), "buf_len", STRLEN(buf), K(MAX_WARNING_LEN));
  } else if (OB_FAIL(databuff_printf(warn_buf, 1024, "[T1]%s", buf))) {
    LOG_WARN("fail to construct warn buf", KR(ret));
  } else {
    LOG_USER_WARN(OB_MAJOR_FREEZE_NOT_ALLOW, warn_buf);
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
