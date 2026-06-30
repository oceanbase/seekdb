#include "rootserver/ob_root_service.h"
#include "share/rc/ob_module_provider.h"
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

#include "rootserver/freeze/ob_freeze_info_detector.h"

#include "rootserver/freeze/ob_major_merge_info_manager.h"
#include "rootserver/ob_root_utils.h"
#include "share/ob_global_merge_table_operator.h"
#include "share/ob_global_stat_proxy.h"
#include "rootserver/ob_thread_idling.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace rootserver
{
ObMajorMergeInfoDetector::ObMajorMergeInfoDetector()
  : is_inited_(false), is_paused_(false), is_primary_service_(true),
    is_global_merge_info_adjusted_(false), is_gc_scn_inited_(false), sql_proxy_(nullptr), last_gc_timestamp_(0), last_run_timestamp_(0),
    major_merge_info_mgr_(nullptr), major_scheduler_idling_(nullptr),
    last_schedule_ts_(0), need_immediate_run_(true)
{}

ObMajorMergeInfoDetector::~ObMajorMergeInfoDetector()
{
  (void)destroy();
}

int ObMajorMergeInfoDetector::init(
    const bool is_primary_service,
    ObMySQLProxy &sql_proxy,
    ObMajorMergeInfoManager &major_merge_info_mgr,
    ObThreadIdling &major_scheduler_idling)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    is_primary_service_ = is_primary_service;
    is_global_merge_info_adjusted_ = false;
    last_gc_timestamp_ = ObTimeUtility::current_time();
    sql_proxy_ = &sql_proxy;
    major_merge_info_mgr_ = &major_merge_info_mgr;
    major_scheduler_idling_ = &major_scheduler_idling;
    is_inited_ = true;
    LOG_INFO("freeze info detector init succ");
  }
  return ret;
}

int ObMajorMergeInfoDetector::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObMajorMergeInfoDetector not init", K(ret));
  } else if (OB_FAIL(TG_START(lib::TGDefIDs::FrzInfoDetTimer))) {
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::FrzInfoDetTimer, *this, 1 * 1000 * 1000L, true/*is_repeat*/))) {
  } else {
    LOG_INFO("ObMajorMergeInfoDetector start succ");
  }
  return ret;
}

ERRSIM_POINT_DEF(SKIP_REFRESH_ZONE_INFO)
void ObMajorMergeInfoDetector::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (is_paused()) {
    update_last_run_timestamp_();
  } else {
    const int64_t now = ObTimeUtility::current_time();
    if (!ATOMIC_LOAD(&need_immediate_run_)
        && now < ATOMIC_LOAD(&last_schedule_ts_) + get_schedule_interval()) {
      return;
    }
    ATOMIC_STORE(&need_immediate_run_, false);
    ATOMIC_STORE(&last_schedule_ts_, now);
    MOD_SCOPE {
      const int64_t start_time_us = ObTimeUtil::current_time();
      LOG_INFO("start freeze_info_detector");
      update_last_run_timestamp_();
      ObCurTraceId::init(GCONF.self_addr_);
      LOG_TRACE("run freeze info detector");

      bool can_work = false;
      bool skip_refresh_zone_info = false;
      int64_t proposal_id = 0;
      ObRole role = ObRole::INVALID_ROLE;

      if (OB_FAIL(obtain_proposal_id_from_ls(is_primary_service_, proposal_id, role))) {
      } else if (ObRole::LEADER != role) {
        LOG_INFO("follower should not run freeze_info_detector", K(role),
                 K_(is_primary_service));
      } else if (OB_FAIL(can_start_work(can_work))) {
      } else if (can_work) {
          if (is_primary_service()) {
            if (OB_FAIL(try_renew_snapshot_gc_scn())) {
            }
          }

          ret = OB_SUCCESS;
          if (OB_FAIL(try_reload_freeze_info())) {
          }

          bool need_broadcast = false;
          ret = OB_SUCCESS;
          if (OB_FAIL(check_need_broadcast(need_broadcast))) {
          }

          if (need_broadcast) {
            ret = OB_SUCCESS;
            if (OB_FAIL(try_minor_freeze())) {
            }

            ret = OB_SUCCESS;
            if (OB_FAIL(try_broadcast_freeze_info())) {
            }
          }

          ret = OB_SUCCESS;
          if (is_primary_service() && need_check_snapshot_gc_scn(start_time_us)) {
            if (OB_FAIL(major_merge_info_mgr_->check_snapshot_gc_scn())) {
            }
          }

          ret = OB_SUCCESS;
#ifdef ERRSIM
          if (OB_UNLIKELY(SKIP_REFRESH_ZONE_INFO)) {
            skip_refresh_zone_info = true;
            LOG_INFO("ERRSIM SKIP_REFRESH_ZONE_INFO", K(ret));
            ret = OB_SUCCESS;
          }
#endif
          if (OB_FAIL(!skip_refresh_zone_info && try_update_zone_info())) {
          }
        }
    }
  }
  LOG_INFO("stop freeze_info_detector");
}

int ObMajorMergeInfoDetector::check_need_broadcast(bool &need_broadcast)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(try_adjust_global_merge_info())) {
  } else if (OB_FAIL(major_merge_info_mgr_->check_need_broadcast(need_broadcast))) {
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_broadcast_freeze_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_->broadcast_freeze_info())) {
  } else {
    major_scheduler_idling_->wakeup();
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_renew_snapshot_gc_scn()
{
  int ret = OB_SUCCESS;
  int64_t now = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if ((now - last_gc_timestamp_) < MODIFY_GC_SNAPSHOT_INTERVAL) {
    // nothing
  } else if (OB_FAIL(major_merge_info_mgr_->renew_snapshot_gc_scn())) {
  } else {
    last_gc_timestamp_ = now;
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_minor_freeze()
{
  int ret = OB_SUCCESS;
  ObAddr rs_addr = GCTX.self_addr();
  obcall::ObRootMinorFreezeArg arg;
  if (OB_FAIL(GCTX.root_service_->root_minor_freeze(arg))) {
  } else {
    LOG_INFO("succ to execute root_minor_freeze rpc", KR(ret), K(arg));
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_update_zone_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_->try_update_zone_info())) {
  }
  return ret;
}

int ObMajorMergeInfoDetector::can_start_work(bool &can_work)
{
  int ret = OB_SUCCESS;
  can_work = true;
  share::schema::ObSchemaGetterGuard schema_guard;
  const ObSimpleTenantSchema *tenant_schema = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is nullptr", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
  } else if ((nullptr == tenant_schema) || !tenant_schema->is_normal()) {
    LOG_INFO("tenant is in abnormal status, no need detect now", KPC(tenant_schema));
    can_work = false;
  } else if (true) {
    // 3. sys tenant init global stat(snpshot_gc_ts) in ObBootstrap(ObBootstrap::init_global_stat()),
    //    after tenant_state set to normal;
    //    in order to avoid racing, detector will wait, until global_stat init complete;
    if (is_gc_scn_inited_) {
      // ...
    } else {
      SCN snapshot_gc_scn;
      ObGlobalStatProxy global_stat_proxy(*sql_proxy_);
      if (OB_FAIL(global_stat_proxy.get_snapshot_gc_scn(snapshot_gc_scn))) {
        LOG_WARN("can not get snapshot gc ts", KR(ret));
        ret = OB_SUCCESS;
        can_work = false;
      } else {
        LOG_INFO("snapshot_gc_scn init succ", K(snapshot_gc_scn));
        is_gc_scn_inited_ = true;
      }
    }
  }
  return ret;
}

int64_t ObMajorMergeInfoDetector::get_schedule_interval() const
{
  return UPDATER_INTERVAL_US;
}

int ObMajorMergeInfoDetector::signal()
{
  ATOMIC_STORE(&need_immediate_run_, true);
  return OB_SUCCESS;
}

void ObMajorMergeInfoDetector::stop()
{
  if (is_inited_) {
    TG_STOP(lib::TGDefIDs::FrzInfoDetTimer);
  }
}

void ObMajorMergeInfoDetector::wait()
{
  if (is_inited_) {
    TG_WAIT(lib::TGDefIDs::FrzInfoDetTimer);
  }
}

int ObMajorMergeInfoDetector::destroy()
{
  int ret = OB_SUCCESS;
  stop();
  wait();
  if (is_inited_) {
    TG_DESTROY(lib::TGDefIDs::FrzInfoDetTimer);
  }
  is_paused_ = false;
  is_inited_ = false;
  sql_proxy_ = nullptr;
  major_merge_info_mgr_ = nullptr;
  major_scheduler_idling_ = nullptr;
  return ret;
}

int ObMajorMergeInfoDetector::check_tenant_is_restore(
    bool &is_restore)
{
  int ret = OB_SUCCESS;
  is_restore = false;
  if (OB_UNLIKELY(!true)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().check_tenant_is_restore(
                     NULL, is_restore))) {
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_reload_freeze_info()
{
  int ret = OB_SUCCESS;
  if (!is_primary_service()) {
    bool is_restore = false;
    if (OB_FAIL(check_tenant_is_restore(is_restore))) {
    } else if (is_restore) {
      LOG_INFO("skip restoring tenant to reload freeze_info", K(is_restore),
               K_(is_primary_service));
    } else if (OB_ISNULL(major_merge_info_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to try reload freeze info, freeze info manager is null", KR(ret),
               K_(is_primary_service));
    } else if (OB_FAIL(major_merge_info_mgr_->reload())) {
    }
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_adjust_global_merge_info()
{
  int ret = OB_SUCCESS;
  bool is_initial = false;
  // both primary and standby tenants should adjust global_merge_info to skip unnecessary major freeze
  // primary tenants: 
  // standby tenants: 
  if (!is_global_merge_info_adjusted_) {
    bool is_restore = false;
    if (OB_FAIL(check_tenant_is_restore(is_restore))) {
    } else if (is_restore) {
      LOG_INFO("skip restoring tenant to adjust global merge info",
               K(is_restore), K_(is_primary_service));
    } else if (OB_FAIL(check_global_merge_info(is_initial))) {
    } else if (!is_initial) {
      // avoid check again, e.g., when switch leader
      is_global_merge_info_adjusted_ = true;
    } else if (OB_ISNULL(major_merge_info_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to try adjust global merge info, freeze info manager is null", KR(ret),
               K_(is_primary_service));
    } else if (OB_FAIL(major_merge_info_mgr_->adjust_global_merge_info())) {
    } else {
      is_global_merge_info_adjusted_ = true;
      LOG_INFO("succ to adjust global merge info", K_(is_primary_service));
    }
  }
  return ret;
}

int ObMajorMergeInfoDetector::check_global_merge_info(bool &is_initial) const
{
  int ret = OB_SUCCESS;
  is_initial = false;
  HEAP_VAR(ObGlobalMergeInfo, global_merge_info) {
    if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(*sql_proxy_, global_merge_info))) {
    } else if ((global_merge_info.last_merged_scn_.get_scn().is_base_scn()) &&
               (global_merge_info.global_broadcast_scn_.get_scn().is_base_scn()) &&
               (global_merge_info.frozen_scn_.get_scn().is_base_scn())) {
      is_initial = true;
    }
  }
  return ret;
}

bool ObMajorMergeInfoDetector::need_check_snapshot_gc_scn(const int64_t start_time_us)
{
  const int64_t START_CHECK_INTERVAL_US = 10 * 60 * 1000 * 1000; // 10 min
  return (ObTimeUtility::current_time() - start_time_us) > START_CHECK_INTERVAL_US;
}

int ObMajorMergeInfoDetector::obtain_proposal_id_from_ls(
    const bool is_primary_service,
    int64_t &proposal_id,
    ObRole &role)
{
  int ret = OB_SUCCESS;
  storage::ObLSHandle ls_handle;
  logservice::ObLogHandler *handler = nullptr;
  if (OB_ISNULL(share::g_mp->ls_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls service is null", KR(ret));
  } else if (OB_FAIL(share::g_mp->ls_service()->get_ls(SYS_LS, ls_handle, ObLSGetMod::RS_MOD))) {
  } else if (OB_ISNULL(ls_handle.get_ls())
      || OB_ISNULL(handler = ls_handle.get_ls()->get_log_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("should not null", KR(ret), K(is_primary_service));
  } else if (OB_FAIL(handler->get_role(role, proposal_id))) {
  }
  return ret;
}

void ObMajorMergeInfoDetector::update_last_run_timestamp_()
{
  last_run_timestamp_ = ObTimeUtility::current_time();
}

} //end rootserver
} //end oceanbase
