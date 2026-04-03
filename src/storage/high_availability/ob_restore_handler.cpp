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

#define USING_LOG_PREFIX STORAGE

#include "ob_restore_handler.h"
#include "ob_restore_dag_net.h"
#include "ob_restore_complete_dag_net.h"
#include "lib/time/ob_time_utility.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/utility/ob_tracepoint.h"
#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_errno.h"
#include "share/ob_io_device_helper.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "observer/omt/ob_tenant.h"
#include "observer/ob_server_event_history_table_operator.h"

namespace oceanbase
{
namespace restore
{

/******************ObLSRestoreStatusHelper*********************/
bool ObLSRestoreStatusHelper::is_valid(const ObLSRestoreStatus &status)
{
  return status >= ObLSRestoreStatus::INIT && status < ObLSRestoreStatus::MAX_STATUS;
}

int ObLSRestoreStatusHelper::get_next_change_status(
    const ObLSRestoreStatus &curr_status,
    const int32_t result,
    ObLSRestoreStatus &next_status)
{
  int ret = OB_SUCCESS;
  if (!is_valid(curr_status)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid restore status", KR(ret), K(curr_status));
  } else {
    switch (curr_status) {
      case ObLSRestoreStatus::INIT: {
        next_status = (OB_SUCCESS == result) ? ObLSRestoreStatus::BUILD : ObLSRestoreStatus::COMPLETE;
        break;
      }
      case ObLSRestoreStatus::BUILD: {
        next_status = ObLSRestoreStatus::WAIT_BUILD;
        break;
      }
      case ObLSRestoreStatus::WAIT_BUILD: {
        next_status = ObLSRestoreStatus::COMPLETE;
        break;
      }
      case ObLSRestoreStatus::COMPLETE: {
        next_status = ObLSRestoreStatus::WAIT_COMPLETE;
        break;
      }
      case ObLSRestoreStatus::WAIT_COMPLETE: {
        next_status = ObLSRestoreStatus::FINISH;
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected restore status", KR(ret), K(curr_status));
        break;
      }
    }
  }
  return ret;
}

const char *ObLSRestoreStatusHelper::get_status_str(const ObLSRestoreStatus &status)
{
  static const char *status_strs[] = {
    "INIT",
    "BUILD",
    "WAIT_BUILD",
    "COMPLETE",
    "WAIT_COMPLETE",
    "FINISH"
  };
  STATIC_ASSERT(ARRAYSIZEOF(status_strs) == (int64_t)ObLSRestoreStatus::MAX_STATUS,
                "ls_restore_status string array size mismatch enum ObLSRestoreStatus count");

  const char *str = "UNKNOWN";

  if (status < ObLSRestoreStatus::INIT || status >= ObLSRestoreStatus::MAX_STATUS) {
    str = "UNKNOWN";
  } else {
    str = status_strs[static_cast<int>(status)];
  }
  return str;
}

/******************ObRestoreHandler*********************/
ObRestoreHandler::ObRestoreHandler()
  : is_inited_(false),
    ls_(nullptr),
    bandwidth_throttle_(nullptr),
    start_ts_(0),
    finish_ts_(0),
    task_list_(),
    lock_(),
    status_(ObLSRestoreStatus::INIT),
    result_(OB_SUCCESS),
    is_stop_(false),
    is_cancel_(false),
    is_complete_(false),
    is_dag_net_cleared_(true),
    timer_task_(*this),
    is_timer_scheduled_(false)
{
}

ObRestoreHandler::~ObRestoreHandler()
{
}

int ObRestoreHandler::init(ObLS *ls, common::ObInOutBandwidthThrottle *bandwidth_throttle)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("restore handler init twice", KR(ret));
  } else if (OB_ISNULL(ls) || OB_ISNULL(bandwidth_throttle)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("restore handler init get invalid argument", KR(ret), KP(ls), KP(bandwidth_throttle));
  } else {
    ls_ = ls;
    start_ts_ = ObTimeUtility::current_time();
    status_ = ObLSRestoreStatus::INIT;
    result_ = OB_SUCCESS;
    is_stop_ = false;
    is_cancel_ = false;
    is_complete_ = false;
    is_dag_net_cleared_ = true;
    is_timer_scheduled_ = false;
    is_inited_ = true;
    bandwidth_throttle_ = bandwidth_throttle;
  }
  return ret;
}

int ObRestoreHandler::get_status_(ObLSRestoreStatus &status)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    status = status_;
  }
  return ret;
}

int ObRestoreHandler::check_task_list_empty_(bool &is_empty)
{
  int ret = OB_SUCCESS;
  is_empty = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    const int64_t count = task_list_.count();
    if (count > 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("restore handler has multiple tasks", KR(ret), K(count));
    } else {
      is_empty = 0 == count;
    }
  }
  return ret;
}

int ObRestoreHandler::set_result(const int32_t result)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_SUCCESS == result_ && OB_SUCCESS != result) {
      FLOG_INFO("set first error result", K(result));
      result_ = result;
    }
  }
  return ret;
}

int ObRestoreHandler::get_result_(int32_t &result)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    result = result_;
  }
  return ret;
}

bool ObRestoreHandler::is_restore_failed_() const
{
  common::SpinRLockGuard guard(lock_);
  return OB_SUCCESS != result_;
}

void ObRestoreHandler::reuse_()
{
  common::SpinWLockGuard guard(lock_);
  start_ts_ = 0;
  finish_ts_ = 0;
  task_list_.reset();
  status_ = ObLSRestoreStatus::INIT;
  result_ = OB_SUCCESS;
  is_cancel_ = false;
  is_complete_ = false;
  is_dag_net_cleared_ = true;
  is_timer_scheduled_ = false;
}

void ObRestoreHandler::ObRestoreHandlerTimerTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(handler_.process())) {
    LOG_WARN("restore handler timer tick process failed", KR(ret));
  }
}

int ObRestoreHandler::start_schedule_()
{
  int ret = OB_SUCCESS;
  bool need_schedule = false;
  {
    common::SpinWLockGuard guard(lock_);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("restore handler not inited, cannot start schedule", KR(ret));
    } else if (is_stop_) {
      ret = OB_IN_STOP_STATE;
      LOG_WARN("restore handler is stopped, cannot start schedule", KR(ret));
    } else if (task_list_.empty() || is_timer_scheduled_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("restore handler has none task or is already scheduled", KR(ret), K_(task_list), K_(is_timer_scheduled));
    } else {
      need_schedule = true;
      is_timer_scheduled_ = true;
    }
  }
  if (OB_SUCC(ret) && need_schedule) {
    const bool repeat = true;
    omt::ObSharedTimer *shared_timer = MTL(omt::ObSharedTimer*);
    if (OB_ISNULL(shared_timer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("shared timer should not be NULL", KR(ret), KPC(ls_));
    } else if (OB_FAIL(TG_SCHEDULE(shared_timer->get_tg_id(), timer_task_,
        ObRestoreHandler::RESTORE_HANDLER_SCHEDULE_INTERVAL_US, repeat))) {
      LOG_WARN("failed to schedule restore handler timer task", KR(ret), KPC(ls_));
    } else {
      LOG_INFO("start restore handler schedule on shared timer", KPC(ls_), "interval_us",
            ObRestoreHandler::RESTORE_HANDLER_SCHEDULE_INTERVAL_US);
    }
    if (OB_FAIL(ret)) {
      common::SpinWLockGuard guard(lock_);
      is_timer_scheduled_ = false;
    }
  }
  return ret;
}

void ObRestoreHandler::stop_schedule_(bool need_wait)
{
  int ret = OB_SUCCESS;
  bool need_stop = false;
  {
    common::SpinWLockGuard guard(lock_);
    if (is_timer_scheduled_) {
      is_timer_scheduled_ = false;
      need_stop = true;
    }
  }

  if (need_stop) {
    omt::ObSharedTimer *shared_timer = MTL(omt::ObSharedTimer*);
    if (OB_ISNULL(shared_timer)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("shared timer should not be NULL when stopping schedule", KR(ret));
    } else {
      TG_CANCEL_TASK(shared_timer->get_tg_id(), timer_task_);
      if (need_wait) {
        TG_WAIT_TASK(shared_timer->get_tg_id(), timer_task_);
      }
      LOG_INFO("stop restore handler schedule on shared timer", K(need_wait));
    }
  }
}

int ObRestoreHandler::get_restore_task_(ObRestoreTask &task)
{
  int ret = OB_SUCCESS;
  task.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (OB_FAIL(get_restore_task_with_nolock_(task))) {
      LOG_WARN("failed to get restore task", KR(ret));
    }
  }
  return ret;
}

int ObRestoreHandler::check_task_exist_(bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObRestoreTask task;
  ObTenantDagScheduler *scheduler = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_restore_task_(task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", KR(ret));
  } else if (OB_FAIL(scheduler->check_dag_net_exist(task.task_id_, is_exist))) {
    LOG_WARN("failed to check dag net exist", KR(ret));
  }

  return ret;
}

int ObRestoreHandler::handle_current_task_(
    bool &need_wait,
    int32_t &task_result)
{
  int ret = OB_SUCCESS;
  need_wait = false;
  task_result = OB_SUCCESS;
  bool is_exist = false;
  bool is_restore_failed = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(check_task_exist_(is_exist))) {
    LOG_WARN("failed to check task exist", KR(ret));
  } else if (OB_FAIL(get_result_(task_result))) {
    LOG_WARN("failed to get result", KR(ret));
  } else if (FALSE_IT(is_restore_failed = (OB_SUCCESS != task_result))) {
    // the order between erasing dag net from map and clearing dag net is not guaranteed (when start_running failed)
    // therefore, only when dag net is not exist and dag net is cleared, state machine can be switched to next stage
  } else if (is_exist || !is_dag_net_cleared()) {
    need_wait = true;
    if (is_restore_failed) {
      // if dag net is exist and restore is failed, cancel the restore task
      // restore task won't be cleared immediately, so we still need to wait
      if (OB_FAIL(cancel_current_task_())) {
        LOG_WARN("failed to cancel current task", KR(ret), KPC(ls_));
      }
    }
  } else {
    // if dag net is not exist, no need to wait
    need_wait = false;
  }
  return ret;
}

int ObRestoreHandler::cancel_current_task_()
{
  int ret = OB_SUCCESS;
  ObTenantDagScheduler *scheduler = nullptr;
  ObRestoreTask restore_task;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get ObTenantDagScheduler from MTL", KR(ret), KPC(ls_));
  } else if (OB_FAIL(get_restore_task_(restore_task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else if (!restore_task.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore task should not be invalid", KR(ret), K(restore_task));
  } else if (OB_FAIL(scheduler->cancel_dag_net(restore_task.task_id_))) {
    LOG_WARN("failed to cancel dag net", KR(ret), K(restore_task));
  } else {
    common::SpinWLockGuard guard(lock_);
    is_cancel_ = true;
  }

  return ret;
}

int ObRestoreHandler::add_ls_restore_task(const ObRestoreTask &task)
{
  int ret = OB_SUCCESS;
  {
    common::SpinWLockGuard guard(lock_);
    if (IS_NOT_INIT) {
      ret = OB_NOT_INIT;
      LOG_WARN("restore handler not inited", KR(ret));
    } else if (!task.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid restore task", KR(ret), K(task));
    } else if (!task_list_.empty()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("add ls restore task failed, task list is not empty", KR(ret), K(task_list_), K(task));
    } else if (is_stop_) {
      ret = OB_IN_STOP_STATE;
      LOG_WARN("restore handler is stopped, cannot add task", KR(ret), K(task));
    } else if (OB_FAIL(task_list_.push_back(task))) {
      LOG_WARN("failed to push restore task", KR(ret), K(task));
    } else if (OB_FAIL(ls_->set_restore_status(ObRestoreStatus(ObRestoreStatus::Status::RESTORE_DOING)))) {
      LOG_WARN("failed to set restore status", KR(ret), K(task));
    } else {
      LOG_INFO("add ls restore task", K(task), KPC(ls_));
    }
  }
  if (FAILEDx(start_schedule_())) {
    LOG_WARN("failed to start schedule", KR(ret), K(task));
  }
  return ret;
}

int ObRestoreHandler::switch_next_stage(const int32_t result)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(switch_next_stage_with_nolock_(result))) {
      LOG_WARN("failed to switch next stage", KR(ret), K(result));
    }
  }
  return ret;
}

int ObRestoreHandler::check_task_exist(const share::ObTaskId &task_id, bool &is_exist)
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObRestoreTask task;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("check task exist get invalid argument", KR(ret), K(task_id));
  } else if (OB_FAIL(get_restore_task_(task))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      is_exist = false;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get restore task", KR(ret), KPC(ls_));
    }
  } else if (task_id == task.task_id_) {
    is_exist = true;
  } else {
    is_exist = false;
  }
  return ret;
}

int ObRestoreHandler::get_restore_task_and_status(
    ObRestoreTask &task,
    ObLSRestoreStatus &status)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    common::SpinRLockGuard guard(lock_);
    if (task_list_.empty()) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      task = task_list_.at(0);
      status = status_;
    }
  }
  return ret;
}

// TODO(xingzhi): Ensure that the modules holding ObRestoreHandler call destroy() correctly when exiting
void ObRestoreHandler::destroy()
{
  stop_schedule_(true/*need_wait*/);
  ls_ = nullptr;
  bandwidth_throttle_ = nullptr;
  start_ts_ = 0;
  finish_ts_ = 0;
  task_list_.reset();
  is_inited_ = false;
}

int ObRestoreHandler::process()
{
  int ret = OB_SUCCESS;
  ObLSRestoreStatus status;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_status_(status))) {
    LOG_WARN("failed to get restore handler status", KR(ret));
  } else {
    switch (status) {
    case ObLSRestoreStatus::INIT: {
      if (OB_FAIL(do_init_status_())) {
        LOG_WARN("failed to do init status", KR(ret), K(status));
      }
      break;
    }
    case ObLSRestoreStatus::BUILD: {
      if (OB_FAIL(do_build_status_())) {
        LOG_WARN("failed to do build status", KR(ret), K(status));
      }
      break;
    }
    case ObLSRestoreStatus::COMPLETE: {
      if (OB_FAIL(do_complete_status_())) {
        LOG_WARN("failed to do complete status", KR(ret), K(status));
      }
      break;
    }
    case ObLSRestoreStatus::FINISH: {
      if (OB_FAIL(do_finish_status_())) {
        LOG_WARN("failed to do finish status", KR(ret), K(status));
      }
      break;
    }
    case ObLSRestoreStatus::WAIT_BUILD:
    case ObLSRestoreStatus::WAIT_COMPLETE: {
      if (OB_FAIL(do_wait_status_())) {
        LOG_WARN("failed to do wait status", KR(ret), K(status));
      }
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("invalid cur status for fail", KR(ret), K(status));
    }
    }
  }
  return ret;
}

bool ObRestoreHandler::is_cancel() const
{
  common::SpinRLockGuard guard(lock_);
  return is_cancel_;
}

bool ObRestoreHandler::is_complete() const
{
  common::SpinRLockGuard guard(lock_);
  return is_complete_;
}

bool ObRestoreHandler::is_dag_net_cleared() const
{
  common::SpinRLockGuard guard(lock_);
  return is_dag_net_cleared_;
}

void ObRestoreHandler::set_dag_net_cleared()
{
  common::SpinWLockGuard guard(lock_);
  is_dag_net_cleared_ = true;
}

int ObRestoreHandler::do_init_status_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const ObLSRestoreStatus status = ObLSRestoreStatus::INIT;
  bool is_empty = false;
  bool need_to_abort = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else {
    start_ts_ = ObTimeUtil::current_time();
    if (ls_->get_persistent_state().is_need_gc()) {
      // ls persistent state is need gc state, which means ls hasn't been completely created / failed to create
      // do nothing
      FLOG_INFO("ls persistent state is need gc state", KPC(ls_));
    } else if (OB_FAIL(check_task_list_empty_(is_empty))) {
      LOG_WARN("failed to check task list empty", KR(ret), KPC(ls_));
    } else if (is_empty) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("task list is empty", KR(ret), KPC(ls_));
      stop_schedule_(false/*need_wait*/);
    } else {
      if (OB_FAIL(check_need_to_abort_(need_to_abort))) {
        LOG_WARN("failed to check need to abort", KR(ret));
      } else if (!need_to_abort) {
        ObRestoreTask task;
        if (OB_FAIL(get_restore_task_(task))) {
            LOG_WARN("failed to get restore task", KR(ret));
        } else {
          SERVER_EVENT_ADD("storage_ha", "ha_restore_start",
              "tenant_id", ls_->get_tenant_id(),
              "ls_id", ls_->get_ls_id().id(),
              "src", task.src_info_,
              "task_id", task.task_id_,
              "is_failed", OB_SUCCESS,
              "task_type", ObRestoreTaskType::get_str(task.type_));
        }
      }
      if (OB_TMP_FAIL(switch_next_stage(ret))) {
        LOG_ERROR("failed to switch next stage", KR(tmp_ret), KR(ret), K(status));
      }
    }
  }
  return ret;
}

int ObRestoreHandler::do_build_status_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  bool need_to_abort = false;
  ObRestoreTask task;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(check_need_to_abort_(need_to_abort))) {
    LOG_WARN("failed to check need to abort", KR(ret));
  } else if (!need_to_abort) {
    if (OB_FAIL(get_restore_task_(task))) {
      LOG_WARN("failed to get restore task", KR(ret));
    } else if (OB_FAIL(generate_build_dag_net_())) {
      LOG_WARN("failed to generate build dag net", KR(ret), KPC(ls_));
    }
  }

  // BUILD -> WAIT_BUILD
  if (OB_TMP_FAIL(switch_next_stage(ret))) {
    LOG_ERROR("failed to switch next stage", KR(tmp_ret), KR(ret), KPC(ls_));
  }
  return ret;
}

int ObRestoreHandler::do_complete_status_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(generate_complete_dag_net_())) {
    LOG_WARN("failed to generate complete dag net", KR(ret), KPC(ls_));
  }

  if (is_complete()) {
    // COMPLETE -> WAIT_COMPLETE
    if (OB_TMP_FAIL(switch_next_stage(ret))) {
      LOG_ERROR("failed to switch next stage", KR(tmp_ret), KR(ret), KPC(ls_));
    }
  }
  return ret;
}

int ObRestoreHandler::do_finish_status_()
{
  int ret = OB_SUCCESS;
  int32_t result = OB_SUCCESS;
  ObRestoreTask task;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_restore_task_(task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else if (OB_FAIL(get_result_(result))) {
    LOG_WARN("failed to get result", KR(ret));
  } else if (OB_FAIL(report_result_())) {
    LOG_WARN("failed to report result", KR(ret), KPC(ls_));
  } else {
    SERVER_EVENT_ADD("storage_ha", "ls_ha_finish",
        "tenant_id", ls_->get_tenant_id(),
        "src", task.src_info_,
        "task_id", task.task_id_,
        "is_failed", result,
        "task_type", ObRestoreTaskType::get_str(task.type_));
    finish_ts_ = ObTimeUtility::current_time();
    FLOG_INFO("do finish restore task", K(task), K(result), "cost_ts", finish_ts_ - start_ts_);
    reuse_();
    stop_schedule_(false/*need_wait*/);
  }
  return ret;
}

int ObRestoreHandler::do_wait_status_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObRestoreTask restore_task;
  bool need_wait = false;
  int32_t task_result = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_restore_task_(restore_task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else if (!restore_task.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore task is not valid", KR(ret), K(restore_task), KPC(ls_));
  } else if (OB_FAIL(handle_current_task_(need_wait, task_result))) {
    LOG_WARN("failed to handle current task", KR(ret), KPC(ls_));
  } else if (need_wait) {
    // do nothing, won't switch status
  } else if (OB_FAIL(switch_next_stage(task_result))) {
    // WAIT_BUILD -> COMPLETE
    // WAIT_COMPLETE -> FINISH
    // if task_result!=OB_SUCCESS, switch to COMPLETE (except for WAIT_COMPLETE)
    LOG_WARN("failed to switch next stage", KR(ret), K(task_result), KPC(ls_));
  }

  if (OB_FAIL(ret)) {
    if (OB_TMP_FAIL(switch_next_stage(ret))) { // WAIT -> COMPLETE (except for WAIT_COMPLETE)
      LOG_ERROR("failed to report result at wait status", KR(tmp_ret), KR(ret), K(status_));
    }
  }

  return ret;
}

int ObRestoreHandler::generate_build_dag_net_()
{
  int ret = OB_SUCCESS;
  ObRestoreTask restore_task;
  ObInOutBandwidthThrottle *bandwidth_throttle = GCTX.bandwidth_throttle_;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_restore_task_(restore_task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else if (!restore_task.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore task is not valid", KR(ret), K(restore_task));
  } else {
    common::SpinWLockGuard guard(lock_);
    ObRestoreDagNetInitParam param;
    param.task_ = restore_task;
    param.handler_ = this;
    param.bandwidth_throttle_ = bandwidth_throttle_;
    if (!param.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid build dag net param", KR(ret), K(param));
    } else if (OB_FAIL(schedule_dag_net_<ObRestoreDagNet>(&param, true /* check_cancel */))) {
      LOG_WARN("failed to schedule build dag net", KR(ret), K(restore_task));
    }
  }
  return ret;
}

int ObRestoreHandler::generate_complete_dag_net_()
{
  int ret = OB_SUCCESS;
  ObRestoreTask restore_task;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_restore_task_(restore_task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else if (!restore_task.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore task is not valid", KR(ret), K(restore_task));
  } else {
    common::SpinWLockGuard guard(lock_);
    ObCompleteRestoreParam param;
    param.task_ = restore_task;
    param.handler_ = this;
    param.result_ = result_;
    if (!param.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid complete dag net param", KR(ret), K(param));
    } else if (OB_FAIL(schedule_dag_net_<ObCompleteRestoreDagNet>(&param, false /* check_cancel */))) {
      LOG_WARN("failed to schedule complete dag net", KR(ret), K(restore_task));
    } else {
      is_complete_ = true;
    }
  }
  return ret;
}

void ObRestoreHandler::stop()
{
  int ret = OB_SUCCESS;
  ObTenantDagScheduler *scheduler = nullptr;
  {
    common::SpinWLockGuard guard(lock_);
    is_stop_ = true;
    result_ = OB_SUCCESS != result_ ? result_ : OB_IN_STOP_STATE;
    if (task_list_.empty()) {
    } else if (task_list_.count() > 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("restore task count is unexpected", KR(ret), K(task_list_));
    } else {
      ObRestoreTask &task = task_list_.at(0);
      if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler*))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("failed to get ObTenantDagScheduler from MTL", KR(ret), KPC(ls_));
      } else if (OB_FAIL(scheduler->cancel_dag_net(task.task_id_))) {
        LOG_ERROR("failed to cancel dag net", KR(ret), K(task), KPC(ls_));
      }
    }
  }
  stop_schedule_(true/*need_wait*/);
}

void ObRestoreHandler::wait(bool &wait_finished)
{
  int ret = OB_SUCCESS;
  wait_finished = false;
  ObRestoreTask task;
  share::ObTenantBase *tenant_base = MTL_CTX();
  omt::ObTenant *tenant = nullptr;

  if (IS_NOT_INIT) {
    wait_finished = true;
  } else if (OB_ISNULL(tenant_base)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant base should not be NULL", KR(ret), KP(tenant_base));
  } else if (FALSE_IT(tenant = static_cast<omt::ObTenant *>(tenant_base))) {
  } else if (tenant->has_stopped()) {
    LOG_INFO("tenant has stop, no need wait restore handler task finish");
    wait_finished = true;
  } else if (OB_FAIL(get_restore_task_(task))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      wait_finished = true;
    } else {
      LOG_WARN("failed to get restore task", KR(ret), KPC(ls_));
    }
  } else {
    wait_finished = false;
  }
}

int ObRestoreHandler::get_restore_task_with_nolock_(ObRestoreTask &task) const
{
  int ret = OB_SUCCESS;
  task.reset();
  if (task_list_.empty()) {
    ret = OB_ENTRY_NOT_EXIST;
    LOG_WARN("restore task is empty", KR(ret), KPC(ls_));
  } else if (task_list_.count() > 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("restore task count should not more than 1", KR(ret), K(task_list_), KPC(ls_));
  } else {
    task = task_list_.at(0);
  }
  return ret;
}

int ObRestoreHandler::check_task_exist_with_nolock_(const share::ObTaskId &task_id, bool &is_exist) const
{
  int ret = OB_SUCCESS;
  is_exist = false;
  ObRestoreTask task;
  if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(task_id));
  } else if (OB_FAIL(get_restore_task_with_nolock_(task))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      is_exist = false;
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get restore task", KR(ret), KPC(ls_));
    }
  } else if (task_id == task.task_id_) {
    is_exist = true;
  } else {
    is_exist = false;
  }
  return ret;
}

int ObRestoreHandler::switch_next_stage_with_nolock_(const int32_t result)
{
  int ret = OB_SUCCESS;
  ObLSRestoreStatus next_status = ObLSRestoreStatus::MAX_STATUS;
  int32_t new_result = OB_SUCCESS;
  new_result = OB_SUCCESS != result_ ? result_ : result;
  if (OB_FAIL(ObLSRestoreStatusHelper::get_next_change_status(status_, new_result, next_status))) {
    LOG_WARN("failed to get next change status", KR(ret), K(status_), K(result), K(new_result));
  } else {
    FLOG_INFO("restore handler change status", K(result), K(new_result), K(result_), K(status_), K(next_status));
    result_ = new_result;
    status_ = next_status;
  }
  return ret;
}

int ObRestoreHandler::check_need_to_abort_(bool &need_to_abort)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  need_to_abort = false;

  if (is_restore_failed_()) {
    int32_t result = OB_SUCCESS;
    if (OB_FAIL(get_result_(result))) {
      LOG_WARN("failed to get result", KR(ret));
    } else {
      tmp_ret = result;
      LOG_INFO("restore is already failed, skip current status", K(result), K(status_), KPC(ls_));
    }
  } else if (is_cancel()) {
    tmp_ret = OB_CANCELED;
    LOG_INFO("restore is cancelled, skip current status", K(tmp_ret), K(status_), KPC(ls_));
  }

  if (OB_TMP_FAIL(tmp_ret)) {
    if (OB_FAIL(set_result(tmp_ret))) {
      LOG_WARN("failed to set result", KR(ret), KR(tmp_ret), K(status_), KPC(ls_));
    } else {
      need_to_abort = true;
    }
  }

  return ret;
}

int ObRestoreHandler::report_result_()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObRestoreTask task;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_restore_task_(task))) {
    LOG_WARN("failed to get restore task", KR(ret));
  } else {
    if (OB_TMP_FAIL(report_meta_table_())) {
      LOG_WARN("failed to report meta table", KR(ret), KR(tmp_ret), K(task));
    }
    // TODO(xingzhi): exit if failed and trigger ObStandbySchemaRefreshTrigger if success
  }
  return ret;
}

int ObRestoreHandler::report_meta_table_()
{
  int ret = OB_SUCCESS;
  int32_t result = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("restore handler do not init", KR(ret));
  } else if (OB_FAIL(get_result_(result))) {
    LOG_WARN("failed to get result", KR(ret));
  } else if (OB_SUCCESS != result) {
    // do nothing for failed restore
  // TODO(xingzhi): check if report_replica_info valid
  } else if (OB_FAIL(ls_->report_replica_info())) {
    LOG_WARN("failed to report replica info", KR(ret), KPC(ls_));
  }
  return ret;
}

} // namespace restore
} // namespace oceanbase
