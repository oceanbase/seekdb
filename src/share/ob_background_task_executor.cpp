/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX SHARE
#include "share/ob_background_task_executor.h"

#include "lib/ob_running_mode.h"
#include "lib/oblog/ob_log.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace share
{

namespace
{

const ObBackgroundTaskPriority PRIORITY_SCHEDULE[] = {
  BG_TASK_HIGH, BG_TASK_HIGH, BG_TASK_HIGH, BG_TASK_HIGH,
  BG_TASK_HIGH, BG_TASK_HIGH, BG_TASK_HIGH, BG_TASK_HIGH,
  BG_TASK_NORMAL, BG_TASK_NORMAL, BG_TASK_NORMAL, BG_TASK_NORMAL,
  BG_TASK_LOW
};

const int64_t PRIORITY_SCHEDULE_SIZE =
    static_cast<int64_t>(sizeof(PRIORITY_SCHEDULE) / sizeof(PRIORITY_SCHEDULE[0]));
const int64_t BACKGROUND_WORKER_IDLE_TIMEOUT_US = 30 * 1000 * 1000L;

bool is_valid_priority(const ObBackgroundTaskPriority priority)
{
  return priority >= BG_TASK_HIGH && priority < BG_TASK_PRIORITY_COUNT;
}

} // end anonymous namespace

ObBackgroundTaskSourceConfig::ObBackgroundTaskSourceConfig()
  : name_(NULL),
    max_concurrency_(1),
    max_concurrency_by_priority_()
{
  for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
    max_concurrency_by_priority_[i] = 1;
  }
}

bool ObBackgroundTaskSourceConfig::is_valid(const int64_t max_worker_count) const
{
  bool valid = NULL != name_
      && max_concurrency_ > 0
      && max_concurrency_ <= max_worker_count;
  for (int64_t i = 0; valid && i < BG_TASK_PRIORITY_COUNT; ++i) {
    valid = max_concurrency_by_priority_[i] > 0
        && max_concurrency_by_priority_[i] <= max_concurrency_;
  }
  return valid;
}

ObBackgroundTaskExecutor::SourceSlot::SourceSlot()
  : source_(NULL),
    state_(SOURCE_UNREGISTERED),
    config_(),
    lanes_(),
    running_count_(0),
    generation_(1)
{
}

void ObBackgroundTaskExecutor::SourceSlot::reset_for_register(
    ObIBackgroundTaskSource &source,
    const ObBackgroundTaskSourceConfig &config)
{
  source_ = &source;
  state_ = SOURCE_REGISTERED;
  config_ = config;
  running_count_ = 0;
  for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
    lanes_[i].reset();
  }
}

void ObBackgroundTaskExecutor::SourceSlot::reset_after_unregister()
{
  source_ = NULL;
  state_ = SOURCE_UNREGISTERED;
  config_ = ObBackgroundTaskSourceConfig();
  running_count_ = 0;
  for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
    lanes_[i].reset();
  }
  ++generation_;
  if (0 == generation_) {
    ++generation_;
  }
}

ObBackgroundTaskExecutor::DispatchCandidate::DispatchCandidate()
  : source_(NULL),
    slot_id_(-1),
    generation_(0),
    priority_(BG_TASK_NORMAL),
    notify_epoch_(0)
{
}

ObBackgroundTaskExecutor::ObBackgroundTaskExecutor()
  : common::ObSimpleThreadPool(),
    lock_(),
    is_inited_(false),
    stopping_(false),
    max_worker_count_(0),
    source_slots_(),
    dispatch_tokens_(),
    ready_sources_(),
    source_rr_cursor_(),
    priority_schedule_cursor_(0),
    delayed_notify_timer_(),
    delayed_notify_tasks_()
{
  for (int64_t i = 0; i < MAX_WORKER_COUNT; ++i) {
    dispatch_tokens_[i].owner_ = this;
    dispatch_tokens_[i].token_id_ = i;
  }
  for (int64_t slot_id = 0; slot_id < MAX_SOURCE_COUNT; ++slot_id) {
    for (int64_t priority = 0;
        priority < BG_TASK_PRIORITY_COUNT; ++priority) {
      delayed_notify_tasks_[slot_id][priority].init(
          this,
          slot_id,
          static_cast<ObBackgroundTaskPriority>(priority));
    }
  }
}

ObBackgroundTaskExecutor::~ObBackgroundTaskExecutor()
{
  destroy();
}

int ObBackgroundTaskExecutor::init()
{
  int ret = OB_SUCCESS;
  const int64_t max_worker_count =
      lib::is_mini_mode()
          ? MINI_MODE_MAX_WORKER_COUNT
          : MAX_WORKER_COUNT;
  const int64_t min_worker_count =
      lib::is_mini_mode()
          ? MINI_MODE_WARM_WORKER_COUNT
          : 1;
  if (OB_FAIL(common::ObSimpleThreadPool::set_idle_shrink_timeout(
      BACKGROUND_WORKER_IDLE_TIMEOUT_US))) {
    LOG_WARN("failed to configure background worker idle timeout", K(ret));
  } else {
    ret = init(max_worker_count, min_worker_count);
  }
  return ret;
}

int ObBackgroundTaskExecutor::init(
    const int64_t max_worker_count,
    const int64_t min_worker_count)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (max_worker_count <= 0
      || max_worker_count > MAX_WORKER_COUNT
      || min_worker_count <= 0
      || min_worker_count > max_worker_count) {
    ret = OB_INVALID_ARGUMENT;
  } else if (FALSE_IT(common::ObSimpleThreadPool::set_run_wrapper(
      share::server_runtime()))) {
  } else if (FALSE_IT(
      common::ObSimpleThreadPool::set_queue_driven_expansion(true))) {
  } else if (OB_FAIL(common::ObSimpleThreadPool::set_adaptive_thread(
      min_worker_count, max_worker_count))) {
    LOG_WARN("failed to configure background worker pool",
        K(ret), K(min_worker_count), K(max_worker_count));
  } else if (OB_FAIL(common::ObSimpleThreadPool::init(
      max_worker_count, MAX_WORKER_COUNT, "BGTask"))) {
    LOG_WARN("failed to initialize background worker pool", K(ret), K(max_worker_count));
  } else if (OB_FAIL(delayed_notify_timer_.set_run_wrapper_with_ret(
      share::server_runtime()))) {
    LOG_WARN("failed to set delayed notification run wrapper", K(ret));
  } else if (OB_FAIL(delayed_notify_timer_.init(
      "BGTaskDelay", common::ObMemAttr("BGTaskDelay")))) {
    LOG_WARN("failed to initialize delayed notification timer", K(ret));
  } else {
    lib::ObMutexGuard guard(lock_);
    is_inited_ = true;
    stopping_ = false;
    max_worker_count_ = max_worker_count;
    priority_schedule_cursor_ = 0;
    for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
      ready_sources_[i] = 0;
      source_rr_cursor_[i] = 0;
    }
    for (int64_t i = 0; i < MAX_WORKER_COUNT; ++i) {
      dispatch_tokens_[i].state_ = TOKEN_IDLE;
    }
  }
  if (OB_FAIL(ret)) {
    delayed_notify_timer_.destroy();
    common::ObSimpleThreadPool::stop();
    common::ObSimpleThreadPool::wait();
    common::ObSimpleThreadPool::destroy();
    common::ObSimpleThreadPool::reset_worker_counts();
  }
  return ret;
}

void ObBackgroundTaskExecutor::stop()
{
  bool need_stop = false;
  {
    lib::ObMutexGuard guard(lock_);
    if (is_inited_ && !stopping_) {
      stopping_ = true;
      for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
        ready_sources_[i] = 0;
      }
      need_stop = true;
    }
  }
  if (need_stop) {
    common::ObSimpleThreadPool::stop();
  }
}

void ObBackgroundTaskExecutor::wait()
{
  if (is_inited_) {
    common::ObSimpleThreadPool::wait();
  }
}

void ObBackgroundTaskExecutor::destroy()
{
  if (is_inited_) {
    stop();
    delayed_notify_timer_.destroy();
    common::ObSimpleThreadPool::wait();
    common::ObSimpleThreadPool::destroy();
    common::ObSimpleThreadPool::reset_worker_counts();
    lib::ObMutexGuard guard(lock_);
    for (int64_t i = 0; i < MAX_SOURCE_COUNT; ++i) {
      if (NULL != source_slots_[i].source_) {
        source_slots_[i].reset_after_unregister();
      }
    }
    for (int64_t i = 0; i < MAX_WORKER_COUNT; ++i) {
      dispatch_tokens_[i].state_ = TOKEN_IDLE;
    }
    for (int64_t i = 0; i < BG_TASK_PRIORITY_COUNT; ++i) {
      ready_sources_[i] = 0;
      source_rr_cursor_[i] = 0;
    }
    max_worker_count_ = 0;
    stopping_ = false;
    is_inited_ = false;
  }
}

int ObBackgroundTaskExecutor::register_source(
    ObIBackgroundTaskSource &source,
    const ObBackgroundTaskSourceConfig &config,
    ObBackgroundTaskSourceHandle &handle)
{
  int ret = OB_SUCCESS;
  int64_t free_slot_id = -1;
  lib::ObMutexGuard guard(lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (stopping_) {
    ret = OB_IN_STOP_STATE;
  } else if (!config.is_valid(max_worker_count_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < MAX_SOURCE_COUNT; ++i) {
      if (source_slots_[i].source_ == &source) {
        ret = OB_ENTRY_EXIST;
      } else if (-1 == free_slot_id && NULL == source_slots_[i].source_) {
        free_slot_id = i;
      }
    }
    if (OB_SUCC(ret) && free_slot_id < 0) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_SUCC(ret)) {
      SourceSlot &slot = source_slots_[free_slot_id];
      clear_ready_bits_locked(free_slot_id);
      slot.reset_for_register(source, config);
      handle.slot_id_ = free_slot_id;
      handle.generation_ = slot.generation_;
    }
  }
  return ret;
}

int ObBackgroundTaskExecutor::unregister_source(
    ObBackgroundTaskSourceHandle &handle)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!is_valid_handle_locked(handle)) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    SourceSlot &slot = source_slots_[handle.slot_id_];
    if (SOURCE_REGISTERED == slot.state_) {
      slot.state_ = SOURCE_STOPPING;
      clear_ready_bits_locked(handle.slot_id_);
    }
    if (slot.running_count_ > 0) {
      ret = OB_EAGAIN;
    } else {
      clear_ready_bits_locked(handle.slot_id_);
      slot.reset_after_unregister();
      handle.reset();
    }
  }
  return ret;
}

int ObBackgroundTaskExecutor::notify(
    const ObBackgroundTaskSourceHandle &handle,
    const ObBackgroundTaskPriority priority)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(lock_);
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (stopping_) {
    ret = OB_IN_STOP_STATE;
  } else if (!is_valid_priority(priority)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!is_valid_handle_locked(handle)) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    SourceSlot &slot = source_slots_[handle.slot_id_];
    if (SOURCE_REGISTERED != slot.state_) {
      ret = OB_IN_STOP_STATE;
    } else {
      PriorityLaneState &lane = slot.lanes_[priority];
      ++lane.notify_epoch_;
      if (0 == lane.notify_epoch_) {
        lane.notify_epoch_ = 1;
        lane.suspended_epoch_ = 0;
      }
      lane.runnable_limit_ = MIN(
          slot.config_.max_concurrency_by_priority_[priority],
          MAX(lane.runnable_limit_, lane.running_count_ + 1));
      ready_sources_[priority] |= (1ULL << handle.slot_id_);
      if (OB_FAIL(ensure_dispatch_tokens_locked())) {
        LOG_WARN("failed to wake background worker",
            K(ret), K(handle.slot_id_), K(priority));
      }
    }
  }
  return ret;
}

int64_t ObBackgroundTaskExecutor::get_worker_count() const
{
  return common::ObSimpleThreadPool::get_thread_count();
}

int64_t ObBackgroundTaskExecutor::get_idle_worker_count() const
{
  return common::ObSimpleThreadPool::idle_count();
}

int64_t ObBackgroundTaskExecutor::get_registered_source_count() const
{
  int64_t count = 0;
  lib::ObMutexGuard guard(lock_);
  for (int64_t i = 0; i < MAX_SOURCE_COUNT; ++i) {
    if (NULL != source_slots_[i].source_) {
      ++count;
    }
  }
  return count;
}

void ObBackgroundTaskExecutor::handle(void *task)
{
  int ret = OB_SUCCESS;
  DispatchToken *token = static_cast<DispatchToken *>(task);
  if (NULL == token || token->owner_ != this) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid background dispatch token", KP(token));
  } else {
    {
      lib::ObMutexGuard guard(lock_);
      if (TOKEN_QUEUED != token->state_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected background dispatch token state",
            K(token->token_id_), K(token->state_));
        return;
      }
      token->state_ = TOKEN_RUNNING;
    }

    bool keep_running = true;
    while (keep_running) {
      DispatchCandidate candidate;
      {
        lib::ObMutexGuard guard(lock_);
        if (!is_inited_ || stopping_ || !pick_candidate_locked(candidate)) {
          token->state_ = TOKEN_IDLE;
          keep_running = false;
        }
      }

      if (keep_running) {
        int tmp_ret = OB_SUCCESS;
        ObBackgroundTaskRunResult result;
        if (OB_ISNULL(candidate.source_)) {
          tmp_ret = OB_ERR_UNEXPECTED;
          ret = tmp_ret;
          LOG_ERROR("background source is null", K(tmp_ret), K(candidate.slot_id_));
        } else if (OB_SUCCESS != (tmp_ret =
            candidate.source_->process_one_quantum(candidate.priority_, result))) {
          ret = tmp_ret;
          LOG_WARN("background source quantum failed",
              K(tmp_ret), K(candidate.slot_id_), K(candidate.priority_));
        }

        {
          lib::ObMutexGuard guard(lock_);
          finish_candidate_locked(candidate, result, tmp_ret);
          int wake_ret = OB_SUCCESS;
          if (!stopping_
              && OB_SUCCESS != (wake_ret = ensure_dispatch_tokens_locked())) {
            ret = wake_ret;
            LOG_WARN("failed to expand background workers", K(wake_ret));
          }
        }

        if (OB_SUCCESS == tmp_ret
            && !result.has_more_ready_
            && result.next_ready_ts_ > 0) {
          const int delay_ret =
              schedule_delayed_notify(candidate, result.next_ready_ts_);
          if (OB_SUCCESS != delay_ret
              && OB_ENTRY_NOT_EXIST != delay_ret
              && OB_IN_STOP_STATE != delay_ret) {
            LOG_WARN_RET(delay_ret,
                "failed to schedule delayed background notification",
                K(candidate.slot_id_), K(candidate.priority_),
                K(result.next_ready_ts_));
          }
        }
      }
    }

  }
}

void ObBackgroundTaskExecutor::handle_drop(void *task)
{
  DispatchToken *token = static_cast<DispatchToken *>(task);
  if (NULL != token && token->owner_ == this) {
    lib::ObMutexGuard guard(lock_);
    token->state_ = TOKEN_IDLE;
  }
}

void ObBackgroundTaskExecutor::DelayedNotifyTask::runTimerTask()
{
  if (NULL != owner_) {
    owner_->on_delayed_notify(slot_id_, priority_);
  }
}

bool ObBackgroundTaskExecutor::is_valid_handle_locked(
    const ObBackgroundTaskSourceHandle &handle) const
{
  return handle.is_valid()
      && handle.slot_id_ < MAX_SOURCE_COUNT
      && NULL != source_slots_[handle.slot_id_].source_
      && SOURCE_UNREGISTERED != source_slots_[handle.slot_id_].state_
      && handle.generation_ == source_slots_[handle.slot_id_].generation_;
}

bool ObBackgroundTaskExecutor::pick_candidate_locked(
    DispatchCandidate &candidate)
{
  bool found = false;
  for (int64_t offset = 0; !found && offset < PRIORITY_SCHEDULE_SIZE; ++offset) {
    const int64_t schedule_pos =
        (priority_schedule_cursor_ + offset) % PRIORITY_SCHEDULE_SIZE;
    const ObBackgroundTaskPriority priority = PRIORITY_SCHEDULE[schedule_pos];
    const uint64_t ready_bitmap = ready_sources_[priority];
    if (0 == ready_bitmap) {
      continue;
    }

    const int64_t start = source_rr_cursor_[priority];
    for (int64_t i = 0; !found && i < MAX_SOURCE_COUNT; ++i) {
      const int64_t slot_id = (start + i) % MAX_SOURCE_COUNT;
      const uint64_t source_bit = 1ULL << slot_id;
      SourceSlot &slot = source_slots_[slot_id];
      if (0 != (ready_bitmap & source_bit)
          && is_lane_runnable_locked(slot_id, priority)) {
        PriorityLaneState &lane = slot.lanes_[priority];
        ++slot.running_count_;
        ++lane.running_count_;
        candidate.source_ = slot.source_;
        candidate.slot_id_ = slot_id;
        candidate.generation_ = slot.generation_;
        candidate.priority_ = priority;
        candidate.notify_epoch_ = lane.notify_epoch_;
        source_rr_cursor_[priority] = (slot_id + 1) % MAX_SOURCE_COUNT;
        priority_schedule_cursor_ =
            (schedule_pos + 1) % PRIORITY_SCHEDULE_SIZE;
        found = true;
      }
    }
  }
  return found;
}

void ObBackgroundTaskExecutor::finish_candidate_locked(
    const DispatchCandidate &candidate,
    const ObBackgroundTaskRunResult &result,
    const int process_ret)
{
  int ret = OB_SUCCESS;
  if (candidate.slot_id_ < 0 || candidate.slot_id_ >= MAX_SOURCE_COUNT) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid completed source slot", K(candidate.slot_id_));
  } else {
    SourceSlot &slot = source_slots_[candidate.slot_id_];
    PriorityLaneState &lane = slot.lanes_[candidate.priority_];
    if (slot.source_ != candidate.source_
        || slot.generation_ != candidate.generation_
        || slot.running_count_ <= 0
        || lane.running_count_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("background source state changed while running",
          K(candidate.slot_id_), K(candidate.generation_),
          K(slot.generation_), K(slot.running_count_), K(lane.running_count_));
    } else {
      --slot.running_count_;
      --lane.running_count_;
      const uint64_t source_bit = 1ULL << candidate.slot_id_;
      if (SOURCE_REGISTERED != slot.state_) {
        ready_sources_[candidate.priority_] &= ~source_bit;
        lane.runnable_limit_ = 0;
        lane.suspended_epoch_ = 0;
      } else if (OB_SUCCESS != process_ret) {
        // Keep the bit as the source still owns the uncompleted task, but do
        // not hot-loop on a failing source. A later retry notification moves
        // notify_epoch_ past suspended_epoch_ and makes it runnable again.
        lane.suspended_epoch_ =
            MAX(lane.suspended_epoch_, candidate.notify_epoch_);
        ready_sources_[candidate.priority_] |= source_bit;
      } else if (result.has_more_ready_) {
        lane.runnable_limit_ = MIN(
            slot.config_.max_concurrency_by_priority_[candidate.priority_],
            lane.runnable_limit_ + 1);
        ready_sources_[candidate.priority_] |= source_bit;
      } else if (candidate.notify_epoch_ == lane.notify_epoch_) {
        ready_sources_[candidate.priority_] &= ~source_bit;
        lane.runnable_limit_ = 0;
        lane.suspended_epoch_ = 0;
      }
    }
  }
}

int ObBackgroundTaskExecutor::schedule_delayed_notify(
    const DispatchCandidate &candidate,
    const int64_t ready_ts)
{
  int ret = OB_SUCCESS;
  bool need_schedule = false;
  int64_t delay = 0;
  {
    lib::ObMutexGuard guard(lock_);
    if (!is_inited_) {
      ret = OB_NOT_INIT;
    } else if (stopping_) {
      ret = OB_IN_STOP_STATE;
    } else if (ready_ts <= 0
        || candidate.slot_id_ < 0
        || candidate.slot_id_ >= MAX_SOURCE_COUNT
        || !is_valid_priority(candidate.priority_)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      SourceSlot &slot = source_slots_[candidate.slot_id_];
      PriorityLaneState &lane = slot.lanes_[candidate.priority_];
      if (SOURCE_REGISTERED != slot.state_
          || slot.source_ != candidate.source_
          || slot.generation_ != candidate.generation_) {
        ret = OB_ENTRY_NOT_EXIST;
      } else if (0 == lane.delayed_notify_ts_
          || ready_ts < lane.delayed_notify_ts_) {
        lane.delayed_notify_ts_ = ready_ts;
        delay = MAX(
            static_cast<int64_t>(0),
            ready_ts - common::ObTimeUtility::current_time());
        need_schedule = true;
      }
    }
  }

  if (OB_SUCC(ret) && need_schedule) {
    ret = delayed_notify_timer_.schedule(
        delayed_notify_tasks_[candidate.slot_id_][candidate.priority_],
        delay,
        false /* repeat */);
    if (OB_FAIL(ret)) {
      bool need_fallback_notify = false;
      ObBackgroundTaskSourceHandle handle;
      {
        lib::ObMutexGuard guard(lock_);
        SourceSlot &slot = source_slots_[candidate.slot_id_];
        if (SOURCE_REGISTERED == slot.state_
            && slot.source_ == candidate.source_
            && slot.generation_ == candidate.generation_
            && slot.lanes_[candidate.priority_].delayed_notify_ts_
                == ready_ts) {
          slot.lanes_[candidate.priority_].delayed_notify_ts_ = 0;
          handle.slot_id_ = candidate.slot_id_;
          handle.generation_ = candidate.generation_;
          need_fallback_notify = true;
        }
      }
      // Scheduling can fail on transient timer allocation pressure. Re-publish
      // readiness so the source can report its deadline again; otherwise the
      // cleared ready bit would leave the owned task permanently stranded.
      if (need_fallback_notify) {
        const int notify_ret = notify(handle, candidate.priority_);
        if (OB_SUCCESS != notify_ret
            && OB_ENTRY_NOT_EXIST != notify_ret
            && OB_IN_STOP_STATE != notify_ret) {
          LOG_WARN_RET(notify_ret,
              "failed to publish fallback background notification",
              K(notify_ret), K(candidate.slot_id_),
              K(candidate.priority_), K(ready_ts));
        }
      }
    }
  }
  return ret;
}

void ObBackgroundTaskExecutor::on_delayed_notify(
    const int64_t slot_id,
    const ObBackgroundTaskPriority priority)
{
  int ret = OB_SUCCESS;
  int64_t reschedule_delay = 0;
  int64_t reschedule_ready_ts = 0;
  ObBackgroundTaskSourceHandle handle;
  bool need_notify = false;
  bool need_reschedule = false;
  {
    lib::ObMutexGuard guard(lock_);
    if (is_inited_
        && !stopping_
        && slot_id >= 0
        && slot_id < MAX_SOURCE_COUNT
        && is_valid_priority(priority)) {
      SourceSlot &slot = source_slots_[slot_id];
      PriorityLaneState &lane = slot.lanes_[priority];
      if (SOURCE_REGISTERED == slot.state_
          && NULL != slot.source_
          && lane.delayed_notify_ts_ > 0) {
        handle.slot_id_ = slot_id;
        handle.generation_ = slot.generation_;
        const int64_t now = common::ObTimeUtility::current_time();
        if (lane.delayed_notify_ts_ <= now) {
          lane.delayed_notify_ts_ = 0;
          need_notify = true;
        } else {
          reschedule_ready_ts = lane.delayed_notify_ts_;
          reschedule_delay = lane.delayed_notify_ts_ - now;
          need_reschedule = true;
        }
      }
    }
  }

  if (need_notify) {
    if (OB_FAIL(notify(handle, priority))
        && OB_ENTRY_NOT_EXIST != ret
        && OB_IN_STOP_STATE != ret) {
      LOG_WARN("failed to publish delayed background notification",
          K(ret), K(slot_id), K(priority));
    }
  } else if (need_reschedule) {
    if (OB_FAIL(delayed_notify_timer_.schedule(
        delayed_notify_tasks_[slot_id][priority],
        reschedule_delay,
        false /* repeat */))) {
      LOG_WARN("failed to reschedule early background notification",
          K(ret), K(slot_id), K(priority), K(reschedule_delay));
      bool need_fallback_notify = false;
      {
        lib::ObMutexGuard guard(lock_);
        SourceSlot &slot = source_slots_[slot_id];
        PriorityLaneState &lane = slot.lanes_[priority];
        if (SOURCE_REGISTERED == slot.state_
            && slot.generation_ == handle.generation_
            && lane.delayed_notify_ts_ == reschedule_ready_ts) {
          lane.delayed_notify_ts_ = 0;
          need_fallback_notify = true;
        }
      }
      if (need_fallback_notify) {
        const int notify_ret = notify(handle, priority);
        if (OB_SUCCESS != notify_ret
            && OB_ENTRY_NOT_EXIST != notify_ret
            && OB_IN_STOP_STATE != notify_ret) {
          LOG_WARN_RET(notify_ret,
              "failed to publish fallback early notification",
              K(notify_ret), K(slot_id), K(priority),
              K(reschedule_ready_ts));
        }
      }
    }
  }
}

int ObBackgroundTaskExecutor::ensure_dispatch_tokens_locked()
{
  int ret = OB_SUCCESS;
  const int64_t active_count = get_active_token_count_locked();
  const int64_t desired_count = MIN(
      max_worker_count_, get_runnable_capacity_locked());
  // One call publishes at most one additional dispatch token. Source
  // completion or a genuinely new notification may ramp the pool further.
  int64_t need_count = MIN(
      static_cast<int64_t>(1), desired_count - active_count);
  for (int64_t i = 0; OB_SUCC(ret)
      && need_count > 0 && i < max_worker_count_; ++i) {
    DispatchToken &token = dispatch_tokens_[i];
    if (TOKEN_IDLE == token.state_) {
      token.state_ = TOKEN_QUEUED;
      if (OB_FAIL(common::ObSimpleThreadPool::push(&token))) {
        token.state_ = TOKEN_IDLE;
      } else {
        --need_count;
      }
    }
  }
  // push() owns the queued token even when its first worker creation attempt
  // fails. Retry once here; a later idempotent notify can retry again.
  if (OB_SUCC(ret)
      && get_active_token_count_locked() > 0
      && 0 == common::ObSimpleThreadPool::get_thread_count()
      && !common::ObSimpleThreadPool::try_expand_one_once(max_worker_count_)) {
    ret = OB_EAGAIN;
  }
  return ret;
}

bool ObBackgroundTaskExecutor::is_lane_runnable_locked(
    const int64_t slot_id,
    const ObBackgroundTaskPriority priority) const
{
  bool runnable = false;
  if (slot_id >= 0
      && slot_id < MAX_SOURCE_COUNT
      && is_valid_priority(priority)) {
    const SourceSlot &slot = source_slots_[slot_id];
    const PriorityLaneState &lane = slot.lanes_[priority];
    runnable = SOURCE_REGISTERED == slot.state_
        && NULL != slot.source_
        && lane.notify_epoch_ > lane.suspended_epoch_
        && lane.running_count_ < lane.runnable_limit_
        && slot.running_count_ < slot.config_.max_concurrency_
        && lane.running_count_
            < slot.config_.max_concurrency_by_priority_[priority];
  }
  return runnable;
}

int64_t ObBackgroundTaskExecutor::get_runnable_capacity_locked() const
{
  int64_t capacity = 0;
  for (int64_t slot_id = 0; slot_id < MAX_SOURCE_COUNT; ++slot_id) {
    const SourceSlot &slot = source_slots_[slot_id];
    if (NULL == slot.source_) {
      continue;
    }

    int64_t lane_capacity = 0;
    const uint64_t source_bit = 1ULL << slot_id;
    for (int64_t priority = 0; priority < BG_TASK_PRIORITY_COUNT; ++priority) {
      const PriorityLaneState &lane = slot.lanes_[priority];
      if (0 != (ready_sources_[priority] & source_bit)
          && lane.notify_epoch_ > lane.suspended_epoch_) {
        lane_capacity += MAX(
            static_cast<int64_t>(0),
            MIN(slot.config_.max_concurrency_by_priority_[priority],
                lane.runnable_limit_) - lane.running_count_);
      }
    }
    const int64_t source_capacity = MAX(
        static_cast<int64_t>(0),
        slot.config_.max_concurrency_ - slot.running_count_);
    capacity += MIN(source_capacity, lane_capacity);
    capacity += slot.running_count_;
  }
  return capacity;
}

int64_t ObBackgroundTaskExecutor::get_active_token_count_locked() const
{
  int64_t count = 0;
  for (int64_t i = 0; i < max_worker_count_; ++i) {
    if (TOKEN_IDLE != dispatch_tokens_[i].state_) {
      ++count;
    }
  }
  return count;
}

void ObBackgroundTaskExecutor::clear_ready_bits_locked(const int64_t slot_id)
{
  if (slot_id >= 0 && slot_id < MAX_SOURCE_COUNT) {
    const uint64_t source_bit = 1ULL << slot_id;
    for (int64_t priority = 0; priority < BG_TASK_PRIORITY_COUNT; ++priority) {
      ready_sources_[priority] &= ~source_bit;
    }
  }
}

} // end namespace share
} // end namespace oceanbase
