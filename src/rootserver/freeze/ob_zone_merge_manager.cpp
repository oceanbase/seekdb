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

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_zone_merge_manager.h"
#include "share/ob_structured_event_logger.h" // for ROOTSERVICE_EVENT_ADD
#include "share/ob_global_merge_table_operator.h"
#include "share/ob_tablet_meta_table_compaction_operator.h"
#include "rootserver/freeze/ob_major_freeze_util.h"
#include "share/ob_freeze_info_proxy.h"

namespace oceanbase
{
namespace rootserver
{
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::palf;

ObZoneMergeManagerBase::ObZoneMergeManagerBase()
  : lock_(ObLatchIds::ZONE_MERGE_MANAGER_READ_LOCK),
    is_inited_(false), is_loaded_(false), zone_merge_info_(),
    global_merge_info_(), proxy_(NULL)
{}

int ObZoneMergeManagerBase::init(ObMySQLProxy &proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    proxy_ = &proxy;
    is_inited_ = true;
    is_loaded_ = false;
  }
  return ret;
}

int ObZoneMergeManagerBase::reload()
{
  int ret = OB_SUCCESS;

  LOG_INFO("start to reload zone_merge_mgr", K_(is_loaded), K_(global_merge_info),
           K_(zone_merge_info));
  HEAP_VAR(ObGlobalMergeInfo, global_merge_info) {
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", KR(ret));
    } else if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(*proxy_,
                          global_merge_info, true/*print_sql*/))) {
      LOG_WARN("fail to get global merge info", KR(ret));
    } else {
      reset_merge_info_without_lock();
      if (OB_FAIL(global_merge_info_.assign(global_merge_info))) {
        LOG_WARN("fail to assign", KR(ret), K(global_merge_info));
      } else if (OB_FAIL(restore_local_merge_info(global_merge_info, zone_merge_info_))) {
        LOG_WARN("fail to restore local merge info", KR(ret), K(global_merge_info));
      }
    }

    if (OB_SUCC(ret)) {
      is_loaded_ = true;
      LOG_INFO("succ to reload zone merge manager", K_(global_merge_info),
               K_(zone_merge_info));
    } else {
      LOG_WARN("fail to reload zone merge manager", KR(ret));
    }
  }
  return ret;
}

int ObZoneMergeManagerBase::restore_local_merge_info(
    const ObGlobalMergeInfo &global_info,
    ObZoneMergeInfo &zone_info) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!global_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid global merge info", KR(ret), K(global_info));
  } else {
    // Local zone progress is deliberately transient in seekdb.  A VERIFYING
    // round has finished compaction and only waits for global checksum work;
    // an unfinished MERGING round is restored from the last durable global
    // completion point so the idempotent local compaction can be rescheduled.
    const bool local_round_complete = global_info.is_last_merge_complete()
                                      || global_info.is_in_verifying_status();
    const SCN &recovered_scn = local_round_complete
                               ? global_info.global_broadcast_scn()
                               : global_info.last_merged_scn();
    zone_info.reset();
    zone_info.is_merging_.set_val(0, false);
    zone_info.broadcast_scn_.set_scn(recovered_scn, false);
    zone_info.last_merged_scn_.set_scn(recovered_scn, false);
    zone_info.all_merged_scn_.set_scn(recovered_scn, false);
    zone_info.frozen_scn_.set_scn(recovered_scn, false);
    zone_info.last_merged_time_.set_val(global_info.last_merged_time_.get_value(), false);
    zone_info.merge_start_time_.set_val(global_info.merge_start_time_.get_value(), false);
    zone_info.merge_status_.set_val(ObZoneMergeInfo::MERGE_STATUS_IDLE, false);
  }
  return ret;
}

int ObZoneMergeManagerBase::try_reload()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (is_loaded_) {
    if (TC_REACH_TIME_INTERVAL(5 * 60 * 1000 * 1000)) { // 5min
      FLOG_INFO("zone_merge_mgr is already loaded", K_(global_merge_info),
                K_(zone_merge_info));
    }
  } else if (OB_FAIL(reload())) {
    LOG_WARN("fail to reload", KR(ret));
  }
  return ret;
}

void ObZoneMergeManagerBase::reset_merge_info_without_lock()
{
  zone_merge_info_.reset();
  global_merge_info_.reset();
  is_loaded_ = false;
}

void ObZoneMergeManagerBase::reset_merge_info()
{
  SpinWLockGuard guard(lock_);
  reset_merge_info_without_lock();
}

int ObZoneMergeManagerBase::check_inner_stat() const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_ || !is_loaded_)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner_stat_error", K_(is_inited), K_(is_loaded), KR(ret));
  }
  return ret;
}

int ObZoneMergeManagerBase::get_zone_merge_info(ObZoneMergeInfo &info) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else if (OB_FAIL(get_zone_merge_info(GCTX.config_->zone.str(), info))) {
    LOG_WARN("fail to get zone", KR(ret));
  }
  return ret;
}

int ObZoneMergeManagerBase::get_zone_merge_info(const ObZone &zone, ObZoneMergeInfo &info) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_valid(zone))) {
    LOG_WARN("fail to check valid", KR(ret), K(zone));
  } else if (OB_FAIL(info.assign(zone_merge_info_))) {
    LOG_WARN("fail to assign", KR(ret), K_(zone_merge_info));
  }

  return ret;
}

int ObZoneMergeManagerBase::get_zone(ObIArray<ObZone> &zone_list) const
{
  int ret = OB_SUCCESS;
  zone_list.reset();
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else if (OB_FAIL(zone_list.push_back(GCTX.config_->zone.str()))) {
    LOG_WARN("fail to push back zone", KR(ret));
  }
  return ret;
}

int ObZoneMergeManagerBase::get_snapshot(
    ObGlobalMergeInfo &global_merge_info,
    ObIArray<ObZoneMergeInfo> &info_array)
{
  int ret = OB_SUCCESS;
  global_merge_info.reset();
  info_array.reset();
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(global_merge_info.assign(global_merge_info_))) {
    LOG_WARN("fail to assign", KR(ret), K_(global_merge_info));
  } else if (OB_FAIL(info_array.push_back(zone_merge_info_))) {
    LOG_WARN("fail to push zone_merge_info", KR(ret), K_(zone_merge_info));
  }
  return ret;
}

int ObZoneMergeManagerBase::get_snapshot(
    ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  global_merge_info.reset();
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(global_merge_info.assign(global_merge_info_))) {
    LOG_WARN("fail to assign", KR(ret), K_(global_merge_info));
  }
  return ret;
}

int ObZoneMergeManagerBase::start_zone_merge(
    const ObZone &zone)
{
  int ret = OB_SUCCESS;
  const int64_t cur_time = ObTimeUtility::current_time();
  FREEZE_TIME_GUARD;

  if (OB_FAIL(check_valid(zone))) {
    LOG_WARN("fail to check valid", KR(ret), K(zone));
  } else if (zone_merge_info_.broadcast_scn() >=
             global_merge_info_.global_broadcast_scn()) {
    ret = OB_ERR_SYS;
    LOG_ERROR("broadcast_scn must not larger than global_broadcast_scn",
              "zone broadcast_scn", zone_merge_info_.broadcast_scn(),
              "global_broadcast_scn", global_merge_info_.global_broadcast_scn(),
              KR(ret), K(zone));
  } else if (zone_merge_info_.frozen_scn() >=
             global_merge_info_.frozen_scn()) {
    ret = OB_ERR_SYS;
    LOG_ERROR("frozen_scn must not larger than global_frozen_scn",
              "zone frozen_scn", zone_merge_info_.frozen_scn(),
              "global_frozen_scn", global_merge_info_.frozen_scn(),
              KR(ret), K(zone));
  } else {
    const int64_t is_merging = 1;
    const bool need_update = true;
    ObZoneMergeInfo tmp_info;
    if (OB_FAIL(tmp_info.assign_value(zone_merge_info_))) {
      LOG_WARN("fail to assign zone merge info", KR(ret), K_(zone_merge_info));
    } else {
      tmp_info.is_merging_.set_val(is_merging, need_update);
      tmp_info.merge_start_time_.set_val(cur_time, need_update);
      tmp_info.merge_status_.set_val(ObZoneMergeInfo::MERGE_STATUS_MERGING, need_update);
      tmp_info.broadcast_scn_.set_scn(global_merge_info_.global_broadcast_scn(), need_update);
      tmp_info.frozen_scn_.set_scn(global_merge_info_.frozen_scn(), need_update);

      if (OB_FAIL(zone_merge_info_.assign_value(tmp_info))) {
        LOG_WARN("fail to assign zone merge info", KR(ret), K(tmp_info));
      } else {
        LOG_INFO("succ to update zone merge info", "latest zone merge_info", tmp_info);
      }
    }
  }
  LOG_INFO("start zone merge", KR(ret), K(zone), "global_broadcast_scn",
    global_merge_info_.global_broadcast_scn());
  return ret;
}

int ObZoneMergeManagerBase::finish_zone_merge(
    const ObZone &zone,
    const SCN &new_last_merged_scn,
    const SCN &new_all_merged_scn)
{
  int ret = OB_SUCCESS;
  const int64_t cur_time = ObTimeUtility::current_time();
  FREEZE_TIME_GUARD;

  if (OB_FAIL(check_valid(zone))) {
    LOG_WARN("fail to check valid", KR(ret), K(zone));
  } else if ((!new_last_merged_scn.is_valid()) || (!new_all_merged_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(zone),
             K(new_last_merged_scn), K(new_all_merged_scn));
  } else if (new_last_merged_scn > zone_merge_info_.broadcast_scn()) {
    // do nothing, this zone may not execute current round major
  } else if (new_last_merged_scn < zone_merge_info_.last_merged_scn()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid merged_scn", KR(ret), K(zone),
              K(new_last_merged_scn), K(new_all_merged_scn),
              K_(zone_merge_info));
  } else if (new_last_merged_scn == zone_merge_info_.last_merged_scn()) {
    LOG_INFO("zone merge already finished", K(zone), K(new_last_merged_scn));
  } else {
    ObZoneMergeInfo tmp_info;
    if (OB_FAIL(tmp_info.assign_value(zone_merge_info_))) {
      LOG_WARN("fail to assign zone merge info", KR(ret), K_(zone_merge_info));
    } else {
      ObZoneMergeInfo::MergeStatus status = static_cast<ObZoneMergeInfo::MergeStatus>(
        zone_merge_info_.merge_status_.value_);
      const int64_t is_merging = 0;
      tmp_info.is_merging_.set_val(is_merging, true);
      tmp_info.last_merged_scn_.set_scn(new_last_merged_scn, true);
      tmp_info.last_merged_time_.set_val(cur_time, true);
      status = ObZoneMergeInfo::MERGE_STATUS_IDLE;
      tmp_info.merge_status_.set_val(status, true);

      if (new_all_merged_scn > zone_merge_info_.all_merged_scn()) {
        tmp_info.all_merged_scn_.set_scn(new_all_merged_scn, true);
      }

      if (OB_FAIL(zone_merge_info_.assign_value(tmp_info))) {
        LOG_WARN("fail to assign zone merge info", KR(ret), K(tmp_info));
      } else {
        LOG_INFO("succ to update zone merge info", "latest zone merge_info", tmp_info);
      }
    }
  }

  LOG_INFO("finish zone merge", KR(ret), K(zone), K(new_last_merged_scn), K(new_all_merged_scn),
    K_(zone_merge_info));
  return ret;
}

int ObZoneMergeManagerBase::finish_all_zone_merge(
    const uint64_t &merged_scn_val)
{
  int ret = OB_SUCCESS;
  share::SCN merged_scn;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(merged_scn.convert_for_inner_table_field(merged_scn_val))) {
    LOG_WARN("failed to convert scn", K(ret), K(merged_scn_val));
  } else if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.config_));
  } else if (OB_FAIL(finish_zone_merge(GCTX.config_->zone.str(), merged_scn, merged_scn))) {
    LOG_WARN("failed to finish zone merge", KR(ret));
  }
  return ret;
}

int ObZoneMergeManagerBase::suspend_merge()
{
  int ret = OB_SUCCESS;
  const bool is_suspend = true;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(suspend_or_resume_zone_merge(is_suspend))) {
    LOG_WARN("fail to suspend merge", KR(ret), K(is_suspend));
  }
  return ret;
}

int ObZoneMergeManagerBase::resume_merge()
{
  int ret = OB_SUCCESS;
  const bool is_suspend = false;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(suspend_or_resume_zone_merge(is_suspend))) {
    LOG_WARN("fail to resume merge", KR(ret), K(is_suspend));
  }
  return ret;
}

int ObZoneMergeManagerBase::set_merge_status(
    const int64_t error_type)
{
  int ret = OB_SUCCESS;

  if ((error_type >= ObZoneMergeInfo::ERROR_TYPE_MAX)
      || (error_type < ObZoneMergeInfo::NONE_ERROR)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(error_type));
  } else {
    int64_t is_merge_error = 1;
    if (error_type == ObZoneMergeInfo::NONE_ERROR) {
      is_merge_error = 0;
    }

    FREEZE_TIME_GUARD;
    if (OB_FAIL(check_inner_stat())) {
      LOG_WARN("fail to check inner stat", KR(ret));
    } else {
      ObGlobalMergeInfo tmp_global_info;
      if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
      } else {
        tmp_global_info.is_merge_error_.set_val(is_merge_error, true);
        tmp_global_info.error_type_.set_val(error_type, true);

        FREEZE_TIME_GUARD;
        if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_,
            tmp_global_info))) {
          LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
        } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
          LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
        } else {
          LOG_INFO("succ to update global merge info", "latest global merge_info", tmp_global_info);
        }
      }
    }

    if (OB_SUCC(ret)) {
      LOG_INFO("succ to set merge status", K(error_type), K(global_merge_info_.is_merge_error_));
      ROOTSERVICE_EVENT_ADD("daily_merge", "set_merge_error", K(is_merge_error), K(error_type));
    }

  }
  return ret;
}

int ObZoneMergeManagerBase::set_zone_merging(
    const ObZone &zone)
{
  int ret = OB_SUCCESS;
  FREEZE_TIME_GUARD;
  if (OB_FAIL(check_valid(zone))) {
    LOG_WARN("fail to check valid", KR(ret), K(zone));
  } else {
    const int64_t is_merging = 1;
    ObZoneMergeInfo tmp_info;
    if (OB_FAIL(tmp_info.assign_value(zone_merge_info_))) {
      LOG_WARN("fail to assign zone merge info", KR(ret), K_(zone_merge_info));
    } else if (is_merging != zone_merge_info_.is_merging_.get_value()) {
      tmp_info.is_merging_.set_val(is_merging, true);

      if (OB_FAIL(zone_merge_info_.assign_value(tmp_info))) {
        LOG_WARN("fail to assign zone merge info", KR(ret), K(tmp_info));
      } else {
        LOG_INFO("succ to update zone merge info", "latest zone merge_info", tmp_info);
      }
    }
  }

  LOG_INFO("set zone merging", KR(ret), K(zone));
  return ret;
}

int ObZoneMergeManagerBase::check_need_broadcast(
    const SCN &frozen_scn,
    bool &need_broadcast)
{
  int ret = OB_SUCCESS;
  need_broadcast = false;
  if (OB_UNLIKELY(!frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(frozen_scn));
  } else if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if ((global_merge_info_.frozen_scn() < frozen_scn)
             && GCONF.enable_major_freeze) { // require enable_major_freeze = true
    need_broadcast = true;
  }
  return ret;
}

int ObZoneMergeManagerBase::set_global_freeze_info(
    const SCN &frozen_scn)
{
  int ret = OB_SUCCESS;

  bool need_broadcast = false;
  if (OB_FAIL(check_need_broadcast(frozen_scn, need_broadcast))) {
    LOG_WARN("fail to check_need_broadcast", KR(ret), K(frozen_scn));
  } else if (!need_broadcast) {
    LOG_INFO("no need set global freeze info", K(frozen_scn), K_(global_merge_info));
  } else {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret));
    } else {
      tmp_global_info.frozen_scn_.set_scn(frozen_scn, true);
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_,
          tmp_global_info))) {
        LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      } else {
        LOG_INFO("succ to update global merge info", "latest global merge_info", tmp_global_info);
      }
    }
  }

  LOG_INFO("finish set global freeze info", KR(ret), K(frozen_scn), K(need_broadcast));
  return ret;
}

int ObZoneMergeManagerBase::get_global_broadcast_scn(SCN &global_broadcast_scn) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_broadcast_scn = global_merge_info_.global_broadcast_scn();
  }
  return ret;
}

int ObZoneMergeManagerBase::get_global_last_merged_scn(SCN &global_last_merged_scn) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_last_merged_scn =  global_merge_info_.last_merged_scn();
  }
  return ret;
}

int ObZoneMergeManagerBase::get_global_merge_status(ObZoneMergeInfo::MergeStatus &global_merge_status) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_merge_status = (ObZoneMergeInfo::MergeStatus)(global_merge_info_.merge_status_.value_);
  }
  return ret;
}

int ObZoneMergeManagerBase::get_global_last_merged_time(int64_t &global_last_merged_time) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_last_merged_time = global_merge_info_.last_merged_time_.get_value();
  }
  return ret;
}

int ObZoneMergeManagerBase::get_global_merge_start_time(int64_t &global_merge_start_time) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_merge_start_time = global_merge_info_.merge_start_time_.get_value();
  }
  return ret;
}

int ObZoneMergeManagerBase::generate_next_global_broadcast_scn(
    SCN &next_scn)
{
  int ret = OB_SUCCESS;
  FREEZE_TIME_GUARD;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (global_merge_info_.is_merge_error()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("should not be is_merge_error", KR(ret), K_(global_merge_info));
  } else if (global_merge_info_.last_merged_scn() < global_merge_info_.global_broadcast_scn()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("not merged yet", "last_merged_scn", global_merge_info_.last_merged_scn(),
             "global_broadcast_scn", global_merge_info_.global_broadcast_scn(), KR(ret));
  } else if (global_merge_info_.last_merged_scn() > global_merge_info_.global_broadcast_scn()) {
    ret = OB_ERR_SYS;
    LOG_ERROR("last_merged_scn must not larger than global_broadcast_scn", KR(ret), "last_merged_scn", global_merge_info_.last_merged_scn(),
              "global_broadcast_scn", global_merge_info_.global_broadcast_scn());
  } else {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else {
      if (global_merge_info_.global_broadcast_scn() < global_merge_info_.frozen_scn()) {
        // only when global_broadcast_scn is less than global frozen_scn, we can use
        // frozen_scn to start major_freeze
        next_scn = global_merge_info_.frozen_scn();
        tmp_global_info.global_broadcast_scn_.set_scn(next_scn, true);
        const int64_t cur_time = ObTimeUtility::current_time();
        tmp_global_info.merge_start_time_.set_val(cur_time, true);
      } else if (global_merge_info_.global_broadcast_scn() == global_merge_info_.frozen_scn()) {
        next_scn = global_merge_info_.global_broadcast_scn();
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("global_broadcast_scn must not larger than global frozen_scn", KR(ret),
          K_(global_merge_info));
      }

      if (OB_SUCC(ret)) {
        LOG_INFO("next global_broadcast_scn", K(next_scn), K(tmp_global_info));

        tmp_global_info.merge_status_.set_val(ObZoneMergeInfo::MERGE_STATUS_MERGING, true);
        FREEZE_TIME_GUARD;
        if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_,
            tmp_global_info))) {
          LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
        } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
          LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
        } else {
          LOG_INFO("succ to update global merge info", "latest global merge_info", tmp_global_info);
        }
      }
    }
  }

  return ret;
}

// if all zones finished merge & checksum checking, we may need to update global merge info
int ObZoneMergeManagerBase::try_update_global_last_merged_scn()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    // here, we don't check is_merge_error, cuz merge & chcksum already finished.
    // we need to do some update work at last. is_merge_error will be caught in next round
    if (global_merge_info_.is_in_merge()) {
      FREEZE_TIME_GUARD;
      // after all zones finished merge, update global merge info
      ObGlobalMergeInfo tmp_global_info;
      if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
      } else {
        const int64_t cur_time = ObTimeUtility::current_time();
        tmp_global_info.last_merged_time_.set_val(cur_time, true);
        tmp_global_info.last_merged_scn_.set_scn(global_merge_info_.global_broadcast_scn(), true);
        tmp_global_info.merge_status_.set_val(ObZoneMergeInfo::MERGE_STATUS_IDLE, true);

        FREEZE_TIME_GUARD;
        if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_,
            tmp_global_info))) {
          LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
        } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
          LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
        } else {
          LOG_INFO("succ to update global merge info", "latest global merge_info", tmp_global_info);
        }
      }
    }
  }
  return ret;
}

// after finishing merge(before checksum checking), update global merge info
int ObZoneMergeManagerBase::update_global_merge_info_after_merge()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (global_merge_info_.is_in_verifying_status()) {
    LOG_INFO("already in verifying status, no need to update global merge status again",
             "global merge status", global_merge_info_.merge_status_);
  } else if (global_merge_info_.is_merge_error()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("should not update global merge status, cuz is_merge_error is true", KR(ret), K_(global_merge_info));
  } else {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else {
      tmp_global_info.merge_status_.set_val(ObZoneMergeInfo::MERGE_STATUS_VERIFYING, true);
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_,
          tmp_global_info))) {
        LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      } else {
        LOG_INFO("succ to update global merge info", "latest global merge_info", tmp_global_info);
      }
    }
  }
  return ret;
}

int ObZoneMergeManagerBase::adjust_global_merge_info()
{
  int ret = OB_SUCCESS;
  ObFreezeInfo max_frozen_status;
  ObFreezeInfoProxy freeze_info_proxy{};
  SCN min_compaction_scn;
  SCN max_frozen_scn;
  // 1. get min{compaction_scn} of all tablets in __all_tablet_meta_table
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (!GCTX.is_shared_storage_mode()
          && OB_FAIL(ObTabletMetaTableCompactionOperator::get_min_compaction_scn(min_compaction_scn))) {
    LOG_WARN("fail to get min_compaction_scn", KR(ret));
  } else if (OB_UNLIKELY(min_compaction_scn < SCN::base_scn())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected min_compaction_scn", KR(ret), K(min_compaction_scn));
  } else if (min_compaction_scn == SCN::base_scn()) {
    // do nothing. no need to adjust global_merge_info
  } else if (min_compaction_scn > SCN::base_scn()) {
    /*  case 1 : min{compaction_scn} is a medium scn
     *  return max{frozen_scn} which is smaller than or equal to curr medium scn from __all_freeze_info
     *  case 2 : min{compaction_scn} is a tenant major scn
     *  max{frozen_scn} must be equal to min{compaction_scn}, return max{frozen_scn}
     */
    if (OB_FAIL(freeze_info_proxy.get_max_frozen_scn_smaller_or_equal_than(*proxy_,
                min_compaction_scn, max_frozen_scn))) {
      LOG_WARN("fail to get max frozen_scn smaller than or equal to min_compaction_scn", KR(ret), K(min_compaction_scn));
    } else if (max_frozen_scn < SCN::base_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected max_frozen_scn", KR(ret), K(max_frozen_scn));
    } else if (max_frozen_scn == SCN::base_scn()) {
      // do nothing. no need to adjust global_merge_info
    } else if (max_frozen_scn > SCN::base_scn()) {
      // 3. if max{frozen_scn} > 1, update __all_merge_info and global_merge_info with max{frozen_scn}
      if (OB_FAIL(inner_adjust_global_merge_info(max_frozen_scn))) {
        LOG_WARN("fail to inner adjust global merge info", KR(ret), K(max_frozen_scn));
      }
    }
  }
  FLOG_INFO("finish to adjust global merge info", K(min_compaction_scn), K(max_frozen_scn), K_(global_merge_info));
  return ret;
}

int ObZoneMergeManagerBase::check_valid(const ObZone &zone) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret), K(zone));
  } else if (zone.is_empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(zone));
  }
  return ret;
}

int ObZoneMergeManagerBase::suspend_or_resume_zone_merge(
    const bool is_suspend)
{
  int ret = OB_SUCCESS;

  ObGlobalMergeInfo tmp_global_info;
  if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
    LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
  } else {
    tmp_global_info.suspend_merging_.set_val(is_suspend, true);
    if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_, tmp_global_info))) {
      LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
    } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
    } else {
      LOG_INFO("succ to update global merge info", "latest global merge_info", tmp_global_info);
    }
  }

  return ret;
}

int ObZoneMergeManagerBase::inner_adjust_global_merge_info(
    const SCN &frozen_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(frozen_scn));
  } else {
    // 1. adjust global_merge_info in memory to control the frozen_scn of the next major compaction.
    // 2. adjust global_merge_info in table for background thread to update report_scn.
    //
    // Note that, here not only adjust last_merged_scn, but also adjust global_broadcast_scn and
    // frozen_scn. So as to avoid error in ObMajorMergeScheduler::do_work(), which works based on
    // these global_merge_info in memory.
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else {
      tmp_global_info.frozen_scn_.set_scn(frozen_scn, true);
      tmp_global_info.global_broadcast_scn_.set_scn(frozen_scn, true);
      tmp_global_info.last_merged_scn_.set_scn(frozen_scn, true);
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(*proxy_, tmp_global_info))) {
        LOG_WARN("fail to update partial global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global_merge_info", KR(ret), K(tmp_global_info), K_(global_merge_info));
      } else {
        LOG_INFO("succ to update global_merge_info", K(tmp_global_info), K_(global_merge_info));
      }
    }
  }
  return ret;
}

// only used for copying data to/from shadow_
int ObZoneMergeManagerBase::copy_infos(
    ObZoneMergeManagerBase &dest,
    const ObZoneMergeManagerBase &src)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dest.zone_merge_info_.assign(src.zone_merge_info_))) {
    LOG_WARN("fail to assign local merge info", KR(ret), K_(src.zone_merge_info));
  } else if (OB_FAIL(dest.global_merge_info_.assign(src.global_merge_info_))) {
    LOG_WARN("fail to assign global merge info", KR(ret), K_(src.global_merge_info));
  } else {
    dest.is_inited_ = src.is_inited_;
    dest.is_loaded_ = src.is_loaded_;
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
ObZoneMergeManager::ObZoneMergeMgrGuard::ObZoneMergeMgrGuard(
    const SpinRWLock &lock,
    ObZoneMergeManagerBase &zone_merge_mgr,
    ObZoneMergeManagerBase &shadow,
    int &ret)
    :  lock_(const_cast<SpinRWLock &>(lock)), zone_merge_mgr_(zone_merge_mgr),
       shadow_(shadow), ret_(ret)
{
  SpinRLockGuard copy_guard(lock_);
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_SUCCESS != ret_)) {
  } else if (OB_UNLIKELY(OB_SUCCESS !=
      (tmp_ret = ObZoneMergeManager::copy_infos(shadow_, zone_merge_mgr_)))) {
    LOG_WARN("fail to copy to zone_merge_mgr shadow", K(tmp_ret), K_(ret));
  }
  if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
    ret_ = tmp_ret;
  }
}

ObZoneMergeManager::ObZoneMergeMgrGuard::~ObZoneMergeMgrGuard()
{
  SpinWLockGuard copy_guard(lock_);
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_SUCCESS != ret_)) {
  } else if (OB_UNLIKELY(OB_SUCCESS !=
      (tmp_ret = ObZoneMergeManager::copy_infos(zone_merge_mgr_, shadow_)))) {
    LOG_WARN_RET(tmp_ret, "fail to copy from zone_merge_mgr shadow", K(tmp_ret), K_(ret));
  }
  if (OB_UNLIKELY(OB_SUCCESS != tmp_ret)) {
    ret_ = tmp_ret;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
ObZoneMergeManager::ObZoneMergeManager()
  : write_lock_(ObLatchIds::ZONE_MERGE_MANAGER_WRITE_LOCK), shadow_()
{}

ObZoneMergeManager::~ObZoneMergeManager()
{}

int ObZoneMergeManager::init(ObMySQLProxy &proxy)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObZoneMergeManagerBase::init(proxy))) {
    LOG_WARN("fail to init zone_merge_manager_base", KR(ret));
  } else if (OB_FAIL(shadow_.init(proxy))) {
    LOG_WARN("fail to init zone_merge_mgr_base shadow_", KR(ret));
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
