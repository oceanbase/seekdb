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
#include "storage/compaction/ob_tenant_freeze_info_mgr.h"
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
    is_primary_active_(false), need_primary_catchup_(false),
    is_global_merge_info_adjusted_(false), is_gc_scn_inited_(false), sql_proxy_(nullptr),
    last_gc_renew_attempt_ts_(0), first_pending_snapshot_gc_history_scn_(0),
    last_run_timestamp_(0),
    major_merge_info_mgr_(nullptr), major_scheduler_idling_(nullptr),
    last_schedule_ts_(0), need_immediate_run_(true),
    snapshot_gc_role_lock_(common::ObLatchIds::MAJOR_FREEZE_SWITCH_LOCK),
    timer_()
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
    ATOMIC_STORE(&is_primary_active_, false);
    ATOMIC_STORE(&need_primary_catchup_, false);
    is_global_merge_info_adjusted_ = false;
    ATOMIC_STORE(&last_gc_renew_attempt_ts_, 0);
    ATOMIC_STORE(&first_pending_snapshot_gc_history_scn_, 0);
    sql_proxy_ = &sql_proxy;
    major_merge_info_mgr_ = &major_merge_info_mgr;
    major_scheduler_idling_ = &major_scheduler_idling;
    if (OB_FAIL(timer_.init("FrzInfoDetTimer", ObMemAttr("FrzInfoDet")))) {
      LOG_WARN("init freeze info detector timer failed", KR(ret));
    } else {
      is_inited_ = true;
      LOG_INFO("freeze info detector init succ");
    }
  }
  return ret;
}

int ObMajorMergeInfoDetector::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObMajorMergeInfoDetector not init", K(ret));
  } else if (OB_FAIL(timer_.start())) {
    LOG_WARN("start freeze info detector timer failed", KR(ret));
  } else if (OB_FAIL(timer_.schedule(*this, 1 * 1000 * 1000L, true/*is_repeat*/))) {
    LOG_WARN("schedule freeze info detector timer failed", KR(ret));
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
    const bool need_snapshot_gc_run = need_renew_snapshot_gc_scn_(now);
    if (!ATOMIC_LOAD(&need_immediate_run_)
        && !need_snapshot_gc_run
        && now < ATOMIC_LOAD(&last_schedule_ts_) + get_schedule_interval()) {
      return;
    }
    ATOMIC_STORE(&need_immediate_run_, false);
    ATOMIC_STORE(&last_schedule_ts_, now);
    MOD_SCOPE {
      LOG_INFO("start freeze_info_detector");
      update_last_run_timestamp_();
      ObCurTraceId::init(GCONF.self_addr_);
      LOG_TRACE("run freeze info detector");

      bool can_work = false;
      bool skip_refresh_zone_info = false;

      if (OB_FAIL(can_start_work(can_work))) {
        LOG_WARN("fail to judge can start work", KR(ret));
      } else if (can_work) {
          if (is_primary_service()) {
            if (OB_FAIL(try_renew_snapshot_gc_scn())) {
              if (REACH_TIME_INTERVAL(60 * 1000 * 1000L)) {
                LOG_WARN("fail to renew gc snapshot", KR(ret), K_(is_primary_service));
              }
            }
          }

          ret = OB_SUCCESS;
          if (OB_FAIL(try_reload_freeze_info())) {
            LOG_WARN("fail to try reload freeze info", KR(ret), K_(is_primary_service));
          }

          bool need_broadcast = false;
          ret = OB_SUCCESS;
          if (OB_FAIL(check_need_broadcast(need_broadcast))) {
            LOG_WARN("fail to check need broadcast", KR(ret));
          }

          if (need_broadcast) {
            ret = OB_SUCCESS;
            if (OB_FAIL(try_minor_freeze())) {
              LOG_WARN("fail to try minor freeze", KR(ret));
            }

            ret = OB_SUCCESS;
            if (OB_FAIL(try_broadcast_freeze_info())) {
              LOG_WARN("fail to broadcast freeze info", KR(ret));
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
            LOG_WARN("fail to try update zone info", KR(ret));
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
    LOG_WARN("fail to try adjust global merge info", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_->check_need_broadcast(need_broadcast))) {
    LOG_WARN("fail to check need broadcast", KR(ret));
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
    LOG_WARN("fail to broadcast_frozen_info", KR(ret));
  } else {
    major_scheduler_idling_->wakeup();
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_renew_snapshot_gc_scn()
{
  ObRecursiveMutexGuard role_guard(snapshot_gc_role_lock_);
  int ret = OB_SUCCESS;
  const int64_t now = ObTimeUtility::current_time();
  storage::ObTenantFreezeInfoMgr *freeze_info_mgr = nullptr;
  int64_t pending_history_scn = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!ATOMIC_LOAD(&is_primary_active_)) {
    // nothing
  } else if (OB_ISNULL(share::g_mp)
      || OB_ISNULL(freeze_info_mgr = share::g_mp->tenant_freeze_info_mgr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant freeze info mgr is null", KR(ret));
  } else if (!need_renew_snapshot_gc_scn_(now)) {
    // IDLE or waiting for the first pending history SCN to reach undo_retention.
  } else {
    const bool need_primary_catchup = ATOMIC_LOAD(&need_primary_catchup_);
    SCN new_snapshot_gc_scn;
    if (!need_primary_catchup) {
      // The retention deadline has been reached. Later retries use the fixed interval.
      ATOMIC_STORE(&first_pending_snapshot_gc_history_scn_, 0);
    }
    ATOMIC_STORE(&last_gc_renew_attempt_ts_, now);
    if (OB_FAIL(major_merge_info_mgr_->renew_snapshot_gc_scn(new_snapshot_gc_scn))) {
      // Keep ACTIVE and retry after the same fixed interval.
    } else {
      if (need_primary_catchup && ATOMIC_LOAD(&is_primary_active_)) {
        const int64_t primary_history_scn = new_snapshot_gc_scn.get_val_for_tx();
        freeze_info_mgr->notify_snapshot_gc_history_created(primary_history_scn);
        ATOMIC_STORE(&first_pending_snapshot_gc_history_scn_, primary_history_scn);
        ATOMIC_STORE(&last_gc_renew_attempt_ts_, 0);
        ATOMIC_STORE(&need_primary_catchup_, false);
      }
      pending_history_scn = freeze_info_mgr->get_pending_snapshot_gc_history_scn();
      if (pending_history_scn > 0) {
        const int64_t undo_retention_s = GCONF.undo_retention;
        const int64_t gc_boundary = MAX(0,
            new_snapshot_gc_scn.get_val_for_tx()
                - undo_retention_s * 1000L * 1000L * 1000L);
        if (gc_boundary >= pending_history_scn
            && freeze_info_mgr->try_clear_pending_snapshot_gc_history_scn(
                pending_history_scn)) {
          ATOMIC_STORE(&first_pending_snapshot_gc_history_scn_, 0);
          ATOMIC_STORE(&last_gc_renew_attempt_ts_, 0);
          LOG_INFO("snapshot gc history event is covered",
              K(new_snapshot_gc_scn), K(gc_boundary), K(pending_history_scn),
              K(undo_retention_s));
        }
      }
    }
  }
  return ret;
}

bool ObMajorMergeInfoDetector::is_snapshot_gc_history_due_(
    const int64_t current_time_ns,
    const int64_t first_pending_history_scn,
    const int64_t undo_retention_s)
{
  const int64_t undo_retention_ns = undo_retention_s * 1000L * 1000L * 1000L;
  return current_time_ns > 0
      && first_pending_history_scn > 0
      && undo_retention_s >= 0
      && MAX(0, current_time_ns - undo_retention_ns) >= first_pending_history_scn;
}

int64_t ObMajorMergeInfoDetector::latch_first_pending_snapshot_gc_history_scn_(
    const int64_t pending_history_scn)
{
  int64_t first_pending_history_scn =
      ATOMIC_LOAD(&first_pending_snapshot_gc_history_scn_);
  if (first_pending_history_scn <= 0 && pending_history_scn > 0) {
    (void)ATOMIC_BCAS(&first_pending_snapshot_gc_history_scn_,
        0, pending_history_scn);
    first_pending_history_scn =
        ATOMIC_LOAD(&first_pending_snapshot_gc_history_scn_);
  }
  return first_pending_history_scn;
}

bool ObMajorMergeInfoDetector::need_renew_snapshot_gc_scn_(const int64_t now)
{
  ObRecursiveMutexGuard role_guard(snapshot_gc_role_lock_);
  storage::ObTenantFreezeInfoMgr *freeze_info_mgr = nullptr;
  const int64_t last_attempt_ts = ATOMIC_LOAD(&last_gc_renew_attempt_ts_);
  bool need_renew = false;
  if (is_primary_service() && ATOMIC_LOAD(&is_primary_active_)) {
    if (ATOMIC_LOAD(&need_primary_catchup_)) {
      need_renew = last_attempt_ts <= 0
          || now >= last_attempt_ts + UPDATER_INTERVAL_US;
    } else if (OB_NOT_NULL(share::g_mp)
        && OB_NOT_NULL(freeze_info_mgr = share::g_mp->tenant_freeze_info_mgr())) {
      const int64_t pending_history_scn =
          freeze_info_mgr->get_pending_snapshot_gc_history_scn();
      if (pending_history_scn > 0) {
        if (last_attempt_ts > 0) {
          need_renew = now >= last_attempt_ts + UPDATER_INTERVAL_US;
        } else {
          const int64_t first_pending_history_scn =
              latch_first_pending_snapshot_gc_history_scn_(pending_history_scn);
          need_renew = is_snapshot_gc_history_due_(
              ObTimeUtility::current_time_ns(), first_pending_history_scn,
              GCONF.undo_retention);
        }
      }
    }
  }
  return need_renew;
}

int ObMajorMergeInfoDetector::on_become_primary()
{
  ObRecursiveMutexGuard role_guard(snapshot_gc_role_lock_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!is_primary_service()) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("only primary service can become primary", KR(ret));
  } else if (ATOMIC_LOAD(&is_primary_active_)) {
    LOG_INFO("snapshot gc detector is already primary active");
  } else {
    ATOMIC_STORE(&need_primary_catchup_, true);
    ATOMIC_STORE(&last_gc_renew_attempt_ts_, 0);
    ATOMIC_STORE(&first_pending_snapshot_gc_history_scn_, 0);
    ATOMIC_STORE(&is_primary_active_, true);
    if (OB_FAIL(signal())) {
      LOG_WARN("fail to signal detector after becoming primary", KR(ret));
    } else {
      LOG_INFO("snapshot gc detector becomes primary");
    }
  }
  return ret;
}

void ObMajorMergeInfoDetector::pause()
{
  ObRecursiveMutexGuard role_guard(snapshot_gc_role_lock_);
  ATOMIC_STORE(&is_primary_active_, false);
  ATOMIC_STORE(&is_paused_, true);
}

void ObMajorMergeInfoDetector::resume()
{
  ObRecursiveMutexGuard role_guard(snapshot_gc_role_lock_);
  ATOMIC_STORE(&is_paused_, false);
}

int ObMajorMergeInfoDetector::try_minor_freeze()
{
  int ret = OB_SUCCESS;
  ObAddr rs_addr = GCTX.self_addr();
  obcall::ObRootMinorFreezeArg arg;
  if (OB_FAIL(GCTX.root_service_->root_minor_freeze(arg))) {
    LOG_WARN("fail to execute root_minor_freeze rpc", KR(ret), K(arg));
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
    LOG_WARN("fail to try update zone info", KR(ret));
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
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_info(tenant_schema))) {
    LOG_WARN("fail to get simple tenant schema", KR(ret));

  // 1. only normal state tenant schema need refresh freeze_info;
  // 2. common tenant(except sys tenant) init snapshot_gc_ts complete(in set_tenant_init_global_stat),
  //    when tenant schema is noraml state, so can start work directly;
  } else if ((nullptr == tenant_schema) || !tenant_schema->is_normal()) {
    LOG_INFO("tenant is in abnormal status, no need detect now", KPC(tenant_schema));
    can_work = false;
  } else {
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
    timer_.stop();
  }
}

void ObMajorMergeInfoDetector::wait()
{
  if (is_inited_) {
    timer_.wait();
  }
}

int ObMajorMergeInfoDetector::destroy()
{
  int ret = OB_SUCCESS;
  stop();
  wait();
  if (is_inited_) {
    timer_.destroy();
  }
  ATOMIC_STORE(&is_paused_, false);
  ATOMIC_STORE(&is_primary_active_, false);
  ATOMIC_STORE(&need_primary_catchup_, false);
  ATOMIC_STORE(&last_gc_renew_attempt_ts_, 0);
  ATOMIC_STORE(&first_pending_snapshot_gc_history_scn_, 0);
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
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().check_tenant_is_restore(
                     NULL, is_restore))) {
    LOG_WARN("fail to check tenant restore", KR(ret));
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_reload_freeze_info()
{
  int ret = OB_SUCCESS;
  if (need_reload_freeze_info_(is_primary_service())) {
    bool is_restore = false;
    if (OB_FAIL(check_tenant_is_restore(is_restore))) {
      LOG_WARN("fail to check tenant is restore", KR(ret), K_(is_primary_service));
    } else if (is_restore) {
      LOG_INFO("skip restoring tenant to reload freeze_info", K(is_restore),
               K_(is_primary_service));
    } else if (OB_ISNULL(major_merge_info_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to try reload freeze info, freeze info manager is null", KR(ret),
               K_(is_primary_service));
    } else if (OB_FAIL(major_merge_info_mgr_->reload())) {
      LOG_WARN("fail to reload freeze_info", KR(ret), K_(is_primary_service));
    }
  }
  return ret;
}

bool ObMajorMergeInfoDetector::need_reload_freeze_info_(
    const bool is_primary_service)
{
  return !is_primary_service;
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
      LOG_WARN("fail to check tenant is restore", KR(ret), K_(is_primary_service));
    } else if (is_restore) {
      LOG_INFO("skip restoring tenant to adjust global merge info",
               K(is_restore), K_(is_primary_service));
    } else if (OB_FAIL(check_global_merge_info(is_initial))) {
      LOG_WARN("fail to check global merge info", KR(ret), K_(is_primary_service));
    } else if (!is_initial) {
      // avoid check again, e.g., when switch leader
      is_global_merge_info_adjusted_ = true;
    } else if (OB_ISNULL(major_merge_info_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to try adjust global merge info, freeze info manager is null", KR(ret),
               K_(is_primary_service));
    } else if (OB_FAIL(major_merge_info_mgr_->adjust_global_merge_info())) {
      LOG_WARN("fail to adjust global merge info", KR(ret), K_(is_primary_service));
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
      LOG_WARN("fail to get global merge info", KR(ret), K_(is_primary_service));
    } else if ((global_merge_info.last_merged_scn_.get_scn().is_base_scn()) &&
               (global_merge_info.global_broadcast_scn_.get_scn().is_base_scn()) &&
               (global_merge_info.frozen_scn_.get_scn().is_base_scn())) {
      is_initial = true;
    }
  }
  return ret;
}

void ObMajorMergeInfoDetector::update_last_run_timestamp_()
{
  last_run_timestamp_ = ObTimeUtility::current_time();
}

} //end rootserver
} //end oceanbase
