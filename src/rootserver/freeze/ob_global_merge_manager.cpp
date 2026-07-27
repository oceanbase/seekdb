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

#include "rootserver/freeze/ob_global_merge_manager.h"

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "rootserver/freeze/ob_major_freeze_util.h"
#include "share/ob_structured_event_logger.h"
#include "share/ob_freeze_info_proxy.h"
#include "share/ob_global_merge_table_operator.h"
#include "share/ob_server_struct.h"
#include "share/ob_tablet_meta_table_compaction_operator.h"

namespace oceanbase
{
namespace rootserver
{
using namespace oceanbase::common;
using namespace oceanbase::share;

ObGlobalMergeManagerBase::ObGlobalMergeManagerBase()
  : lock_(ObLatchIds::GLOBAL_MERGE_MANAGER_READ_LOCK),
    is_inited_(false),
    is_loaded_(false),
    global_merge_info_(),
    proxy_(nullptr)
{
}

int ObGlobalMergeManagerBase::init(ObMySQLProxy &proxy)
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

int ObGlobalMergeManagerBase::reload()
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ObGlobalMergeInfo, global_merge_info) {
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", KR(ret));
    } else if (OB_FAIL(ObGlobalMergeTableOperator::load_global_merge_info(
        *proxy_, global_merge_info, true /* print_sql */))) {
      LOG_WARN("fail to get global merge info", KR(ret));
    } else {
      reset_merge_info_without_lock();
      if (OB_FAIL(global_merge_info_.assign(global_merge_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(global_merge_info));
      } else {
        is_loaded_ = true;
        LOG_INFO("succeed to reload global merge manager", K_(global_merge_info));
      }
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::try_reload()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (is_loaded_) {
    if (TC_REACH_TIME_INTERVAL(5 * 60 * 1000 * 1000)) {
      FLOG_INFO("global merge manager is already loaded", K_(global_merge_info));
    }
  } else if (OB_FAIL(reload())) {
    LOG_WARN("fail to reload", KR(ret));
  }
  return ret;
}

void ObGlobalMergeManagerBase::reset_merge_info_without_lock()
{
  global_merge_info_.reset();
  is_loaded_ = false;
}

void ObGlobalMergeManagerBase::reset_merge_info()
{
  SpinWLockGuard guard(lock_);
  reset_merge_info_without_lock();
}

int ObGlobalMergeManagerBase::check_inner_stat() const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_ || !is_loaded_)) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("inner stat error", K_(is_inited), K_(is_loaded), KR(ret));
  }
  return ret;
}

int ObGlobalMergeManagerBase::get_snapshot(ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  global_merge_info.reset();
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(global_merge_info.assign(global_merge_info_))) {
    LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
  }
  return ret;
}

int ObGlobalMergeManagerBase::suspend_merge()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(suspend_or_resume_merge(true))) {
    LOG_WARN("fail to suspend merge", KR(ret));
  }
  return ret;
}

int ObGlobalMergeManagerBase::resume_merge()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(suspend_or_resume_merge(false))) {
    LOG_WARN("fail to resume merge", KR(ret));
  }
  return ret;
}

int ObGlobalMergeManagerBase::suspend_or_resume_merge(const bool suspend)
{
  int ret = OB_SUCCESS;
  ObGlobalMergeInfo tmp_global_info;
  if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
    LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
  } else {
    tmp_global_info.suspend_merging_.set_val(suspend, true);
    if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(
        *proxy_, tmp_global_info))) {
      LOG_WARN("fail to update global merge info", KR(ret), K(tmp_global_info));
    } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
    } else {
      LOG_INFO("succeed to update global merge info", K(tmp_global_info));
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::set_merge_status(const int64_t error_type)
{
  int ret = OB_SUCCESS;
  if (error_type >= ObGlobalMergeInfo::ERROR_TYPE_MAX
      || error_type < ObGlobalMergeInfo::NONE_ERROR) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(error_type));
  } else if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    const int64_t is_merge_error =
        ObGlobalMergeInfo::NONE_ERROR == error_type ? 0 : 1;
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else {
      tmp_global_info.is_merge_error_.set_val(is_merge_error, true);
      tmp_global_info.error_type_.set_val(error_type, true);
      FREEZE_TIME_GUARD;
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(
          *proxy_, tmp_global_info))) {
        LOG_WARN("fail to update global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      } else {
        LOG_INFO("succeed to set merge status", K(error_type), K(is_merge_error));
        MANAGEMENT_EVENT_ADD("daily_merge", "set_merge_error",
                              K(is_merge_error), K(error_type));
      }
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::check_need_broadcast(
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
  } else if (global_merge_info_.frozen_scn() < frozen_scn
             && GCONF.enable_major_freeze) {
    need_broadcast = true;
  }
  return ret;
}

int ObGlobalMergeManagerBase::set_global_freeze_info(const SCN &frozen_scn)
{
  int ret = OB_SUCCESS;
  bool need_broadcast = false;
  if (OB_FAIL(check_need_broadcast(frozen_scn, need_broadcast))) {
    LOG_WARN("fail to check need broadcast", KR(ret), K(frozen_scn));
  } else if (!need_broadcast) {
    LOG_INFO("no need to set global freeze info", K(frozen_scn), K_(global_merge_info));
  } else {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret));
    } else {
      tmp_global_info.frozen_scn_.set_scn(frozen_scn, true);
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(
          *proxy_, tmp_global_info))) {
        LOG_WARN("fail to update global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      }
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::get_global_broadcast_scn(SCN &global_broadcast_scn) const
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

int ObGlobalMergeManagerBase::get_global_last_merged_scn(SCN &global_last_merged_scn) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_last_merged_scn = global_merge_info_.last_merged_scn();
  }
  return ret;
}

int ObGlobalMergeManagerBase::get_global_merge_status(
    ObGlobalMergeInfo::MergeStatus &global_merge_status) const
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else {
    global_merge_status = static_cast<ObGlobalMergeInfo::MergeStatus>(
        global_merge_info_.merge_status_.get_value());
  }
  return ret;
}

int ObGlobalMergeManagerBase::get_global_last_merged_time(
    int64_t &global_last_merged_time) const
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

int ObGlobalMergeManagerBase::get_global_merge_start_time(
    int64_t &global_merge_start_time) const
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

int ObGlobalMergeManagerBase::generate_next_global_broadcast_scn(SCN &next_scn)
{
  int ret = OB_SUCCESS;
  FREEZE_TIME_GUARD;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (global_merge_info_.is_merge_error()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge status contains an error", KR(ret), K_(global_merge_info));
  } else if (global_merge_info_.last_merged_scn()
             < global_merge_info_.global_broadcast_scn()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("previous merge has not finished", KR(ret), K_(global_merge_info));
  } else if (global_merge_info_.last_merged_scn()
             > global_merge_info_.global_broadcast_scn()) {
    ret = OB_ERR_SYS;
    LOG_ERROR("last merged scn exceeds broadcast scn", KR(ret), K_(global_merge_info));
  } else {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else if (global_merge_info_.global_broadcast_scn()
               < global_merge_info_.frozen_scn()) {
      next_scn = global_merge_info_.frozen_scn();
      tmp_global_info.global_broadcast_scn_.set_scn(next_scn, true);
      tmp_global_info.merge_start_time_.set_val(ObTimeUtility::current_time(), true);
    } else if (global_merge_info_.global_broadcast_scn()
               == global_merge_info_.frozen_scn()) {
      next_scn = global_merge_info_.global_broadcast_scn();
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("broadcast scn exceeds frozen scn", KR(ret), K_(global_merge_info));
    }

    if (OB_SUCC(ret)) {
      tmp_global_info.merge_status_.set_val(
          ObGlobalMergeInfo::MERGE_STATUS_MERGING, true);
      FREEZE_TIME_GUARD;
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(
          *proxy_, tmp_global_info))) {
        LOG_WARN("fail to update global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      }
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::try_update_global_last_merged_scn()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (global_merge_info_.is_in_merge()) {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else {
      tmp_global_info.last_merged_time_.set_val(ObTimeUtility::current_time(), true);
      tmp_global_info.last_merged_scn_.set_scn(
          global_merge_info_.global_broadcast_scn(), true);
      tmp_global_info.merge_status_.set_val(
          ObGlobalMergeInfo::MERGE_STATUS_IDLE, true);
      FREEZE_TIME_GUARD;
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(
          *proxy_, tmp_global_info))) {
        LOG_WARN("fail to update global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      }
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::adjust_global_merge_info()
{
  int ret = OB_SUCCESS;
  ObFreezeInfoProxy freeze_info_proxy;
  SCN min_compaction_scn;
  SCN max_frozen_scn;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check inner stat", KR(ret));
  } else if (OB_FAIL(ObTabletMetaTableCompactionOperator::get_min_compaction_scn(
      min_compaction_scn))) {
    LOG_WARN("fail to get min compaction scn", KR(ret));
  } else if (OB_UNLIKELY(min_compaction_scn < SCN::base_scn())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected min compaction scn", KR(ret), K(min_compaction_scn));
  } else if (min_compaction_scn > SCN::base_scn()) {
    if (OB_FAIL(freeze_info_proxy.get_max_frozen_scn_smaller_or_equal_than(
        *proxy_, min_compaction_scn, max_frozen_scn))) {
      LOG_WARN("fail to get matching frozen scn", KR(ret), K(min_compaction_scn));
    } else if (max_frozen_scn < SCN::base_scn()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected max frozen scn", KR(ret), K(max_frozen_scn));
    } else if (max_frozen_scn > SCN::base_scn()
               && OB_FAIL(inner_adjust_global_merge_info(max_frozen_scn))) {
      LOG_WARN("fail to adjust global merge info", KR(ret), K(max_frozen_scn));
    }
  }
  FLOG_INFO("finish adjusting global merge info",
            K(min_compaction_scn), K(max_frozen_scn), K_(global_merge_info));
  return ret;
}

int ObGlobalMergeManagerBase::inner_adjust_global_merge_info(const SCN &frozen_scn)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!frozen_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(frozen_scn));
  } else {
    ObGlobalMergeInfo tmp_global_info;
    if (OB_FAIL(tmp_global_info.assign_value(global_merge_info_))) {
      LOG_WARN("fail to assign global merge info", KR(ret), K_(global_merge_info));
    } else {
      tmp_global_info.frozen_scn_.set_scn(frozen_scn, true);
      tmp_global_info.global_broadcast_scn_.set_scn(frozen_scn, true);
      tmp_global_info.last_merged_scn_.set_scn(frozen_scn, true);
      if (OB_FAIL(ObGlobalMergeTableOperator::update_partial_global_merge_info(
          *proxy_, tmp_global_info))) {
        LOG_WARN("fail to update global merge info", KR(ret), K(tmp_global_info));
      } else if (OB_FAIL(global_merge_info_.assign_value(tmp_global_info))) {
        LOG_WARN("fail to assign global merge info", KR(ret), K(tmp_global_info));
      }
    }
  }
  return ret;
}

int ObGlobalMergeManagerBase::copy_info(
    ObGlobalMergeManagerBase &dest,
    const ObGlobalMergeManagerBase &src)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dest.global_merge_info_.assign(src.global_merge_info_))) {
    LOG_WARN("fail to assign global merge info", KR(ret), K(src.global_merge_info_));
  } else {
    dest.is_inited_ = src.is_inited_;
    dest.is_loaded_ = src.is_loaded_;
  }
  return ret;
}

ObGlobalMergeManager::ObGlobalMergeMgrGuard::ObGlobalMergeMgrGuard(
    const SpinRWLock &lock,
    ObGlobalMergeManagerBase &global_merge_mgr,
    ObGlobalMergeManagerBase &shadow,
    int &ret)
  : lock_(const_cast<SpinRWLock &>(lock)),
    global_merge_mgr_(global_merge_mgr),
    shadow_(shadow),
    ret_(ret)
{
  SpinRLockGuard copy_guard(lock_);
  if (OB_SUCCESS == ret_) {
    const int tmp_ret = ObGlobalMergeManager::copy_info(shadow_, global_merge_mgr_);
    if (OB_SUCCESS != tmp_ret) {
      ret_ = tmp_ret;
      LOG_WARN("fail to copy global merge info to shadow", K(tmp_ret));
    }
  }
}

ObGlobalMergeManager::ObGlobalMergeMgrGuard::~ObGlobalMergeMgrGuard()
{
  SpinWLockGuard copy_guard(lock_);
  if (OB_SUCCESS == ret_) {
    const int tmp_ret = ObGlobalMergeManager::copy_info(global_merge_mgr_, shadow_);
    if (OB_SUCCESS != tmp_ret) {
      ret_ = tmp_ret;
      LOG_WARN_RET(tmp_ret, "fail to copy global merge info from shadow");
    }
  }
}

ObGlobalMergeManager::ObGlobalMergeManager()
  : write_lock_(ObLatchIds::GLOBAL_MERGE_MANAGER_WRITE_LOCK),
    shadow_()
{
}

int ObGlobalMergeManager::init(ObMySQLProxy &proxy)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObGlobalMergeManagerBase::init(proxy))) {
    LOG_WARN("fail to init global merge manager", KR(ret));
  } else if (OB_FAIL(shadow_.init(proxy))) {
    LOG_WARN("fail to init global merge manager shadow", KR(ret));
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
