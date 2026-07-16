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

#define USING_LOG_PREFIX TABLELOCK
#include "storage/tablelock/ob_table_lock_local_executor.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "storage/tx/ob_trans_service.h"

namespace oceanbase
{
using namespace transaction;
using namespace transaction::tablelock;

namespace observer
{

template <typename T>
int check_exist(const ObLockTaskBatchRequest<T> &arg,
                const common::ObTabletID  &tablet_id,
                ObLS * tenant_ls)
{
  int ret = OB_SUCCESS;
  ObTabletHandle tablet_handle;
  ObTabletStatus::Status tablet_status = ObTabletStatus::MAX;
  ObTabletCreateDeleteMdsUserData data;
  mds::MdsWriter unused_writer;// will be removed later
  mds::TwoPhaseCommitState unused_trans_stat;// will be removed later
  share::SCN unused_trans_version;// will be removed later
  if (ObTableLockTaskType::LOCK_ALONE_TABLET == arg.task_type_ ||
      ObTableLockTaskType::UNLOCK_ALONE_TABLET == arg.task_type_ ||
      ObTableLockTaskType::ADD_LOCK_INTO_QUEUE_WITHOUT_CHECK == arg.task_type_) {
    // alone tablet does not check exist
  } else if (OB_FAIL(tenant_ls->get_tablet(tablet_id,
                                                    tablet_handle,
                                                    0,
                                                    ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    LOG_WARN("get tablet with timeout failed", K(ret), K(tablet_id));
  } else if (OB_FAIL(tablet_handle.get_obj()->get_latest(
      data, unused_writer, unused_trans_stat, unused_trans_version))) {
    if (OB_EMPTY_RESULT == ret) {
      // tablet is creating
      ret = OB_TABLET_NOT_EXIST;
    } else {
      LOG_WARN("failed to get latest tablet status", KR(ret), K(tablet_id));
    }
  } else if (FALSE_IT(tablet_status = data.get_tablet_status())) {
  } else if (ObTabletStatus::NORMAL == tablet_status) {
    // do nothing
  } else if (ObTabletStatus::RESERVED_STATUS_4 == tablet_status
             || ObTabletStatus::RESERVED_STATUS_5 == tablet_status
             || ObTabletStatus::RESERVED_STATUS_6 == tablet_status) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("reserved tablet status is not supported", KR(ret), K(tablet_id), K(tablet_status));
  } else if (OB_UNLIKELY(data.tablet_status_.is_deleted_for_gc())) {
    // tablet shell
    ret = OB_TABLET_NOT_EXIST;
    LOG_INFO("tablet is already deleted", KR(ret), K(tablet_id));
  } else {
    // do nothing
  }
  return ret;
}

#define BATCH_PROCESS(arg, func_name, result)                           \
  ({                                                                    \
    int ret = OB_SUCCESS;                                               \
    ObAccessService *access_srv = share::g_mp->access_service();               \
    ObLS *tenant_ls = nullptr;                                               \
    common::ObTabletID tablet_id;                                       \
    if (OB_FAIL(share::g_mp->ls_service()->get_ls(tenant_ls))) {       \
      LOG_WARN("check ls failed", K(ret), K(arg));                      \
      if (OB_LS_NOT_EXIST == ret) {                                     \
        result.can_retry_ = true;                                       \
      }                                                                 \
    } else {                                                            \
      for (int i = 0; i < arg.params_.count() && OB_SUCC(ret); i++) {   \
        if (arg.params_[i].lock_id_.is_tablet_lock()) {                 \
          if (OB_FAIL(arg.params_[i].lock_id_.convert_to(tablet_id))) { \
            LOG_WARN("convert lock id to tablet id failed", K(ret),     \
                     K(arg.params_[i].lock_id_));                       \
          } else if (OB_FAIL(check_exist(arg,                           \
                                         tablet_id,                     \
                                         tenant_ls))) {                 \
            LOG_WARN("check tablet failed", K(ret), K(tablet_id),       \
                     K(arg.params_[i].expired_time_), K(tenant_ls));    \
            if (OB_TABLET_NOT_EXIST == ret) {                           \
              result.can_retry_ = true;                                 \
            }                                                           \
          }                                                             \
        }                                                               \
        if (OB_FAIL(ret)) {                                             \
        } else if (OB_FAIL(access_srv->func_name(*(arg.tx_desc_),       \
                                                 arg.params_[i]))) {    \
          LOG_WARN("failed to exec", K(ret), K(arg.params_[i]));        \
        } else if (arg.params_[i].lock_id_.is_tablet_lock() &&          \
                   OB_FAIL(check_exist(arg,                             \
                                       tablet_id,                       \
                                       tenant_ls))) {                   \
          LOG_WARN("check tablet failed", K(ret), K(tablet_id),         \
                   K(arg.params_[i].expired_time_), K(tenant_ls));      \
        } else {                                                        \
          result.success_pos_ = i;                                      \
        }                                                               \
      }                                                                 \
    }                                                                   \
    ret;                                                                \
  })

// ObTableLockTaskP / ObHighPriorityTableLockTaskP processors removed: single-task lock RPC
// was dead (no clients). The batch path below is the live one.

int handle_batch_lock_task(const ObLockTaskBatchRequest<ObLockParam> &arg,
                           ObTableLockTaskResult &result)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  // lock/unlock process:
  // 1. get ls
  // 2. get store ctx
  // 3. lock/unlock
  // 4. collect tx exec result.

  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    ObTransService *tx_srv = share::g_mp->trans_service();
    switch (arg.task_type_) {
      case ObTableLockTaskType::PRE_CHECK_TABLET: {
        // NOTE: yanyuan.cxf pre check should not check timeout
        ret = BATCH_PROCESS(arg, pre_check_lock, result);
        break;
      }
      case ObTableLockTaskType::ADD_LOCK_INTO_QUEUE:
      case ObTableLockTaskType::ADD_LOCK_INTO_QUEUE_WITHOUT_CHECK: {
        ret = BATCH_PROCESS(arg, add_lock_into_queue, result);
        break;
      }
      case ObTableLockTaskType::LOCK_TABLE:
      case ObTableLockTaskType::LOCK_PARTITION:
      case ObTableLockTaskType::LOCK_SUBPARTITION:
      case ObTableLockTaskType::LOCK_TABLET:
      case ObTableLockTaskType::LOCK_OBJECT:
      case ObTableLockTaskType::LOCK_ALONE_TABLET: {
        if (OB_FAIL(BATCH_PROCESS(arg, lock_obj, result))) {
          LOG_WARN("failed to exec lock obj operation", K(ret), K(arg));
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_ERROR("invalid task type", K(ret), K(arg));
        break;
      } // default
    } // switch

    if (OB_SUCCESS != (tmp_ret = tx_srv->
                       get_tx_exec_result(*(arg.tx_desc_),
                                          result.get_tx_result()))) {
      result.tx_result_ret_code_ = tmp_ret;
      LOG_WARN("get trans_result fail", KR(tmp_ret), K(arg.tx_desc_));
    }
  }

  result.ret_code_ = ret;
  LOG_DEBUG("handle_batch_lock_task", KR(ret), K(result), K(arg));
  ret = OB_SUCCESS;

  return ret;
}

static int process_for_replace_lock_table_(const ObLockTaskBatchRequest<ObReplaceLockParam> &arg,
                                           ObTableLockTaskResult &result);
static int replace_lock_for_tablet_in_table_(transaction::ObTxDesc &tx_desc,
                                              const ObReplaceLockParam &lock_param);

int handle_batch_replace_lock_task(const ObLockTaskBatchRequest<ObReplaceLockParam> &arg,
                                   ObTableLockTaskResult &result)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  // lock/unlock process:
  // 1. get ls
  // 2. get store ctx
  // 3. lock/unlock
  // 4. collect tx exec result.

  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    ObTransService *tx_srv = share::g_mp->trans_service();
    switch (arg.task_type_) {
      case ObTableLockTaskType::REPLACE_LOCK_TABLE: {
        if (OB_FAIL(process_for_replace_lock_table_(arg, result))) {
          LOG_WARN("failed to exec replace_obj_lock operation for table", K(ret), K(arg));
        }
        break;
      }
      case ObTableLockTaskType::REPLACE_LOCK_PARTITION:
      case ObTableLockTaskType::REPLACE_LOCK_SUBPARTITION:
      case ObTableLockTaskType::REPLACE_LOCK_TABLETS:
      case ObTableLockTaskType::REPLACE_LOCK_OBJECTS:
      case ObTableLockTaskType::REPLACE_LOCK_ALONE_TABLET: {
        if (OB_FAIL(BATCH_PROCESS(arg, replace_obj_lock, result))) {
          LOG_WARN("failed to exec replace_obj_lock operation", K(ret), K(arg));
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_ERROR("invalid task type", K(ret), K(arg));
        break;
      } // default
    } // switch

    if (OB_SUCCESS != (tmp_ret = tx_srv->
                       get_tx_exec_result(*(arg.tx_desc_),
                                          result.get_tx_result()))) {
      result.tx_result_ret_code_ = tmp_ret;
      LOG_WARN("get trans_result fail", KR(tmp_ret), K(arg.tx_desc_));
    }
  }

  result.ret_code_ = ret;
  LOG_DEBUG("handle_batch_replace_lock_task", KR(ret), K(result), K(arg));
  ret = OB_SUCCESS;

  return ret;
}

static int process_for_replace_lock_table_(const ObLockTaskBatchRequest<ObReplaceLockParam> &arg,
                                           ObTableLockTaskResult &result)
{
  int ret = OB_SUCCESS;
  ObAccessService *access_srv = share::g_mp->access_service();
  ObLS *tenant_ls = nullptr;
  common::ObTabletID tablet_id;
  if (OB_FAIL(share::g_mp->ls_service()->get_ls(tenant_ls))) {
    LOG_WARN("check ls failed", K(ret), K(arg));
    if (OB_LS_NOT_EXIST == ret) {
      result.can_retry_ = true;
    }
  } else {
    for (int i = 0; i < arg.params_.count() && OB_SUCC(ret); i++) {
      if (arg.params_[i].lock_id_.is_tablet_lock()) {
        if (OB_FAIL(arg.params_[i].lock_id_.convert_to(tablet_id))) {
          LOG_WARN("convert lock id to tablet id failed", K(ret), K(arg.params_[i].lock_id_));
        } else if (OB_FAIL(check_exist(arg, tablet_id, tenant_ls))) {
          LOG_WARN("check tablet failed", K(ret), K(tablet_id), K(arg.params_[i].expired_time_), K(tenant_ls));
          if (OB_TABLET_NOT_EXIST == ret) {
            result.can_retry_ = true;
          }
        } else if (OB_FAIL(replace_lock_for_tablet_in_table_(*(arg.tx_desc_), arg.params_[i]))) {
          LOG_WARN("failed to replace lock for tablet in table", K(ret), K(arg.params_[i]));
        } else if (OB_FAIL(check_exist(arg, tablet_id, tenant_ls))) {
          LOG_WARN("check tablet failed", K(ret), K(tablet_id), K(arg.params_[i].expired_time_), K(tenant_ls));
        } else {
          result.success_pos_ = i;
        }
      } else if (OB_FAIL(access_srv->replace_obj_lock(*(arg.tx_desc_), arg.params_[i]))) {
        LOG_WARN("failed to replace lock table", K(ret), K(arg.params_[i]));
      }
    }
  }
  return ret;
}

static int replace_lock_for_tablet_in_table_(transaction::ObTxDesc &tx_desc,
                                             const ObReplaceLockParam &lock_param)
{
  int ret = OB_SUCCESS;
  ObAccessService *access_srv = share::g_mp->access_service();
  if (is_need_lock_tablet_mode(lock_param.lock_mode_) && !is_need_lock_tablet_mode(lock_param.new_lock_mode_)) {
    ret = access_srv->unlock_obj(tx_desc, lock_param);
  } else if (!is_need_lock_tablet_mode(lock_param.lock_mode_) && is_need_lock_tablet_mode(lock_param.new_lock_mode_)) {
    ObLockParam new_lock_param;
    // we should set new_owner_id and new_lock_mode to owner_id and lock_mode in lock progress
    if (OB_FAIL(new_lock_param.set(lock_param.lock_id_,
                                   lock_param.new_lock_mode_,
                                   lock_param.new_owner_id_,
                                   OUT_TRANS_LOCK,
                                   lock_param.schema_version_,
                                   lock_param.is_deadlock_avoid_enabled_,
                                   lock_param.is_try_lock_,
                                   lock_param.expired_time_))) {
      LOG_WARN("set lock_param for replace tablet lock failed", K(ret), K(lock_param));
    } else {
      ret = access_srv->lock_obj(tx_desc, new_lock_param);
    }
  } else {
    ret = access_srv->replace_obj_lock(tx_desc, lock_param);
  }
  return ret;
}

int handle_high_priority_batch_lock_task(const ObLockTaskBatchRequest<ObLockParam> &arg,
                                         ObTableLockTaskResult &result)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  // lock/unlock process:
  // 1. get ls
  // 2. get store ctx
  // 3. lock/unlock
  // 4. collect tx exec result.

  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    ObTransService *tx_srv = share::g_mp->trans_service();
    switch (arg.task_type_) {
      case ObTableLockTaskType::UNLOCK_TABLE:
      case ObTableLockTaskType::UNLOCK_PARTITION:
      case ObTableLockTaskType::UNLOCK_SUBPARTITION:
      case ObTableLockTaskType::UNLOCK_TABLET:
      case ObTableLockTaskType::UNLOCK_OBJECT:
      case ObTableLockTaskType::UNLOCK_ALONE_TABLET: {
        if (OB_FAIL(BATCH_PROCESS(arg, unlock_obj, result))) {
          LOG_WARN("failed to exec unlock obj operation", K(ret), K(arg));
        }
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        LOG_ERROR("invalid task type", K(ret), K(arg));
        break;
      } // default
    } // switch

    if (OB_SUCCESS != (tmp_ret = tx_srv->
                       get_tx_exec_result(*(arg.tx_desc_),
                                          result.get_tx_result()))) {
      result.tx_result_ret_code_ = tmp_ret;
      LOG_WARN("get trans_result fail", KR(tmp_ret), K(arg.tx_desc_));
    }
  }

  result.ret_code_ = ret;
  LOG_DEBUG("handle_high_priority_batch_lock_task", KR(ret), K(result), K(arg));
  ret = OB_SUCCESS;

  return ret;
}

// ObOutTransLockTableP / ObOutTransUnlockTableP processors removed: the out-trans lock RPC
// (ObTableLockRpcProxy::lock_table/unlock_table) was dead (no callers).


} // observer
} // oceanbase
