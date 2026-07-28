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
#include "storage/tablelock/ob_table_lock_service.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tablelock/ob_table_lock_local_executor.h"

#include "storage/tx/ob_trans_service.h"
#include "storage/tablelock/ob_lock_utils.h" // ObInnerTableLockUtil
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablelock/ob_table_lock_live_detector.h"

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace share::schema;

namespace transaction
{

namespace tablelock
{
ObTableLockService::ObTableLockCtx::ObTableLockCtx() :
  task_type_(INVALID_LOCK_TASK_TYPE),
  is_in_trans_(false),
  table_id_(OB_INVALID_ID),
  partition_id_(OB_INVALID_ID),
  lock_op_type_(UNKNOWN_TYPE),
  origin_timeout_us_(-1),
  timeout_us_(-1),
  abs_timeout_ts_(-1),
  trans_state_(),
  tx_desc_(nullptr),
  tx_param_(),
  current_savepoint_(),
  need_rollback_(false),
  tablet_list_(),
  obj_list_(),
  lock_mode_(MAX_LOCK_MODE),
  lock_owner_(),
  schema_version_(-1),
  tx_is_killed_(false),
  is_from_sql_(false),
  ret_code_before_end_stmt_or_tx_(OB_SUCCESS),
  lock_priority_(ObTableLockPriority::NORMAL),
  stmt_savepoint_(),
  is_for_replace_(false)
{
  is_enable_lock_priority_ = false;
  is_enable_lock_priority_ = GCONF.enable_lock_priority;
}

void ObTableLockService::ObRetryCtx::reuse()
{
  need_retry_ = false;
  task_executed_ = false;
  task_prepared_ = false;
  retry_lock_ids_.reuse();
}

int ObTableLockService::ObTableLockCtx::set_by_lock_req(const ObLockRequest &arg, const bool is_replace_task)
{
  int ret = OB_SUCCESS;
  switch (arg.type_) {
    case ObLockRequest::ObLockMsgType::LOCK_TABLE_REQ:
    case ObLockRequest::ObLockMsgType::UNLOCK_TABLE_REQ: {
      const ObLockTableRequest &lock_arg = static_cast<const ObLockTableRequest &>(arg);
      if (is_replace_task) {
        task_type_ = REPLACE_LOCK_TABLE;
      } else if (arg.is_unlock_request()) {
        task_type_ = UNLOCK_TABLE;
      } else {
        task_type_ = LOCK_TABLE;
      }
      table_id_ = lock_arg.table_id_;
      break;
    }
    case ObLockRequest::ObLockMsgType::LOCK_TABLET_REQ:
    case ObLockRequest::ObLockMsgType::UNLOCK_TABLET_REQ: {
      const ObLockTabletsRequest &lock_arg = static_cast<const ObLockTabletsRequest &>(arg);
      if (is_replace_task) {
        task_type_ = REPLACE_LOCK_TABLETS;
      } else if (arg.is_unlock_request()) {
        task_type_ = UNLOCK_TABLET;
      } else {
        task_type_ = LOCK_TABLET;
      }
      table_id_ = lock_arg.table_id_;
      if (OB_FAIL(set_tablet_id(lock_arg.tablet_ids_))) {
        LOG_WARN("set tablet id failed", K(ret), K(lock_arg));
      }
      break;
    }
    case ObLockRequest::ObLockMsgType::LOCK_OBJ_REQ:
    case ObLockRequest::ObLockMsgType::UNLOCK_OBJ_REQ: {
      const ObLockObjsRequest &lock_arg = static_cast<const ObLockObjsRequest &>(arg);
      if (is_replace_task) {
        task_type_ = REPLACE_LOCK_OBJECTS;
      } else if (arg.is_unlock_request()) {
        task_type_ = UNLOCK_OBJECT;
      } else {
        task_type_ = LOCK_OBJECT;
      }
      if (OB_FAIL(set_lock_id(lock_arg.objs_))) {
      LOG_WARN("set lock id failed", K(ret), K(lock_arg));
      }
      break;
    }
    case ObLockRequest::ObLockMsgType::LOCK_PARTITION_REQ:
    case ObLockRequest::ObLockMsgType::UNLOCK_PARTITION_REQ: {
      const ObLockPartitionRequest &lock_arg = static_cast<const ObLockPartitionRequest &>(arg);
      if (lock_arg.is_sub_part_) {
        if (is_replace_task) {
          task_type_ = REPLACE_LOCK_SUBPARTITION;
        } else if (arg.is_unlock_request()) {
          task_type_ = UNLOCK_SUBPARTITION;
        } else {
          task_type_ = LOCK_SUBPARTITION;
        }
      } else {
        if (is_replace_task) {
          task_type_ = REPLACE_LOCK_PARTITION;
        } else if (arg.is_unlock_request()) {
          task_type_ = UNLOCK_PARTITION;
        } else {
          task_type_ = LOCK_PARTITION;
        }
      }
      table_id_ = lock_arg.table_id_;
      partition_id_ = lock_arg.part_object_id_;
      break;
    }
    case ObLockRequest::ObLockMsgType::LOCK_ALONE_TABLET_REQ:
    case ObLockRequest::ObLockMsgType::UNLOCK_ALONE_TABLET_REQ: {
      const ObLockAloneTabletRequest &lock_arg = static_cast<const ObLockAloneTabletRequest &>(arg);
      if (is_replace_task) {
        task_type_ = REPLACE_LOCK_ALONE_TABLET;
      } else if (arg.is_unlock_request()) {
        task_type_ = UNLOCK_ALONE_TABLET;
      } else {
        task_type_ = LOCK_ALONE_TABLET;
      }
      table_id_ = lock_arg.table_id_;
      if (OB_FAIL(set_tablet_id(lock_arg.tablet_ids_))) {
        LOG_WARN("set tablet id failed", K(ret), K(lock_arg));
      }
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("meet not support request type", K(ret), K(arg));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(set_by_lock_req_common_part(arg))) {
    LOG_WARN("set lock_ctx common part failed", K(ret), K(arg));
  }
  return ret;
}

int ObTableLockService::ObTableLockCtx::set_by_lock_req_common_part(const ObLockRequest &arg)
{
  int ret = OB_SUCCESS;
  if (INVALID_LOCK_TASK_TYPE == task_type_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("not set task_type before fill ctx", K(ret), K(arg));
  } else if (((is_unlock_task() || is_replace_task()) && OUT_TRANS_UNLOCK != arg.op_type_)
             || (!is_unlock_task() && !is_replace_task() && OUT_TRANS_UNLOCK == arg.op_type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("lock task_type is not match with lock op_type", K(ret), K(arg), KPC(this));
  } else {
    origin_timeout_us_ = arg.timeout_us_;
    timeout_us_ = arg.timeout_us_;
    abs_timeout_ts_ = (0 == arg.timeout_us_) ? ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US
                                             : ObTimeUtility::current_time() + arg.timeout_us_;
    lock_op_type_ = arg.op_type_;
    is_from_sql_ = arg.is_from_sql_;
    lock_mode_ = arg.lock_mode_;
    lock_owner_ = arg.owner_id_;
    lock_priority_ = arg.lock_priority_;
  }
  return ret;
}

int ObTableLockService::ObReplaceTableLockCtx::get_lock_param(const ObLockID &lock_id,
                                                              ObReplaceLockParam &lock_param) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_lock_mode_valid(new_lock_mode_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(lock_id), K(new_lock_mode_));
  } else if (OB_FAIL(lock_param.set(lock_id,
                                    lock_mode_,
                                    lock_owner_,
                                    lock_op_type_,
                                    schema_version_,
                                    is_deadlock_avoid_enabled(),
                                    is_try_lock(),
                                    abs_timeout_ts_))) {
    LOG_WARN("set param for ObLockParam failed",
             K(ret),
             K(lock_id),
             K(lock_mode_),
             K(lock_owner_),
             K(new_lock_mode_),
             K(new_lock_owner_),
             K(lock_op_type_),
             K(is_try_lock()),
             K(abs_timeout_ts_));
  } else {
    lock_param.is_for_replace_ = true;
    lock_param.new_lock_mode_ = new_lock_mode_;
    lock_param.new_owner_id_ = new_lock_owner_;
  }
  return ret;
}

int64_t ObTableLockService::ObOBJLockGarbageCollector::GARBAGE_COLLECT_EXEC_INTERVAL = 10_s;
int64_t ObTableLockService::ObOBJLockGarbageCollector::GARBAGE_COLLECT_TIMEOUT = 10_min;

ObTableLockService::ObOBJLockGarbageCollector::ObOBJLockGarbageCollector()
  : timer_(),
    timer_task_(*this),
    last_success_timestamp_(0) {}
ObTableLockService::ObOBJLockGarbageCollector::~ObOBJLockGarbageCollector() {}

int ObTableLockService::ObOBJLockGarbageCollector::start()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(timer_.init("OBJLockGC", common::ObMemAttr("OBJLockGC")))) {
    LOG_WARN("fail to init timer for ObTableLockService::ObOBJLockGarbageCollector",
             KR(ret), KPC(this));
  } else if (OB_FAIL(timer_.schedule(timer_task_,
                                 GARBAGE_COLLECT_EXEC_INTERVAL,
                                 true /* repeat */,
                                 false /* immediate */))) {
    LOG_ERROR("ObTableLockService::ObOBJLockGarbageCollector schedules repeat task failed",
              KR(ret), KPC(this));
  } else {
    LOG_INFO("ObTableLockService::ObOBJLockGarbageCollector starts successfully", K(ret),
             KPC(this));
  }
  return ret;
}

void ObTableLockService::ObOBJLockGarbageCollector::stop()
{
  if (timer_.inited()) {
    timer_.stop();
  }
  LOG_INFO("ObTableLockService::ObOBJLockGarbageCollector stops successfully", KPC(this));
}

void ObTableLockService::ObOBJLockGarbageCollector::wait()
{
  if (timer_.inited()) {
    timer_.wait();
  }
  LOG_INFO("ObTableLockService::ObOBJLockGarbageCollector waits successfully", KPC(this));
}

void ObTableLockService::ObOBJLockGarbageCollector::destroy()
{
  timer_.destroy();
  LOG_INFO("ObTableLockService::ObOBJLockGarbageCollector destroys successfully", KPC(this));
}

int ObTableLockService::ObOBJLockGarbageCollector::garbage_collect_right_now()
{
  int ret = OB_SUCCESS;
  if (!timer_.inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("timer of ObTableLockService::ObOBJLockGarbageCollector is not running", K(ret));
  } else {
    run_gc_once_();
  }
  return ret;
}

void ObTableLockService::ObOBJLockGarbageCollector::run_gc_once_()
{
  common::ObDIActionGuard ag("TableLockService", "OBJLockGC", "GCTimer");
  int ret = OB_SUCCESS;
  if (OB_FAIL(garbage_collect_())) {
    check_and_report_timeout_();
    LOG_WARN("check and clear obj lock failed, will retry later",
             K(ret), K(last_success_timestamp_), KPC(this));
  } else {
    last_success_timestamp_ = ObClockGenerator::getClock();
    LOG_DEBUG("check and clear obj lock successfully", K(ret),
              K(last_success_timestamp_), KPC(this));
  }
}

int ObTableLockService::ObOBJLockGarbageCollector::garbage_collect_()
{
  int ret = OB_SUCCESS;
  if (!timer_.inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("timer of ObTableLockService::ObOBJLockGarbageCollector is not running", K(ret));
  } else if (OB_FAIL(ObTableLockDetector::do_detect_and_clear())) {
    LOG_WARN("do_detect_and_clear failed", K(ret));
  }
  return ret;
}

void ObTableLockService::ObOBJLockGarbageCollector::check_and_report_timeout_()
{
  int ret = OB_SUCCESS;
  int64_t current_timestamp = ObClockGenerator::getClock();
  if (last_success_timestamp_ > current_timestamp) {
    LOG_ERROR("last success timestamp is not correct", K(current_timestamp),
              K(last_success_timestamp_), KPC(this));
  } else if (current_timestamp - last_success_timestamp_ >
                 GARBAGE_COLLECT_TIMEOUT &&
             last_success_timestamp_ != 0) {
    LOG_ERROR("task failed too many times", K(current_timestamp),
              K(last_success_timestamp_), KPC(this));
  }
}

int ObTableLockService::ObTableLockCtx::set_tablet_id(const common::ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  tablet_list_.reuse();
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); i++) {
    if (OB_FAIL(tablet_list_.push_back(tablet_ids.at(i)))) {
      LOG_WARN("set tablet id failed", K(ret), K(i), K(tablet_ids));
    }
  }
  return ret;
}

int ObTableLockService::ObTableLockCtx::set_tablet_id(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  tablet_list_.reuse();
  if (OB_FAIL(tablet_list_.push_back(tablet_id))) {
    LOG_WARN("set tablet id failed", K(ret), K(tablet_id));
  }
  return ret;
}

int ObTableLockService::ObTableLockCtx::set_lock_id(const common::ObIArray<ObLockID> &lock_ids)
{
  int ret = OB_SUCCESS;
  obj_list_.reuse();
  for (int64_t i = 0; OB_SUCC(ret) && i < lock_ids.count(); i++) {
    if (OB_FAIL(obj_list_.push_back(lock_ids.at(i)))) {
      LOG_WARN("set lock id failed", K(ret), K(i), K(lock_ids));
    }
  }
  return ret;
}



bool ObTableLockService::ObTableLockCtx::is_timeout() const
{
  return ObTimeUtility::current_time() >= abs_timeout_ts_;
}

int64_t ObTableLockService::ObTableLockCtx::remain_timeoutus() const
{
  int64_t remain_us = abs_timeout_ts_ - ObTimeUtility::current_time();
  return remain_us > 0 ? remain_us : 0;
}

int64_t ObTableLockService::ObTableLockCtx::get_tablet_cnt() const
{
  return tablet_list_.count();
}

const ObTabletID &ObTableLockService::ObTableLockCtx::get_tablet_id(const int64_t index) const
{
  return tablet_list_.at(index);
}

void ObTableLockService::ObTableLockCtx::mark_need_rollback()
{
  need_rollback_ = true;
}

void ObTableLockService::ObTableLockCtx::clear_need_rollback()
{
  need_rollback_ = false;
}

bool ObTableLockService::ObTableLockCtx::is_deadlock_avoid_enabled() const
{
  return tablelock::is_deadlock_avoid_enabled(is_from_sql_, origin_timeout_us_);
}

int ObTableLockService::server_module_init(ObTableLockService* &lock_service)
{
  return lock_service->init();
}

int ObTableLockService::init()
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("lock service init twice.", K(ret));
  } else if (OB_UNLIKELY(!GCTX.self_addr().is_valid()) ||
             OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(GCTX.self_addr()),
             KP(GCTX.sql_proxy_));
  } else {
    sql_proxy_ = GCTX.sql_proxy_;
    is_inited_ = true;
  }

  if (OB_FAIL(ret)) {
    destroy();
  }

  return ret;
}

int ObTableLockService::start()
{
  obj_lock_garbage_collector_.start();
  return OB_SUCCESS;
}

void ObTableLockService::stop()
{
  obj_lock_garbage_collector_.stop();
}

void ObTableLockService::wait()
{
  obj_lock_garbage_collector_.wait();
}

void ObTableLockService::destroy()
{
  obj_lock_garbage_collector_.destroy();
  sql_proxy_ = nullptr;
  is_inited_ = false;
}



int ObTableLockService::lock_table(const uint64_t table_id,
                                   const ObTableLockMode lock_mode,
                                   const ObTableLockOwnerID lock_owner,
                                   const int64_t timeout_us)
{
  LOG_INFO("ObTableLockService::lock_table",
            K(table_id), K(lock_mode), K(lock_owner), K(timeout_us));
  int ret = OB_SUCCESS;
  int ret_code_before_end_stmt_or_tx = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret), K(table_id), K(lock_mode),
             K(lock_owner));
  } else if (OB_UNLIKELY(!is_valid_id(table_id)) ||
             OB_UNLIKELY(!is_lock_mode_valid(lock_mode))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(lock_mode), K(lock_owner));
  } else {
    // avoid deadlock when ddl conflict with dml
    // by restart ddl table lock trans
    int64_t retry_timeout_us = timeout_us;
    bool need_retry = false;
    int64_t abs_timeout_ts = (0 == timeout_us)
      ? ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US
      : ObTimeUtility::current_time() + timeout_us;
    do {
      if (timeout_us != 0) {
        retry_timeout_us = abs_timeout_ts - ObTimeUtility::current_time();
      }
      ObTableLockCtx ctx;
      ctx.task_type_ = LOCK_TABLE;
      ctx.table_id_ = table_id;
      ctx.lock_op_type_ = OUT_TRANS_LOCK;
      ctx.origin_timeout_us_ = timeout_us;
      ctx.timeout_us_ = retry_timeout_us;
      ctx.abs_timeout_ts_ = abs_timeout_ts;
      ctx.lock_mode_ = lock_mode;
      ctx.lock_owner_ = lock_owner;
      ret = process_lock_task_(ctx);
      need_retry = need_retry_trans_(ctx, ret);
      ret_code_before_end_stmt_or_tx = ctx.ret_code_before_end_stmt_or_tx_;
    } while (need_retry);
  }
  ret = rewrite_return_code_(ret, ret_code_before_end_stmt_or_tx, false /*is_from_sql*/);
  return ret;
}

int ObTableLockService::unlock_table(const uint64_t table_id,
                                     const ObTableLockMode lock_mode,
                                     const ObTableLockOwnerID lock_owner,
                                     const int64_t timeout_us)
{
  LOG_INFO("ObTableLockService::unlock_table",
            K(table_id), K(lock_mode), K(lock_owner), K(timeout_us));
  int ret = OB_SUCCESS;
  DEBUG_SYNC(BEFORE_LOCK_SERVICE_UNLOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret), K(table_id), K(lock_mode),
             K(lock_owner));
  } else if (OB_UNLIKELY(!is_valid_id(table_id)) ||
             OB_UNLIKELY(!is_lock_mode_valid(lock_mode))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(lock_mode), K(lock_owner));
  } else {
    int64_t retry_timeout_us = timeout_us;
    bool need_retry = false;
    int64_t abs_timeout_ts = (0 == timeout_us)
      ? ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US
      : ObTimeUtility::current_time() + timeout_us;
    do {
      if (timeout_us != 0) {
        retry_timeout_us = abs_timeout_ts - ObTimeUtility::current_time();
      }
      ObTableLockCtx ctx;
      ctx.task_type_ = UNLOCK_TABLE;
      ctx.table_id_ = table_id;
      ctx.lock_op_type_ = OUT_TRANS_UNLOCK;
      ctx.origin_timeout_us_ = timeout_us;
      ctx.timeout_us_ = retry_timeout_us;
      ctx.abs_timeout_ts_ = abs_timeout_ts;
      ctx.lock_mode_ = lock_mode;
      ctx.lock_owner_ = lock_owner;
      ret = process_lock_task_(ctx);
      need_retry = need_retry_trans_(ctx, ret);
    } while (need_retry);
  }
  ret = rewrite_return_code_(ret);
  return ret;
}

int ObTableLockService::lock_tablet(const uint64_t table_id,
                                    const ObTabletID &tablet_id,
                                    const ObTableLockMode lock_mode,
                                    const ObTableLockOwnerID lock_owner,
                                    const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  int ret_code_before_end_stmt_or_tx = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret), K(table_id), K(tablet_id),
             K(lock_mode), K(lock_owner));
  } else if (OB_UNLIKELY(!is_valid_id(table_id)) ||
             OB_UNLIKELY(!tablet_id.is_valid()) ||
             OB_UNLIKELY(!is_lock_mode_valid(lock_mode))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(tablet_id), K(lock_mode),
             K(lock_owner));
  } else {
    // avoid deadlock when ddl conflict with dml
    // by restart ddl table lock trans
    int64_t retry_timeout_us = timeout_us;
    bool need_retry = false;
    int64_t abs_timeout_ts = (0 == timeout_us)
      ? ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US
      : ObTimeUtility::current_time() + timeout_us;
    do {
      if (timeout_us != 0) {
        retry_timeout_us = abs_timeout_ts - ObTimeUtility::current_time();
      }
      ObTableLockCtx ctx;
      ctx.task_type_ = LOCK_TABLET;
      ctx.table_id_ = table_id;
      ctx.lock_op_type_ = OUT_TRANS_LOCK;
      ctx.origin_timeout_us_ = timeout_us;
      ctx.timeout_us_ = retry_timeout_us;
      ctx.abs_timeout_ts_ = (0 == retry_timeout_us) ? ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US
                                                    : ObTimeUtility::current_time() + retry_timeout_us;
      ctx.lock_mode_ = lock_mode;
      ctx.lock_owner_ = lock_owner;
      if (OB_FAIL(ctx.set_tablet_id(tablet_id))) {
        LOG_WARN("set tablet id failed", K(ret), K(tablet_id));
      } else if (OB_FAIL(process_lock_task_(ctx))) {
        LOG_WARN("process lock task failed", K(ret), K(tablet_id));
      }
      need_retry = need_retry_trans_(ctx, ret);
      ret_code_before_end_stmt_or_tx = ctx.ret_code_before_end_stmt_or_tx_;
    } while (need_retry);
  }
  ret = rewrite_return_code_(ret, ret_code_before_end_stmt_or_tx, false /*is_from_sql*/);
  return ret;
}

int ObTableLockService::unlock_tablet(const uint64_t table_id,
                                      const ObTabletID &tablet_id,
                                      const ObTableLockMode lock_mode,
                                      const ObTableLockOwnerID lock_owner,
                                      const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  DEBUG_SYNC(BEFORE_LOCK_SERVICE_UNLOCK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret), K(table_id), K(tablet_id),
             K(lock_mode), K(lock_owner));
  } else if (OB_UNLIKELY(!is_valid_id(table_id)) ||
             OB_UNLIKELY(!tablet_id.is_valid()) ||
             OB_UNLIKELY(!is_lock_mode_valid(lock_mode))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(table_id), K(tablet_id), K(lock_mode),
             K(lock_owner));
  } else {
    int64_t retry_timeout_us = timeout_us;
    bool need_retry = false;
    int64_t abs_timeout_ts = (0 == timeout_us)
      ? ObTimeUtility::current_time() + DEFAULT_TIMEOUT_US
      : ObTimeUtility::current_time() + timeout_us;
    do {
      if (timeout_us != 0) {
        retry_timeout_us = abs_timeout_ts - ObTimeUtility::current_time();
      }
      ObTableLockCtx ctx;
      ctx.task_type_ = UNLOCK_TABLET;
      ctx.table_id_ = table_id;
      ctx.lock_op_type_ = OUT_TRANS_UNLOCK;
      ctx.origin_timeout_us_ = timeout_us;
      ctx.timeout_us_ = retry_timeout_us;
      ctx.abs_timeout_ts_ = abs_timeout_ts;
      ctx.lock_mode_ = lock_mode;
      ctx.lock_owner_ = lock_owner;
      if (OB_FAIL(ctx.set_tablet_id(tablet_id))) {
        LOG_WARN("set tablet id failed", K(ret), K(tablet_id));
      } else if (OB_FAIL(process_lock_task_(ctx))) {
        LOG_WARN("process lock task failed", K(ret), K(tablet_id));
      }
      need_retry = need_retry_trans_(ctx, ret);
    } while (need_retry);
  }
  ret = rewrite_return_code_(ret);

  return ret;
}

int ObTableLockService::lock_partition_or_subpartition(ObTxDesc &tx_desc,
                                                       const ObTxParam &tx_param,
                                                       ObLockPartitionRequest &arg)
{
  int ret = OB_SUCCESS;
  ObPartitionLevel part_level = PARTITION_LEVEL_MAX;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret));
  } else if (OB_FAIL(get_table_partition_level_(arg.table_id_, part_level))) {
    LOG_WARN("can not get table partition level", K(ret), K(arg));
  } else {
    if (PARTITION_LEVEL_TWO == part_level) {
      arg.is_sub_part_ = true;
    }
    if (OB_FAIL(lock(tx_desc, tx_param, arg))) {
      LOG_WARN("lock partition failed", K(ret), K(arg));
    }
  }
  return ret;
}

int ObTableLockService::lock(ObTxDesc &tx_desc,
                             const ObTxParam &tx_param,
                             const ObLockRequest &arg,
                             const bool is_for_replace)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid()) ||
             OB_UNLIKELY(!tx_param.is_valid()) ||
             OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(arg), K(tx_desc.is_valid()),
             K(tx_param.is_valid()), K(arg.is_valid()));
  } else {
    ObTableLockCtx ctx;
    if (OB_FAIL(ctx.set_by_lock_req(arg))) {
      LOG_WARN("set ObTableLockCtx failed", K(ret), K(arg));
    } else {
      if (is_for_replace) {
        ctx.is_for_replace_ = true;
      }
      ctx.is_in_trans_ = true;
      ctx.tx_desc_ = &tx_desc;
      ctx.tx_param_ = tx_param;
      if (OB_FAIL(process_lock_task_(ctx))) {
        LOG_WARN("process lock task failed", K(ret), K(ctx), K(arg));
        ret = rewrite_return_code_(ret, ctx.ret_code_before_end_stmt_or_tx_, ctx.is_from_sql_);
      }
    }
  }
  return ret;
}

int ObTableLockService::unlock(ObTxDesc &tx_desc,
                               const ObTxParam &tx_param,
                               const ObUnLockRequest &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_unlock_request()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("can not unlock by a lock request", K(ret), K(tx_desc), K(tx_param), K(arg));
  } else if (OB_FAIL(lock(tx_desc, tx_param, arg))) {
    LOG_WARN("do unlock request failed", K(ret), K(tx_desc), K(tx_param), K(arg));
  }
  return ret;
}

int ObTableLockService::replace_lock(ObTxDesc &tx_desc,
                                     const ObTxParam &tx_param,
                                     const ObReplaceLockRequest &replace_req)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid()) ||
             OB_UNLIKELY(!tx_param.is_valid()) ||
             OB_UNLIKELY(!replace_req.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(replace_req), K(tx_desc.is_valid()),
             K(tx_param.is_valid()), K(replace_req.is_valid()));
  } else {
    ObReplaceTableLockCtx ctx;
    if (OB_FAIL(ctx.set_by_lock_req(*replace_req.unlock_req_, true))) {
      LOG_WARN("fail to set unlock_ctx", K(ret), K(replace_req));
    } else {
      ctx.is_in_trans_ = true;
      ctx.tx_desc_ = &tx_desc;
      ctx.tx_param_ = tx_param;
      ctx.new_lock_mode_ = replace_req.new_lock_mode_;
      ctx.new_lock_owner_ = replace_req.new_lock_owner_;
      if (OB_FAIL(process_lock_task_(ctx))) {
        LOG_WARN("process lock task failed", K(ret), K(ctx), K(replace_req));
        ret = rewrite_return_code_(ret, ctx.ret_code_before_end_stmt_or_tx_, ctx.is_from_sql_);
      }
    }
  }
  return ret;
}

int ObTableLockService::replace_lock(ObTxDesc &tx_desc,
                                     const ObTxParam &tx_param,
                                     const ObReplaceAllLocksRequest &replace_req)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("lock service is not inited", K(ret));
  } else if (OB_UNLIKELY(!tx_desc.is_valid()) ||
             OB_UNLIKELY(!tx_param.is_valid()) ||
             OB_UNLIKELY(!replace_req.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tx_desc), K(replace_req), K(tx_desc.is_valid()),
             K(tx_param.is_valid()), K(replace_req.is_valid()));
  } else {
    for (int64_t i = 0; i < replace_req.unlock_req_list_.count() && OB_SUCC(ret); i++) {
      if (OB_FAIL(unlock(tx_desc, tx_param, *replace_req.unlock_req_list_.at(i)))) {
        LOG_WARN("unlock in replace failed", K(ret), K(replace_req));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(lock(tx_desc, tx_param, *replace_req.lock_req_, true))){
      LOG_WARN("lock in replace failed", K(ret), K(replace_req));
    }
  }
  return ret;
}

int ObTableLockService::garbage_collect_right_now()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLockService is not be inited", K(ret));
  } else if (OB_FAIL(obj_lock_garbage_collector_.garbage_collect_right_now())) {
    LOG_WARN("garbage collect right now failed", K(ret));
  } else {
    LOG_DEBUG("garbage collect right now");
  }
  return ret;
}

int ObTableLockService::get_obj_lock_garbage_collector(ObOBJLockGarbageCollector *&obj_lock_garbage_collector)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTableLockService is not be inited", K(ret));
  } else {
    obj_lock_garbage_collector = &obj_lock_garbage_collector_;
  }
  return ret;
}
int ObTableLockService::process_lock_task_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  LOG_INFO("[table lock] lock_table", K(ctx));

  if (!ctx.is_in_trans_ && OB_FAIL(start_tx_(ctx))) {
    LOG_ERROR("failed to start trans", K(ret));
  } else if (ctx.is_in_trans_ && OB_FAIL(start_stmt_(ctx))) {
    LOG_WARN("start stmt failed", K(ret), K(ctx));
  } else if (!ctx.is_enable_lock_priority_ && ObTableLockPriority::NORMAL != ctx.lock_priority_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("priority should be NORMAL when disable lock_priority", K(ret), K(ctx));
  } else if (ctx.is_obj_lock_task()) {
    if (OB_FAIL(process_obj_lock_task_(ctx))) {
      LOG_WARN("lock obj failed", K(ret), K(ctx));
    }
  } else if (ctx.is_alone_tablet_lock_task()) {
    // only alone tablet should do like this.
    if (OB_FAIL(process_alone_tablet_lock_task_(ctx))) {
      LOG_WARN("process tablet lock task failed", K(ret), K(ctx));
    }
  } else {
    if (OB_FAIL(process_table_lock_task_(ctx))) {
      LOG_WARN("process table lock task failed", K(ret), K(ctx));
    }
  }
  ctx.ret_code_before_end_stmt_or_tx_ = ret;
  if (ctx.is_in_trans_ && OB_UNLIKELY(OB_SUCCESS != (tmp_ret = end_stmt_(ctx, OB_SUCCESS != ret)))) {
    LOG_WARN("failed to end stmt", K(ret), K(tmp_ret), K(ctx));
    // end stmt failed need rollback the whole trans.
    ret = (OB_SUCCESS == tmp_ret) ? ret : tmp_ret;
  } else if (!ctx.is_in_trans_ && OB_UNLIKELY(OB_SUCCESS != (tmp_ret = end_tx_(ctx, OB_SUCCESS != ret)))) {
    LOG_WARN("failed to end trans", K(ret), K(tmp_ret), K(ctx));
    ret = (OB_SUCCESS == ret) ? tmp_ret : ret;
  }
  if (ctx.is_in_trans_ && ctx.tx_is_killed_) {
    // Kill the in-transaction lock transaction.
    if (OB_SUCCESS != (tmp_ret = deal_with_deadlock_(ctx))) {
      LOG_WARN("deal with deadlock failed.", K(tmp_ret), K(ctx));
    }
  }

  LOG_INFO("[table lock] lock_table", K(ret), K(ctx));

  return ret;
}

int ObTableLockService::process_obj_lock_task_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObLockSet lock_set;

  if (ctx.obj_list_.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("obj list is empty when lock obj", K(ret), K(ctx));
  } else if (OB_FAIL(get_lock_set_(ctx, ctx.obj_list_, lock_set))) {
    LOG_WARN("get lock_set failed", K(ret), K(ctx));
  } else if (ctx.is_enable_lock_priority_ && !ctx.is_unlock_task() && OB_FAIL(process_obj_lock_with_prio_(ctx, lock_set))) {
    LOG_WARN("add obj lock into queue failed", K(ret), K(ctx));
  } else if (OB_FAIL(process_obj_lock_(ctx, lock_set))) {
    LOG_WARN("lock obj failed", K(ret), K(ctx));
  }
  return ret;
}

int ObTableLockService::process_table_lock_task_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObLockSet table_lock_set;
  ObLockID table_lock_id;
  ObTableLockMode table_lock_mode = ctx.lock_mode_;

  if (OB_FAIL(get_lock_id(ctx.table_id_, table_lock_id))) {
    LOG_WARN("get lock id failed", K(ret), K(ctx));
  } else if (is_part_table_lock_(ctx.task_type_)
             && OB_FAIL(get_table_lock_mode_(ctx.task_type_, ctx.lock_mode_, table_lock_mode))) {
    LOG_WARN("get table lock mode failed", K(ret), K(ctx), K(ctx.task_type_), K(ctx.lock_mode_));
  } else if (OB_FAIL(get_lock_set_(ctx, table_lock_id, table_lock_set))) {
    LOG_WARN("get lock_set failed", K(ret), K(ctx));
    // NOTICE:
    // When lock_priority is enabled, we need to obtain the table's schema and tablets to enqueue the corresponding
    // tablets into the locking queue for prioritized locking. However, at this point, the table is not yet locked.
    // Consequently, by the time the table is being locked, the schema and tablets might have undergone changes. In
    // response, we reacquire the schema and tablets and proceed to lock them. For newly added tablets, the requirement
    // of prioritized locking might not be fulfilled. As for deleted tablets, they are removed from the
    // locking queue upon transaction commitment.
  } else if (ctx.is_enable_lock_priority_
             && !ctx.is_unlock_task()
             && OB_FAIL(
               process_table_tablet_lock_with_prio_(ctx,
                                                    ctx.lock_mode_,
                                                    table_lock_mode,
                                                    table_lock_set))) {
    LOG_WARN("add table and tablet lock into queue failed", K(ret), K(ctx));
  } else if (OB_FAIL(process_table_tablet_lock_(ctx,
                                                ctx.lock_mode_,
                                                table_lock_mode,
                                                table_lock_set))) {
    LOG_WARN("lock table and tablet failed", K(ret), K(table_lock_mode), K(ctx));
  }
  return ret;
}

int ObTableLockService::process_alone_tablet_lock_task_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObLockSet lock_set;

  // TODO: yanyuan.cxf we may need the right schema_version while lock/unlock alone tablet.
  ctx.schema_version_ = 0;

  if (OB_FAIL(get_lock_set_(ctx, ctx.tablet_list_, lock_set))) {
    LOG_WARN("fail to get lock set", K(ret), K(ctx.get_tablet_cnt()));
  } else if (ctx.is_enable_lock_priority_ && !ctx.is_unlock_task()
             && OB_FAIL(process_obj_lock_with_prio_(ctx, lock_set))) {
    LOG_WARN("add alone tablet lock into queue failed", K(ret), K(ctx));
  } else if (OB_FAIL(process_obj_lock_(ctx, lock_set))) {
    LOG_WARN("lock alone tablet failed", K(ret), K(ctx));
  }
  return ret;
}

bool ObTableLockService::is_part_table_lock_(const ObTableLockTaskType task_type)
{
  return (LOCK_TABLET == task_type || UNLOCK_TABLET == task_type ||
          LOCK_PARTITION == task_type || UNLOCK_PARTITION == task_type ||
          LOCK_SUBPARTITION == task_type || UNLOCK_SUBPARTITION == task_type ||
          LOCK_ALONE_TABLET == task_type || UNLOCK_ALONE_TABLET == task_type ||
          REPLACE_LOCK_TABLETS == task_type || REPLACE_LOCK_ALONE_TABLET == task_type ||
          REPLACE_LOCK_PARTITION == task_type || REPLACE_LOCK_SUBPARTITION == task_type);
}

int ObTableLockService::get_table_lock_mode_(const ObTableLockTaskType task_type,
                                             const ObTableLockMode part_lock_mode,
                                             ObTableLockMode &table_lock_mode)
{
  int ret = OB_SUCCESS;
  if (is_part_table_lock_(task_type)) {
    // lock tablet.
    if (EXCLUSIVE == part_lock_mode ||
        ROW_EXCLUSIVE == part_lock_mode) {
      table_lock_mode = ROW_EXCLUSIVE;
    } else if (SHARE == part_lock_mode ||
               ROW_SHARE == part_lock_mode) {
      table_lock_mode = ROW_SHARE;
    } else if (SHARE_ROW_EXCLUSIVE == part_lock_mode) {
      // TODO: cxf lock all the tablets of this table.
      table_lock_mode = SHARE_ROW_EXCLUSIVE;
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObTableLockService::get_retry_lock_ids_(const ObLockIDArray &lock_ids,
                                            const int64_t start_pos,
                                            ObLockIDArray &retry_lock_ids)
{
  int ret = OB_SUCCESS;
  for (int64_t i = start_pos; i < lock_ids.count() && OB_SUCC(ret); ++i) {
    if (OB_FAIL(retry_lock_ids.push_back(lock_ids.at(i)))) {
      LOG_WARN("get retry tablet failed", K(ret), K(lock_ids.at(i)));
    }
  }
  return ret;
}

int ObTableLockService::get_retry_lock_ids_(const ObLockSet &lock_set,
                                            const int64_t start_pos,
                                            ObLockIDArray &retry_lock_ids)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_retry_lock_ids_(lock_set.get_lock_ids(), start_pos, retry_lock_ids))) {
    LOG_WARN("get retry lock id list failed", K(ret));
  }
  return ret;
}

int ObTableLockService::collect_rollback_info_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  ctx.mark_need_rollback();
  LOG_DEBUG("ObTableLockService::collect_rollback_info_", K(ret), K(ctx));
  return ret;
}

int ObTableLockService::collect_rollback_info_(const ObRetryCtx &retry_ctx,
                                               ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (retry_ctx.task_prepared_) {
    ctx.mark_need_rollback();
  }
  LOG_DEBUG("ObTableLockService::collect_rollback_info_", K(ret), K(ctx));
  return ret;
}

template<class LocalExecutor>
int ObTableLockService::handle_task_result_(LocalExecutor &executor,
                                                      ObTableLockCtx &ctx,
                                                      const ObLockSet &lock_set,
                                                      bool &can_retry,
                                                      ObRetryCtx &retry_ctx)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTransService *txs = share::g_mp->trans_service();

  can_retry = true;
  retry_ctx.need_retry_ = true;
  if (retry_ctx.task_executed_) {
    const ObTableLockTaskResult &result = executor.get_result();
    tmp_ret = executor.get_return_code();
    if (need_retry_whole_task_(tmp_ret)) {
      LOG_WARN("lock task failed, but we need retry", KR(tmp_ret));
      if (OB_TMP_FAIL(get_retry_lock_ids_(lock_set,
                                          0,
                                          retry_ctx.retry_lock_ids_))) {
        can_retry = false;
        retry_ctx.need_retry_ = false;
        ret = tmp_ret;
        LOG_WARN("get retry tablet list failed", KR(ret));
      }
    } else {
      if (OB_TMP_FAIL(tmp_ret)) {
        LOG_WARN("lock task failed", KR(tmp_ret));
      } else if (OB_TMP_FAIL(result.get_tx_result_code())) {
        LOG_WARN("get tx exec result failed", KR(tmp_ret));
      } else if (OB_TMP_FAIL(txs->add_tx_exec_result(*ctx.tx_desc_,
                                                     result.tx_result_))) {
        LOG_WARN("failed to add exec result", K(tmp_ret), K(ctx), K(result.tx_result_));
      }

      // Execution or transaction-result failures require statement rollback.
      if (OB_TMP_FAIL(tmp_ret)) {
        ret = tmp_ret;
        can_retry = false;
        retry_ctx.need_retry_ = false;
        (void) collect_rollback_info_(ctx);
      } else {
        tmp_ret = result.get_ret_code();
        if (need_retry_partial_task_(tmp_ret, &result)) {
          LOG_WARN("lock task failed, but we need retry", KR(tmp_ret));
          if (OB_TMP_FAIL(get_retry_lock_ids_(lock_set,
                                              result.get_success_pos() + 1,
                                              retry_ctx.retry_lock_ids_))) {
            can_retry = false;
            retry_ctx.need_retry_ = false;
            ret = tmp_ret;
            LOG_WARN("get retry tablet list failed", KR(ret));
          }
        } else if (OB_TRANS_KILLED == tmp_ret) {
          ctx.tx_is_killed_ = true;
          can_retry = false;
        } else if (OB_TMP_FAIL(tmp_ret)) {
          retry_ctx.need_retry_ = false;
          can_retry = false;
        }
        if (OB_TMP_FAIL(tmp_ret)) {
          LOG_WARN("lock task failed", K(tmp_ret));
          if (OB_SUCC(ret) || ret == OB_TRY_LOCK_ROW_CONFLICT) {
            ret = tmp_ret;
          }
        }
      }
    }
  }
  LOG_DEBUG("ObTableLockService::handle_task_result_", K(ret), K(ctx));

  return ret;
}

int ObTableLockService::pre_check_lock_(ObTableLockCtx &ctx, const ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  ret = batch_pre_check_lock_(ctx, lock_set);
  return ret;
}

// for 4.1
template<class LocalExecutor>
int ObTableLockService::execute_lock_set_in_batches_(LocalExecutor &executor,
                                                   ObTableLockCtx &ctx,
                                                   const ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  constexpr static int64_t RETRY_SET_NUM = 2;
  ObLockSet retry_sets[RETRY_SET_NUM];
  const ObLockSet *input_set = nullptr;
  ObLockSet *retry_set = nullptr;
  bool can_retry = true;
  int64_t retry_times = 1;
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    retry_set = const_cast<ObLockSet *>(&lock_set);
    do {
      input_set = retry_set;
      retry_set = &retry_sets[retry_times % RETRY_SET_NUM];
      if (OB_FAIL(retry_set->reuse())) {
        LOG_WARN("reuse retry set failed", K(ret));
      } else if (OB_FAIL(execute_lock_set_in_batches_(executor,
                                                    ctx,
                                                    *input_set,
                                                    can_retry,
                                                    *retry_set))) {
        LOG_WARN("process lock task failed", KR(ret), K(can_retry), K(ctx), K(retry_times));
      }
      if (can_retry && !retry_set->empty()) {
        retry_times++;
      }
      if (retry_times % 10 == 0) {
        LOG_WARN("retry too many times", K(retry_times), K(can_retry), K(ctx));
        LOG_WARN("retry lock data", K(lock_set));
      }
    } while (can_retry && !retry_set->empty());
  }
  return ret;
}

template<class LocalExecutor>
int ObTableLockService::execute_lock_set_once_(LocalExecutor &executor,
                                                ObTableLockCtx &ctx,
                                                const ObLockSet &lock_set,
                                                ObRetryCtx &retry_ctx)
{
  int ret = OB_SUCCESS;

  retry_ctx.reuse();
  if (!lock_set.empty()) {
    const ObLockIDArray &lock_ids = lock_set.get_lock_ids();
    if (OB_FAIL(execute_lock_task_(executor,
                               ctx,
                               lock_ids,
                               retry_ctx))) {
      LOG_WARN("execute lock task failed", K(ret));
    } else if (retry_ctx.need_retry_
               && OB_FAIL(get_retry_lock_ids_(lock_ids,
                                              0,
                                              retry_ctx.retry_lock_ids_))) {
      LOG_WARN("get retry tablet failed", KR(ret));
    }
  }
  return ret;
}

template<class LocalExecutor>
int ObTableLockService::execute_lock_set_in_batches_(LocalExecutor &executor,
                                                   ObTableLockCtx &ctx,
                                                   const ObLockSet &lock_set,
                                                   bool &can_retry,
                                                   ObLockSet &retry_lock_set)
{
  int ret = OB_SUCCESS;
  ObRetryCtx retry_ctx;

  if (OB_FAIL(execute_lock_set_once_(executor,
                                      ctx,
                                      lock_set,
                                      retry_ctx))) {
    can_retry = false;
    (void)collect_rollback_info_(retry_ctx, ctx);
    LOG_WARN("execute lock task failed", KR(ret));
  } else {
    ret = handle_task_result_(executor,
                                        ctx,
                                        lock_set,
                                        can_retry,
                                        retry_ctx);
  }

  // get the retry set
  if (can_retry && retry_ctx.retry_lock_ids_.count() != 0) {
    LOG_WARN("lock task failed, but we need retry", K(ret), K(can_retry), K(retry_ctx));
    if (OB_FAIL(fill_lock_set_(ctx,
                                  retry_ctx.retry_lock_ids_,
                                  retry_lock_set))) {
      LOG_WARN("refill lock set failed", KP(ret), K(ctx));
      can_retry = false;
    }
  }

  LOG_DEBUG("ObTableLockService::execute_lock_set_in_batches_", K(ret), K(ctx));
  return ret;
}

int ObTableLockService::batch_pre_check_lock_(ObTableLockCtx &ctx,
                                              const ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  int last_ret = OB_SUCCESS;
  int64_t USLEEP_TIME = 100; // 0.1 ms
  bool need_retry = false;
  observer::ObLocalBatchLockExecutor<ObLockTaskBatchRequest<ObLockParam>> executor(
      observer::handle_batch_lock_task);
  // only used in LOCK_TABLE/LOCK_PARTITION
  if (LOCK_TABLE == ctx.task_type_ ||
      LOCK_PARTITION == ctx.task_type_) {
    do {
      need_retry = false;
      if (ctx.is_timeout()) {
        ret = (last_ret == OB_TRY_LOCK_ROW_CONFLICT) ?
          OB_ERR_EXCLUSIVE_LOCK_CONFLICT : OB_TIMEOUT;
        LOG_WARN("process obj lock timeout", K(ret), K(ctx));
      } else {
        ret = execute_lock_set_in_batches_(executor,
                                         ctx,
                                         lock_set);
        // the process process may be timeout because left time not enough,
        // just rewrite it to OB_ERR_EXCLUSIVE_LOCK_CONFLICT
        if (is_timeout_ret_code_(ret)) {
          ret = (last_ret == OB_TRY_LOCK_ROW_CONFLICT) ?
            OB_ERR_EXCLUSIVE_LOCK_CONFLICT : OB_TIMEOUT;
          LOG_WARN("process obj lock timeout", K(ret), K(ctx));
        }
      }

      if (!ctx.is_try_lock() &&
          ctx.is_deadlock_avoid_enabled() &&
          OB_TRY_LOCK_ROW_CONFLICT == ret) {
        ret = OB_TRANS_KILLED;
        ctx.tx_is_killed_ = true;
      }
      if (ret == OB_TRY_LOCK_ROW_CONFLICT) {
        if (ctx.is_try_lock()) {
          ret = OB_ERR_EXCLUSIVE_LOCK_CONFLICT;
          LOG_INFO("try lock and meet conflict", K(ret), K(ctx));
        } else if (OB_UNLIKELY(ctx.is_timeout())) {
          ret = OB_ERR_EXCLUSIVE_LOCK_CONFLICT;
          LOG_WARN("lock table timeout", K(ret), K(ctx));
        } else {
          need_retry = true;
          last_ret = ret;
          ret = OB_SUCCESS;
          ob_usleep(USLEEP_TIME);
        }
      }
    } while (need_retry);  // retry task level
    LOG_DEBUG("ObTableLockService::pre_check_lock_", K(ret), K(ctx));
  }
  return ret;
}

int ObTableLockService::deal_with_deadlock_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  SessionGuard session_guard;
  const uint32_t sess_id = ctx.tx_desc_->get_session_id();
  if (OB_FAIL(ObTransDeadlockDetectorAdapter::get_session_info(sess_id, session_guard))) {
    LOG_WARN("get session info failed", K(ret), K(sess_id));
  } else if (!session_guard.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session guard invalid", K(ret), K(sess_id));
  } else {
    ret = ObTransDeadlockDetectorAdapter::kill_tx(sess_id);
  }
  if (!OB_SUCC(ret)) {
    LOG_WARN("kill trans or stmt failed", K(ret), K(sess_id));
  }
  LOG_DEBUG("ObTableLockService::deal_with_deadlock_", K(ret), K(sess_id));
  return ret;
}

int ObTableLockService::get_table_partition_level_(const ObTableID table_id,
                                                  ObPartitionLevel &part_level)
{
  int ret = OB_SUCCESS;
  ObSimpleTableSchemaV2 *table_schema = nullptr;
  ObArenaAllocator allocator("TableSchema");

  if (OB_FAIL(ObSchemaUtils::get_latest_table_schema(
      *sql_proxy_,
      allocator,
      table_id,
      table_schema))) {
    LOG_WARN("can not get table schema", K(ret), K(table_id));
  } else {
    part_level = table_schema->get_part_level();
  }
  return ret;
}

int ObTableLockService::pack_batch_request_(ObTableLockCtx &ctx,
                                            const ObTableLockTaskType task_type,
                                            const ObLockIDArray &lock_ids,
                                            ObLockTaskBatchRequest<ObLockParam> &request)
{
  int ret = OB_SUCCESS;
  ObLockParam lock_param;

  if (OB_FAIL(request.init(task_type, ctx.tx_desc_))) {
    LOG_WARN("request init failed", K(ret), K(ctx), KP(ctx.tx_desc_), K(lock_ids), K(task_type));
  } else {
    for (int i = 0; i < lock_ids.count() && OB_SUCC(ret); ++i) {
      lock_param.reset();
      if (ctx.is_enable_lock_priority_) {
        lock_param.is_two_phase_lock_ = true;
        lock_param.lock_priority_ = ctx.lock_priority_;
      }
      if (OB_FAIL(lock_param.set(lock_ids[i],
                                 ctx.lock_mode_,
                                 ctx.lock_owner_,
                                 ctx.lock_op_type_,
                                 ctx.schema_version_,
                                 ctx.is_deadlock_avoid_enabled(),
                                 ctx.is_try_lock(),
                                 ctx.abs_timeout_ts_,
                                 ctx.is_for_replace_))) {
        LOG_WARN("get lock param failed", K(ret));
      } else if (OB_FAIL(request.params_.push_back(lock_param))) {
        LOG_WARN("get lock request failed", K(ret), K(lock_param));
      }
    }
  }
  return ret;
}

int ObTableLockService::pack_batch_request_(ObTableLockCtx &ctx,
                                            const ObTableLockTaskType task_type,
                                            const ObLockIDArray &lock_ids,
                                            ObLockTaskBatchRequest<ObReplaceLockParam> &request)
{
  int ret = OB_SUCCESS;
  ObReplaceLockParam lock_param;
  if (OB_FAIL(request.init(task_type, ctx.tx_desc_))) {
    LOG_WARN("request init failed", K(ret), K(ctx), KP(ctx.tx_desc_), K(lock_ids), K(task_type));
  } else if (!ctx.is_replace_task()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("lock_param is not compatible with request", K(ret), K(ctx), K(task_type));
  } else {
    const ObReplaceTableLockCtx &replace_ctx = static_cast<const ObReplaceTableLockCtx &>(ctx);
    for (int i = 0; i < lock_ids.count() && OB_SUCC(ret); ++i) {
      lock_param.reset();
      if (OB_FAIL(replace_ctx.get_lock_param(lock_ids[i], lock_param))) {
        LOG_WARN("get lock param failed", K(ret));
      } else if (OB_FAIL(request.params_.push_back(lock_param))) {
        LOG_WARN("get lock request failed", K(ret), K(lock_param));
      }
    }
  }
  return ret;
}


template<class LocalExecutor>
int ObTableLockService::execute_lock_set_(LocalExecutor &executor,
                                          ObTableLockCtx &ctx,
                                          const ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  constexpr static int64_t RETRY_SET_NUM = 2;
  ObLockSet retry_sets[RETRY_SET_NUM];
  const ObLockSet *input_set = nullptr;
  ObLockSet *retry_set = nullptr;
  bool can_retry = true;
  int64_t retry_times = 1;
  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    retry_set = const_cast<ObLockSet *>(&lock_set);
    do {
      input_set = retry_set;
      retry_set = &retry_sets[retry_times % RETRY_SET_NUM];
      if (OB_FAIL(retry_set->reuse())) {
        LOG_WARN("reuse retry set failed", K(ret));
      } else if (OB_FAIL(execute_lock_set_(executor,
                                           ctx,
                                           *input_set,
                                           can_retry,
                                           *retry_set))) {
        LOG_WARN("process lock task failed", KR(ret), K(ctx), K(retry_times));
      }
      if (can_retry && !retry_set->empty()) {
        retry_times++;
      }
      if (retry_times % 10 == 0) {
        LOG_WARN("retry too many times", K(retry_times), K(ctx), K(retry_set->size()));
      }
    } while (can_retry && !retry_set->empty());
  }
  return ret;
}

template <class LocalExecutor>
int ObTableLockService::execute_lock_task_(LocalExecutor &executor,
                                       ObTableLockCtx &ctx,
                                       const ObLockIDArray &lock_ids,
                                       ObRetryCtx &retry_ctx)
{
  int ret = OB_SUCCESS;

  retry_ctx.need_retry_ = false;
  if (OB_UNLIKELY(retry_ctx.task_prepared_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("single table lock task already prepared", K(ret), K(retry_ctx));
  } else {
    retry_ctx.task_prepared_ = true;
    if (ctx.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("process obj lock timeout", K(ret), K(ctx));
    } else {
      ret = pack_and_execute_task_(executor, ctx, lock_ids, retry_ctx);
    }
  }

  if (OB_FAIL(ret)) {
    retry_ctx.need_retry_ = false;
  }
  return ret;
}

template <typename LocalExecutor>
int ObTableLockService::pack_and_execute_task_(LocalExecutor &executor,
                                           ObTableLockCtx &ctx,
                                           const ObLockIDArray &lock_ids,
                                           ObRetryCtx &retry_ctx)
{
  int ret = OB_SUCCESS;
  ObLockTaskBatchRequest<ObLockParam> request;
  if (OB_FAIL(pack_batch_request_(ctx, ctx.task_type_, lock_ids, request))) {
    LOG_WARN("pack_batch_request_ failed", K(ret), K(ctx), K(lock_ids));
  } else if (OB_UNLIKELY(retry_ctx.task_executed_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("single table lock task already executed", K(ret), K(retry_ctx));
  } else if (OB_FAIL(executor.execute(request))) {
    LOG_WARN("failed to execute local lock task", KR(ret), K(ctx.abs_timeout_ts_), K(request));
  } else {
    retry_ctx.task_executed_ = true;
    ALLOW_NEXT_LOG();
    LOG_INFO("execute table lock task", KR(ret), K(retry_ctx), "request", request);
  }
  return ret;
}

template <>
int ObTableLockService::pack_and_execute_task_(observer::ObLocalBatchLockExecutor<ObLockTaskBatchRequest<ObReplaceLockParam>> &executor,
                                           ObTableLockCtx &ctx,
                                           const ObLockIDArray &lock_ids,
                                           ObRetryCtx &retry_ctx)
{
  int ret = OB_SUCCESS;
  ObLockTaskBatchRequest<ObReplaceLockParam> request;
  if (OB_FAIL(pack_batch_request_(ctx, ctx.task_type_, lock_ids, request))) {
    LOG_WARN("pack_batch_request_ failed", K(ret), K(ctx), K(lock_ids));
  } else if (OB_UNLIKELY(retry_ctx.task_executed_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("single table lock task already executed", K(ret), K(retry_ctx));
  } else if (OB_FAIL(executor.execute(request))) {
    LOG_WARN("failed to execute local lock task", KR(ret), K(ctx.abs_timeout_ts_), K(request));
  } else {
    retry_ctx.task_executed_ = true;
    ALLOW_NEXT_LOG();
    LOG_INFO("execute table lock task", KR(ret), K(retry_ctx), "request", request);
  }
  return ret;
}

template <class LocalExecutor>
int ObTableLockService::execute_one_lock_task_(LocalExecutor &executor,
                                           ObTableLockCtx &ctx,
                                           const ObLockIDArray &lock_ids,
                                           ObRetryCtx &retry_ctx)
{
  int ret = OB_SUCCESS;

  retry_ctx.reuse();
  if (OB_FAIL(execute_lock_task_(executor,
                             ctx,
                             lock_ids,
                             retry_ctx))) {
    LOG_WARN("execute lock task failed", K(ret));
  } else if (retry_ctx.need_retry_) {
    if (OB_FAIL(get_retry_lock_ids_(lock_ids,
                                    0,
                                    retry_ctx.retry_lock_ids_))) {
      retry_ctx.need_retry_ = false;
      LOG_WARN("get retry tablet failed", KR(ret));
    }
  }
  return ret;
}

template<class LocalExecutor>
int ObTableLockService::execute_lock_set_(LocalExecutor &executor,
                                          ObTableLockCtx &ctx,
                                          const ObLockSet &lock_set,
                                          bool &can_retry,
                                          ObLockSet &retry_lock_set)
{
  int ret = OB_SUCCESS;
  ObLockIDArray retry_lock_ids;
  ObRetryCtx retry_ctx;
  if (!lock_set.empty()) {
    retry_ctx.reuse();
    const ObLockIDArray &lock_ids = lock_set.get_lock_ids();

    if (OB_FAIL(execute_one_lock_task_(executor,
                                   ctx,
                                   lock_ids,
                                   retry_ctx))) {
      can_retry = false;
      (void)collect_rollback_info_(retry_ctx, ctx);
      LOG_WARN("execute lock task failed", KR(ret));
    } else {
      ret = handle_task_result_(executor,
                                          ctx,
                                          lock_set,
                                          can_retry,
                                          retry_ctx);
    }
    // Collect retry tablets from the local task result.
    if (can_retry) {
      if (OB_FAIL(get_retry_lock_ids_(retry_ctx.retry_lock_ids_,
                                      0,
                                      retry_lock_ids))) {
        can_retry = false;
        LOG_WARN("get retry tablet list failed", K(ret));
      }
    }
  }
  // get the retry set
  if (can_retry && retry_lock_ids.count() != 0) {
    if (OB_FAIL(fill_lock_set_(ctx,
                                         retry_lock_ids,
                                         retry_lock_set))) {
      LOG_WARN("refill lock set failed", KP(ret), K(ctx));
      can_retry = false;
    }
  }
  return ret;
}

int ObTableLockService::inner_process_obj_lock_batch_(ObTableLockCtx &ctx,
                                                      const ObLockSet &lock_map)
{
  int ret = OB_SUCCESS;
  if (ctx.is_unlock_task()) {
    observer::ObLocalBatchLockExecutor<ObLockTaskBatchRequest<ObLockParam>> executor(
        observer::handle_high_priority_batch_lock_task);
    ret = execute_lock_set_(executor, ctx, lock_map);
  } else if (ctx.is_replace_task()) {
    observer::ObLocalBatchLockExecutor<ObLockTaskBatchRequest<ObReplaceLockParam>> executor(
        observer::handle_batch_replace_lock_task);
    ret = execute_lock_set_(executor, ctx, lock_map);
  } else {
    observer::ObLocalBatchLockExecutor<ObLockTaskBatchRequest<ObLockParam>> executor(
        observer::handle_batch_lock_task);
    ret = execute_lock_set_(executor, ctx, lock_map);
  }
  return ret;
}

int ObTableLockService::process_table_tablet_lock_with_prio_(ObTableLockCtx &ctx,
                                                             const ObTableLockMode lock_mode,
                                                             const ObTableLockMode table_lock_mode,
                                                             const ObLockSet &table_lock_set)
{
  int ret = OB_SUCCESS;
  ObLockSet tablet_lock_set;

  ctx.schema_version_ = 0;
  ctx.lock_mode_ = table_lock_mode;

  if (OB_FAIL(process_obj_lock_with_prio_(ctx, table_lock_set))) {
    LOG_WARN("lock table failed", K(ret), K(ctx), K(lock_mode));
  } else if (OB_FAIL(get_tablet_lock_set_(lock_mode, ctx, tablet_lock_set))) {
    LOG_WARN("failed to get_tablet_lock_set_", K(ret), K(ctx), K(lock_mode));
  } else if (FALSE_IT(ctx.lock_mode_ = lock_mode)) {
  } else if (OB_FAIL(process_obj_lock_with_prio_(ctx, tablet_lock_set))) {
    LOG_WARN("lock tablet failed", K(ret), K(ctx));
  }
  LOG_DEBUG("ObTableLockService::process_table_tablet_lock_with_prio_",
            K(ret),
            K(ctx),
            K(lock_mode),
            K(table_lock_mode));
  return ret;
}


int ObTableLockService::process_table_tablet_lock_(ObTableLockCtx &ctx,
                                                   const ObTableLockMode lock_mode,
                                                   const ObTableLockMode table_lock_mode,
                                                   const ObLockSet &table_lock_set)
{
  int ret = OB_SUCCESS;
  ObLockSet tablet_lock_set;

  ctx.schema_version_ = 0;
  ctx.lock_mode_ = table_lock_mode;

  if (OB_FAIL(process_obj_lock_(ctx, table_lock_set))) {
    LOG_WARN("lock table failed", K(ret), K(ctx), K(table_lock_mode));
  }
  DEBUG_SYNC(TABLE_LOCK_AFTER_LOCK_TABLE_BEFORE_LOCK_TABLET);
  if (FAILEDx(get_tablet_lock_set_(lock_mode, ctx, tablet_lock_set))) {
    LOG_WARN("get tablet lock_set failed", K(ret), K(ctx), K(lock_mode));
  } else if (FALSE_IT(ctx.lock_mode_ = lock_mode)) {
  } else  if (!ctx.is_enable_lock_priority_ && OB_FAIL(pre_check_lock_(ctx, tablet_lock_set))) {
    LOG_WARN("failed to pre_check_lock_", K(ret), K(ctx), K(lock_mode));
  } else if (OB_FAIL(process_obj_lock_(ctx, tablet_lock_set))) {
    LOG_WARN("lock tablet failed", K(ret), K(ctx), K(lock_mode));
  }
  return ret;
}

int ObTableLockService::process_obj_lock_with_prio_(ObTableLockCtx &ctx,
                                                    const ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  const ObTableLockTaskType ori_task_type = ctx.task_type_;

  if (OB_UNLIKELY(!ctx.can_execute_push_lock_task())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("can not push lock", K(ret), K(ctx));
  } else {
    if (ctx.task_type_ == ObTableLockTaskType::LOCK_ALONE_TABLET) {
      ctx.task_type_ = ObTableLockTaskType::ADD_LOCK_INTO_QUEUE_WITHOUT_CHECK;
    } else {
      ctx.task_type_ = ObTableLockTaskType::ADD_LOCK_INTO_QUEUE;
    }
    if (OB_FAIL(process_obj_lock_(ctx, lock_set))) {
      LOG_WARN("process_obj_lock_ failed", K(ret), K(ctx));
    }
    ctx.task_type_ = ori_task_type;
  }
  LOG_DEBUG("ObTableLockService::process_obj_lock_with_prio_", K(ret), K(ctx));

  return ret;
}

int ObTableLockService::process_obj_lock_(ObTableLockCtx &ctx,
                                          const ObLockSet &lock_set)

{
  int ret = OB_SUCCESS;
  bool need_retry = false;
  do {
    need_retry = false;

    if (ctx.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("lock table timeout", K(ret), K(ctx));
    } else if (OB_FAIL(start_sub_tx_(ctx))) {
      LOG_WARN("failed to start sub tx", K(ret), K(ctx));
    } else if (OB_FAIL(inner_process_obj_lock_batch_(ctx,
                                                     lock_set))) {
      LOG_WARN("fail to lock tablets", K(ret));
      need_retry = need_retry_single_task_(ctx, ret);
      // rollback the sub tx and overwrite the ret code.
      if (need_retry && OB_FAIL(end_sub_tx_(ctx, true /*rollback*/))) {
        LOG_WARN("failed to rollback sub tx", K(ret), K(ctx));
      }
    } else if (OB_FAIL(end_sub_tx_(ctx, false /*not rollback*/))) {
      LOG_WARN("failed to end sub tx", K(ret), K(ctx));
    }
  } while (need_retry && OB_SUCC(ret));
  LOG_DEBUG("ObTableLockService::process_obj_lock_", K(ret), K(ctx));
  return ret;
}

int ObTableLockService::check_op_allowed_(const uint64_t table_id,
                                          const ObSimpleTableSchemaV2 *table_schema,
                                          bool &is_allowed)
{
  int ret = OB_SUCCESS;
  

  is_allowed = true;

  if (!table_schema->is_user_table()
      && !table_schema->is_tmp_table()
      && !ObInnerTableLockUtil::in_inner_table_lock_white_list(table_id)) {
    // all the tmp table is a normal table now, deal it as a normal user table
    // table lock not support virtual table/sys table(not in white list) etc.
    is_allowed = false;
  } else {
    bool is_primary = true;
    if (OB_FAIL(ObShareUtil::check_if_server_role_is_primary(is_primary))) {
      LOG_WARN("fail to execute check_if_server_role_is_primary", KR(ret));
    } else if (!is_primary) {
      is_allowed = false;
    }
  }
  return ret;
}

int ObTableLockService::get_process_tablets_(const ObSimpleTableSchemaV2 *table_schema,
                                             ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;

  if (ctx.is_tablet_lock_task()) {
    // case 1: lock/unlock tablet
    // do nothing
  } else {
    ctx.tablet_list_.reuse();
    if (LOCK_PARTITION == ctx.task_type_ || UNLOCK_PARTITION == ctx.task_type_ || REPLACE_LOCK_PARTITION == ctx.task_type_) {
      // case 2: lock/unlock partition
      // get all the tablet of this partition.
      ObObjectID part_id(ctx.partition_id_);
      if (OB_FAIL(table_schema->get_tablet_ids_by_part_object_id(part_id,
                                                                  ctx.tablet_list_))) {
        LOG_WARN("failed to get tablet ids", K(ret), K(part_id));
      }
    } else if (LOCK_SUBPARTITION == ctx.task_type_ || UNLOCK_SUBPARTITION == ctx.task_type_ || REPLACE_LOCK_SUBPARTITION == ctx.task_type_) {
      // case 3: lock/unlock subpartition
      // get the tablet of subpartition
      ObObjectID part_id(ctx.partition_id_);
      ObTabletID tablet_id;
      if (OB_FAIL(table_schema->get_tablet_id_by_object_id(part_id,
                                                            tablet_id))) {
        LOG_WARN("failed to get tablet id", K(ret), K(part_id));
      } else if (OB_FAIL(ctx.tablet_list_.push_back(tablet_id))) {
        LOG_WARN("failed to push back tablet id", K(ret));
      }
    } else if ((LOCK_TABLE == ctx.task_type_ || UNLOCK_TABLE == ctx.task_type_)
               && is_need_lock_tablet_mode(ctx.lock_mode_)) {
      // case 4: lock/unlock table
      // get all the tablet of this table.
      if (OB_FAIL(table_schema->get_tablet_ids(ctx.tablet_list_))) {
        LOG_WARN("failed to get tablet ids", K(ret));
      }
    } else if (REPLACE_LOCK_TABLE == ctx.task_type_) {
      // case 5: replace lock table
      const ObReplaceTableLockCtx &replace_ctx = static_cast<const ObReplaceTableLockCtx &>(ctx);
      // should check both the original lock_mode and the target lock_mode
      if (is_need_lock_tablet_mode(replace_ctx.lock_mode_) || is_need_lock_tablet_mode(replace_ctx.new_lock_mode_)) {
        // get all the tablet of this table.
        if (OB_FAIL(table_schema->get_tablet_ids(ctx.tablet_list_))) {
          LOG_WARN("failed to get tablet ids", K(ret));
        }
      }
    } else {
      // do nothing
    }
  }
  LOG_DEBUG("ObTableLockService::get_process_tablets_", K(ret), K(ctx.task_type_), K(ctx));

  return ret;
}

int ObTableLockService::fill_lock_set_(ObTableLockCtx &ctx,
                                          const ObLockIDArray &lock_ids,
                                          ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  if (OB_FAIL(lock_set.reuse())) {
    LOG_WARN("fail to reuse lock set", KR(ret));
  } else {
    for (int64_t i = 0; i < lock_ids.count() && OB_SUCC(ret); ++i) {
      const ObLockID &lock_id = lock_ids.at(i);
      if (OB_FAIL(lock_set.push_back(lock_id))) {
        LOG_WARN("push_back lock_id failed", K(ret), K(lock_id));
      }
      LOG_DEBUG("lock added to lock set", K(lock_id), K(i));
    }
  }

  return ret;
}

int ObTableLockService::fill_lock_set_(ObTableLockCtx &ctx,
                                         const common::ObTabletIDArray &tablets,
                                         ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  ObLockID lock_id;
  if (OB_FAIL(lock_set.reuse())) {
    LOG_WARN("fail to reuse lock set", KR(ret));
  } else {
    for (int64_t i = 0; i < tablets.count() && OB_SUCC(ret); ++i) {
      lock_id.reset();
      const ObTabletID &tablet_id = tablets.at(i);
      if (OB_FAIL(get_lock_id(tablet_id,
                                     lock_id))) {
        LOG_WARN("get lock id failed", K(ret), K(ctx));
      } else if (OB_FAIL(lock_set.push_back(lock_id))) {
        LOG_WARN("push_back lock_id failed", K(ret), K(lock_id));
      }
      LOG_DEBUG("tablet added to lock set", K(lock_id), K(tablet_id), K(i));
    }
  }

  return ret;
}

int ObTableLockService::get_tablet_lock_set_(const ObTableLockMode lock_mode,
                                                ObTableLockCtx &ctx,
                                                ObLockSet &tablet_lock_set)
{
  int ret = OB_SUCCESS;
  ObSimpleTableSchemaV2 *table_schema = nullptr;
  ObArenaAllocator allocator("TableSchema");
  bool is_allowed = false;

  if (OB_FAIL(get_table_schema_(ctx, allocator, table_schema))) {
    LOG_WARN("failed to get table_schema", K(ret), K(ctx));
  } else if (OB_FAIL(check_op_allowed_(ctx.table_id_, table_schema, is_allowed))) {
    LOG_WARN("failed to check op allowed", K(ret), K(ctx));
  } else if (!is_allowed) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("lock table not allowed now", K(ret), K(ctx));
  } else if (FALSE_IT(ctx.schema_version_ = table_schema->get_schema_version())) {
  } else if (OB_FAIL(get_process_tablets_(table_schema, ctx))) {
    LOG_WARN("failed to get parts", K(ret), K(ctx));
  } else if (OB_FAIL(get_lock_set_(ctx, ctx.tablet_list_, tablet_lock_set))) {
    LOG_WARN("fail to get lock set", K(ret), K(ctx.get_tablet_cnt()));
  }
  return ret;
}

int ObTableLockService::get_lock_set_(ObTableLockCtx &ctx,
                                         const ObLockID &lock_id,
                                         ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(lock_set.reuse())) {
    LOG_WARN("fail to reuse lock set", KR(ret));
  } else if (OB_FAIL(lock_set.push_back(lock_id))) {
    LOG_WARN("push_back lock_id failed", K(ret), K(lock_id));
  }
  return ret;
}

int ObTableLockService::get_lock_set_(ObTableLockCtx &ctx,
                                         const common::ObIArray<ObLockID> &lock_ids,
                                         ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(lock_set.reuse())) {
    LOG_WARN("fail to reuse lock set", KR(ret));
  } else if (OB_FAIL(lock_set.assign(lock_ids))) {
    LOG_WARN("assign lock_ids failed", K(ret), K(lock_ids));
  }
  return ret;
}

int ObTableLockService::get_lock_set_(ObTableLockCtx &ctx,
                                         const common::ObTabletIDArray &tablets,
                                         ObLockSet &lock_set)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(fill_lock_set_(ctx, tablets, lock_set))) {
    LOG_WARN("fill lock set failed", KR(ret));
  }

  return ret;
}

bool ObTableLockService::need_retry_trans_(const ObTableLockCtx &ctx,
                                           const int64_t ret) const
{
  bool need_retry = false;
  if (ctx.is_in_trans_) {
  } else {
    // only anonymous can retry
    // retry condition 1
    need_retry = (ctx.tx_is_killed_ &&
                  !ctx.is_try_lock() &&
                  !ctx.is_timeout());
    // retry condition 2
    need_retry = need_retry || (OB_TABLET_NOT_EXIST == ret && !ctx.is_timeout());
  }
  return need_retry;
}

bool ObTableLockService::need_retry_single_task_(const ObTableLockCtx &ctx,
                                                 const int64_t ret) const
{
  bool need_retry = false;
  if (ctx.is_in_trans_) {
    need_retry = (OB_TABLET_NOT_EXIST == ret);
  } else {
    // TODO: yanyuan.cxf multi data source can not rollback, so we can not retry.
  }
  return need_retry;
}

bool ObTableLockService::need_retry_whole_task_(const int ret)
{
  return (OB_SERVER_RUNTIME_NOT_READY == ret);
}

bool ObTableLockService::need_retry_partial_task_(const int ret,
                                                  const ObTableLockTaskResult *result) const
{
  bool need_retry = false;
  need_retry = (OB_TABLET_NOT_EXIST == ret);
  need_retry = need_retry && result->can_retry();
  // Retry if the tablet is being created and the lock task can retry.
  return need_retry;
}

int ObTableLockService::rewrite_return_code_(const int ret, const int ret_code_before_end_stmt_or_tx, const bool is_from_sql) const
{
  int rewrite_rcode = ret;
  if (is_from_sql) {
    if (is_lock_conflict_ret_code_(ret_code_before_end_stmt_or_tx) && is_timeout_ret_code_(ret)) {
      rewrite_rcode = OB_ERR_EXCLUSIVE_LOCK_CONFLICT;
    }
  } else if (is_can_retry_err_(ret)) {
    // rewrite to OB_EAGAIN, to make sure the ddl process will retry again.
    rewrite_rcode = OB_EAGAIN;
  }
  return rewrite_rcode;
}

bool ObTableLockService::is_lock_conflict_ret_code_(const int ret) const
{
  return (OB_TRY_LOCK_ROW_CONFLICT == ret || OB_ERR_EXCLUSIVE_LOCK_CONFLICT == ret);
}

bool ObTableLockService::is_timeout_ret_code_(const int ret) const
{
  return (OB_TIMEOUT == ret || OB_TRANS_TIMEOUT == ret ||
          OB_TRANS_STMT_TIMEOUT == ret);
}

bool ObTableLockService::is_can_retry_err_(const int ret) const
{
  return (OB_TRANS_KILLED == ret || OB_OBJ_UNLOCK_CONFLICT == ret || OB_OBJ_LOCK_NOT_COMPLETED == ret
          || OB_TRY_LOCK_ROW_CONFLICT == ret || OB_ERR_EXCLUSIVE_LOCK_CONFLICT == ret
          || OB_TIMEOUT == ret || OB_TRANS_CTX_NOT_EXIST == ret);
}

int ObTableLockService::start_tx_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObTxParam &tx_param = ctx.tx_param_;
  tx_param.access_mode_ = ObTxAccessMode::RW;
  tx_param.isolation_ = ObTxIsolationLevel::RC;
  tx_param.timeout_us_ = common::max(static_cast<int64_t>(0), ctx.abs_timeout_ts_ - ObTimeUtility::current_time());
  tx_param.lock_timeout_us_ = -1; // use abs_timeout_ts as lock wait timeout
  // no session id here

  ObTransService *txs = share::g_mp->trans_service();
  if (ctx.trans_state_.is_start_trans_executed()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("start_trans is executed", K(ret));
  } else if (OB_FAIL(txs->acquire_tx(ctx.tx_desc_))) {
    LOG_WARN("fail acquire txDesc", K(ret), K(tx_param));
  } else {
    if (OB_FAIL(txs->start_tx(*ctx.tx_desc_, tx_param))) {
      LOG_WARN("fail start trans", K(ret), K(tx_param));
    } else {
      ctx.trans_state_.set_start_trans_executed(true);
    }
    // start tx failed, release the txDesc I just created.
    if (OB_FAIL(ret)) {
      if (OB_TMP_FAIL(txs->release_tx(*ctx.tx_desc_))) {
        LOG_ERROR("release tx failed", K(tmp_ret), KPC(ctx.tx_desc_));
      }
    }
  }

  LOG_DEBUG("ObTableLockService::start_tx_", K(ret), K(ctx), K(tx_param));
  return ret;
}

int ObTableLockService::end_tx_(ObTableLockCtx &ctx, const bool is_rollback)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (!ctx.trans_state_.is_start_trans_executed()
      || !ctx.trans_state_.is_start_trans_success()) {
    LOG_INFO("end_trans skip", K(ret), K(ctx));
  } else {
    ObTransService *txs = share::g_mp->trans_service();
    const int64_t stmt_timeout_ts = ctx.abs_timeout_ts_;
    if (is_rollback) {
      if (OB_FAIL(txs->rollback_tx(*ctx.tx_desc_))) {
        LOG_WARN("fail rollback tx when session terminate",
                 K(ret), KPC(ctx.tx_desc_), K(stmt_timeout_ts));
      }
    } else {
      ACTIVE_SESSION_FLAG_SETTER_GUARD(in_committing);
      if (OB_FAIL(txs->commit_tx(*ctx.tx_desc_, stmt_timeout_ts))) {
        LOG_WARN("fail end trans when session terminate",
                K(ret), KPC(ctx.tx_desc_), K(stmt_timeout_ts));
      }
    }
    if (OB_TMP_FAIL(txs->release_tx(*ctx.tx_desc_))) {
      LOG_ERROR("release tx failed", K(ret), K(tmp_ret), KPC(ctx.tx_desc_));
    }
    ctx.tx_desc_ = NULL;
    ctx.trans_state_.clear_start_trans_executed();
  }

  ctx.trans_state_.reset();
  LOG_DEBUG("ObTableLockService::end_tx_", K(ret), K(tmp_ret), K(ctx), K(is_rollback));

  return ret;
}

int ObTableLockService::start_sub_tx_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;

  if (ctx.is_savepoint_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("start_sub_tx is executed", K(ret));
  } else {
    ObTransService *txs = share::g_mp->trans_service();
    const ObTxParam &tx_param = ctx.tx_param_;
    const ObTxIsolationLevel &isolation_level = tx_param.isolation_;
    const int64_t expire_ts = ctx.abs_timeout_ts_;
    auto &savepoint = ctx.current_savepoint_;
    if (OB_FAIL(txs->create_implicit_savepoint(*ctx.tx_desc_,
                                               tx_param,
                                               savepoint))) {
      ctx.reset_savepoint();
      LOG_WARN("create implicit savepoint failed", K(ret), KPC(ctx.tx_desc_), K(tx_param));
    }
  }
  LOG_DEBUG("ObTableLockService::start_sub_tx_", K(ret), K(ctx));

  return ret;
}

int ObTableLockService::end_sub_tx_(ObTableLockCtx &ctx, const bool is_rollback)
{
  int ret = OB_SUCCESS;

  if (!ctx.is_savepoint_valid()) {
    LOG_INFO("end_sub_tx_ skip", K(ret), K(ctx));
  } else {
    const auto &savepoint = ctx.current_savepoint_;
    const int64_t expire_ts = OB_MAX(ctx.abs_timeout_ts_, DEFAULT_TIMEOUT_US + ObTimeUtility::current_time());
    ObTransService *txs = share::g_mp->trans_service();
	    if (is_rollback &&
	        OB_FAIL(txs->rollback_to_implicit_savepoint(*ctx.tx_desc_,
	                                                    savepoint,
	                                                    expire_ts,
	                                                    ctx.need_rollback()))) {
	      LOG_WARN("fail to rollback sub tx", K(ret), K(ctx.tx_desc_),
	               K(ctx.need_rollback_));
	    }

    ctx.clear_need_rollback();
    ctx.reset_savepoint();
  }
  LOG_DEBUG("ObTableLockService::end_sub_tx_", K(ret), K(ctx));

  return ret;
}

int ObTableLockService::start_stmt_(ObTableLockCtx &ctx)
{
  int ret = OB_SUCCESS;

  if (ctx.is_stmt_savepoint_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("start_stmt_ is executed", K(ret));
  } else {
    ObTransService *txs = share::g_mp->trans_service();
    const ObTxParam &tx_param = ctx.tx_param_;
    const ObTxIsolationLevel &isolation_level = tx_param.isolation_;
    const int64_t expire_ts = ctx.abs_timeout_ts_;
    auto &savepoint = ctx.stmt_savepoint_;
    if (OB_FAIL(txs->create_implicit_savepoint(*ctx.tx_desc_,
                                               tx_param,
                                               savepoint))) {
      ctx.reset_stmt_savepoint();
      LOG_WARN("create implicit savepoint failed", K(ret), KPC(ctx.tx_desc_), K(tx_param));
    }
  }
  LOG_DEBUG("ObTableLockService::start_stmt_", K(ret), K(ctx));

  return ret;
}

int ObTableLockService::end_stmt_(ObTableLockCtx &ctx, const bool is_rollback)
{
  int ret = OB_SUCCESS;

  if (!ctx.is_stmt_savepoint_valid()) {
    LOG_INFO("end_stmt_ skip", K(ret), K(ctx));
  } else {
    const auto &savepoint = ctx.stmt_savepoint_;
    const int64_t expire_ts = OB_MAX(ctx.abs_timeout_ts_, DEFAULT_TIMEOUT_US + ObTimeUtility::current_time());
    ObTransService *txs = share::g_mp->trans_service();
    // just rollback the whole stmt, if it is needed.
	    if (is_rollback &&
	        OB_FAIL(txs->rollback_to_implicit_savepoint(*ctx.tx_desc_,
	                                                    savepoint,
	                                                    expire_ts,
	                                                    ctx.need_rollback()))) {
	      LOG_WARN("fail to rollback stmt", K(ret), K(ctx.tx_desc_),
	               K(ctx.need_rollback_));
	    }
    LOG_DEBUG("ObTableLockService::end_stmt_", K(ret), K(ctx), K(is_rollback));
    ctx.clear_need_rollback();
    ctx.reset_stmt_savepoint();
  }

  return ret;
}

int ObTableLockService::get_table_schema_(const ObTableLockCtx &ctx,
                                          common::ObIAllocator &allocator,
                                          ObSimpleTableSchemaV2 *&table_schema)
{
  int ret = OB_SUCCESS;
  

  if (OB_UNLIKELY(ctx.is_alone_tablet_lock_task() || ctx.is_obj_lock_task())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only get schema_version for LOCK TABLE and UNLOCK TABLE request", K(ret), K(ctx));
  } else if (OB_FAIL(ObSchemaUtils::get_latest_table_schema(
               *sql_proxy_, allocator, ctx.table_id_, table_schema))) {
    if (OB_TABLE_NOT_EXIST == ret) {
      LOG_INFO("table not exist, check whether it meets expectations", K(ret), K(ctx));
    } else {
      LOG_WARN("get table schema failed", K(ret), K(ctx));
    }
  } else if (OB_UNLIKELY(OB_ISNULL(table_schema))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table_schema is null", K(ret), K(ctx));
  }
  return ret;
}

} // tablelock
} // transaction
} // oceanbase
