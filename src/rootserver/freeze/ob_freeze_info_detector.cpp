#include "rootserver/ob_local_management_service.h"
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
#include "share/ob_internal_table_change_notifier.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "rootserver/ob_thread_idling.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "share/rc/ob_server_runtime.h"

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
    last_schedule_ts_(0), need_immediate_run_(true),
    core_table_change_seq_(0), freeze_info_change_seq_(0),
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
    is_global_merge_info_adjusted_ = false;
    last_gc_timestamp_ = ObTimeUtility::current_time();
    sql_proxy_ = &sql_proxy;
    major_merge_info_mgr_ = &major_merge_info_mgr;
    major_scheduler_idling_ = &major_scheduler_idling;
    if (OB_FAIL(timer_.init("FrzInfoDetTimer", ObMemAttr("FrzInfoDet")))) {
      LOG_WARN("init freeze info detector timer failed", KR(ret));
    } else if (OB_FAIL(
        ObInternalTableChangeNotifier::get_instance().register_table(
            OB_ALL_CORE_TABLE_TID))) {
      LOG_WARN("register core table change tracking failed", KR(ret));
    } else if (OB_FAIL(
        ObInternalTableChangeNotifier::get_instance().register_table(
            OB_ALL_FREEZE_INFO_TID))) {
      LOG_WARN("register freeze info change tracking failed", KR(ret));
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
    SERVER_MODULE_SCOPE {
      const int64_t start_time_us = ObTimeUtil::current_time();
      LOG_INFO("start freeze_info_detector");
      update_last_run_timestamp_();
      ObCurTraceId::init(GCONF.self_addr_);
      LOG_TRACE("run freeze info detector");

      bool can_work = false;
      if (OB_FAIL(can_start_work(can_work))) {
        LOG_WARN("fail to judge can start work", KR(ret));
      } else if (can_work) {
          if (is_primary_service()) {
            if (OB_FAIL(try_renew_snapshot_gc_scn())) {
              LOG_WARN("fail to renew gc snapshot", KR(ret), K_(is_primary_service));
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
          if (is_primary_service() && need_check_snapshot_gc_scn(start_time_us)) {
            if (OB_FAIL(major_merge_info_mgr_->check_snapshot_gc_scn())) {
              LOG_WARN("fail to check_snapshot_gc_ts", KR(ret));
            }
          }

          ret = OB_SUCCESS;
          if (OB_FAIL(try_reload_merge_info())) {
            LOG_WARN("fail to reload merge info", KR(ret));
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
  int ret = OB_SUCCESS;
  int64_t now = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if ((now - last_gc_timestamp_) < MODIFY_GC_SNAPSHOT_INTERVAL) {
    // nothing
  } else if (OB_FAIL(major_merge_info_mgr_->renew_snapshot_gc_scn())) {
    LOG_WARN("fail to renew snapshot gc scn", KR(ret));
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
  if (OB_FAIL(GCTX.local_management_service_->root_minor_freeze(arg))) {
    LOG_WARN("fail to execute root_minor_freeze rpc", KR(ret), K(arg));
  } else {
    LOG_INFO("succ to execute root_minor_freeze rpc", KR(ret), K(arg));
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_reload_merge_info()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(major_merge_info_mgr_->try_reload_merge_info())) {
    LOG_WARN("fail to reload merge info", KR(ret));
  }
  return ret;
}

int ObMajorMergeInfoDetector::can_start_work(bool &can_work)
{
  int ret = OB_SUCCESS;
  can_work = true;
  share::schema::ObSchemaGetterGuard schema_guard;
  const ObSimpleServerRuntimeSchema *runtime_schema = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is nullptr", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_server_runtime_info(runtime_schema))) {
    LOG_WARN("fail to get runtime schema", KR(ret));

  // Freeze information is refreshed only while the runtime schema is normal.
  } else if ((nullptr == runtime_schema) || !runtime_schema->is_normal()) {
    LOG_INFO("runtime is in abnormal status, no need to detect now", KPC(runtime_schema));
    can_work = false;
  } else {
    // Bootstrap initializes the global snapshot GC SCN after the runtime becomes normal.
    // Wait for that initialization to avoid racing it.
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
  is_paused_ = false;
  is_inited_ = false;
  sql_proxy_ = nullptr;
  major_merge_info_mgr_ = nullptr;
  major_scheduler_idling_ = nullptr;
  return ret;
}

int ObMajorMergeInfoDetector::try_reload_freeze_info()
{
  int ret = OB_SUCCESS;
  if (!is_primary_service()) {
    ObInternalTableChangeNotifier &notifier =
        ObInternalTableChangeNotifier::get_instance();
    uint64_t target_core_seq = 0;
    uint64_t target_freeze_seq = 0;
    int seq_ret = notifier.get_change_seq(
        OB_ALL_CORE_TABLE_TID, target_core_seq);
    if (OB_SUCCESS == seq_ret) {
      seq_ret = notifier.get_change_seq(
          OB_ALL_FREEZE_INFO_TID, target_freeze_seq);
    }
    if (OB_ISNULL(major_merge_info_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to try reload freeze info, freeze info manager is null", KR(ret),
               K_(is_primary_service));
    } else if (OB_SUCCESS == seq_ret
               && target_core_seq == core_table_change_seq_
               && target_freeze_seq == freeze_info_change_seq_) {
      // Unchanged standby merge metadata needs no inner SQL.
    } else if (OB_FAIL(major_merge_info_mgr_->reload(
                   true /* force_reload_global_info */))) {
      LOG_WARN("fail to reload freeze_info", KR(ret), K_(is_primary_service));
    } else if (OB_SUCCESS == seq_ret) {
      core_table_change_seq_ = target_core_seq;
      freeze_info_change_seq_ = target_freeze_seq;
    }
  }
  return ret;
}

int ObMajorMergeInfoDetector::try_adjust_global_merge_info()
{
  int ret = OB_SUCCESS;
  bool is_initial = false;
  // Both primary and standby servers adjust global_merge_info to skip unnecessary major freezes.
  if (!is_global_merge_info_adjusted_) {
    if (OB_FAIL(check_global_merge_info(is_initial))) {
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

bool ObMajorMergeInfoDetector::need_check_snapshot_gc_scn(const int64_t start_time_us)
{
  const int64_t START_CHECK_INTERVAL_US = 10 * 60 * 1000 * 1000; // 10 min
  return (ObTimeUtility::current_time() - start_time_us) > START_CHECK_INTERVAL_US;
}

void ObMajorMergeInfoDetector::update_last_run_timestamp_()
{
  last_run_timestamp_ = ObTimeUtility::current_time();
}

} //end rootserver
} //end oceanbase
