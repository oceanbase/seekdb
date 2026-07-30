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

#define USING_LOG_PREFIX SHARE

#include "lib/thread/ob_async_task_queue.h"
#include "lib/profile/ob_trace_id.h"
namespace oceanbase
{
using namespace common;
using namespace lib;
namespace share
{
ObAsyncTaskQueue::ObAsyncTaskQueue()
  : is_inited_(false),
    use_external_driver_(false),
    queue_(),
    allocator_(),
    external_pending_task_(NULL)
{
}

ObAsyncTaskQueue::~ObAsyncTaskQueue()
{
  int ret = destroy();
  if (OB_FAIL(ret)) {
    LOG_WARN("destroy failed", K(ret));
  }
}

int ObAsyncTaskQueue::init(const int64_t thread_cnt, const int64_t queue_size, const char *thread_name, const int64_t page_size)
{
  return init_(
      thread_cnt, queue_size, thread_name, page_size, true /* create_thread */);
}

int ObAsyncTaskQueue::init_without_thread(
    const int64_t queue_size,
    const int64_t page_size)
{
  return init_(
      0, queue_size, NULL, page_size, false /* create_thread */);
}

int ObAsyncTaskQueue::init_(
    const int64_t thread_cnt,
    const int64_t queue_size,
    const char *thread_name,
    const int64_t page_size,
    const bool create_thread)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("task queue has already been initialized", K(ret));
  } else if ((create_thread && thread_cnt <= 0)
      || queue_size <= 0
      || 0 != (queue_size & (queue_size - 1))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(thread_cnt), K(queue_size), K(ret));
  } else if (OB_FAIL(allocator_.init(TOTAL_LIMIT, HOLD_LIMIT, page_size))) {
    LOG_WARN("allocator init failed", "total limit", static_cast<int64_t>(TOTAL_LIMIT),
        "hold limit", static_cast<int64_t>(HOLD_LIMIT),
        "page size",  static_cast<int64_t>(ALLOC_PAGE_SIZE), K(ret));
  } else if (OB_FAIL(queue_.init(queue_size))) {
    LOG_WARN("queue init failed", K(queue_size), K(ret));
  } else if (create_thread && OB_FAIL(create(thread_cnt, thread_name))) {
    LOG_WARN("create async task thread failed", K(ret), K(thread_cnt));
  } else {
    allocator_.set_attr(ObMemAttr("AsyncTaskQueue"));
    use_external_driver_ = !create_thread;
    external_pending_task_ = NULL;
    is_inited_ = true;
  }
  return ret;
}

int ObAsyncTaskQueue::destroy()
{
  int ret = ObReentrantThread::destroy();
  if (OB_FAIL(ret)) {
    LOG_WARN("reentrant thread thread failed", K(ret));
  }
  if (is_inited_) {
    if (use_external_driver_) {
      clear_external_tasks_();
    }
    queue_.destroy();
    allocator_.destroy();
    external_pending_task_ = NULL;
    use_external_driver_ = false;
    is_inited_ = false;
  }
  return ret;
}

int ObAsyncTaskQueue::push(const ObAsyncTask &task)
{
  int ret = OB_SUCCESS;
  ObAsyncTask *task_ptr = NULL;
  const int64_t buf_size = task.get_deep_copy_size();
  char *buf = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (NULL == (buf = static_cast<char *>(allocator_.alloc(buf_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("allocator alloc memory failed", K(buf_size), K(ret));
  } else if (NULL == (task_ptr = task.deep_copy(buf, buf_size))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("task deep copy failed", K(ret));
    allocator_.free(buf);
    buf = NULL;
  } else {
    task_ptr->set_retry_times(task.get_retry_times());
    task_ptr->set_retry_interval(task.get_retry_interval());
    if (OB_FAIL(queue_.push(task_ptr))) {
      LOG_WARN("push task to queue failed", K(ret), "queue_size", queue_.size());
      task_ptr->~ObAsyncTask();
      allocator_.free(buf);
      buf = NULL;
    }
  }
  return ret;
}

void ObAsyncTaskQueue::run2()
{
  int ret = OB_SUCCESS;
  LOG_INFO("async task queue start");
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObAddr zero_addr;
    while (!stop_) {
      IGNORE_RETURN lib::Thread::update_loop_ts(ObTimeUtility::fast_current_time());
      if (REACH_TIME_INTERVAL(600 * 1000 * 1000)) {
        // Print the size of the queue at regular intervals
        LOG_INFO("[ASYNC TASK QUEUE]", "queue_size", queue_.size());
      }
      ObAsyncTask *task = NULL;
      ret = pop(task);
      if (OB_FAIL(ret))  {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("pop task from queue failed", K(ret));
        }
      } else if (NULL == task) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("pop return a null task", K(ret));
      } else {
        bool rescheduled = false;
        if (task->get_last_execute_time() > 0) {
          while (!stop_ && OB_SUCC(ret)) {
            int64_t now = ObTimeUtility::current_time();
            int64_t sleep_time = task->get_last_execute_time() + task->get_retry_interval() - now;
            if (sleep_time > 0) {
              ob_throttle_usleep(static_cast<int32_t>(MIN(sleep_time, SLEEP_INTERVAL)), 0);
            } else {
              break;
            }
          }
        }
        // generate trace id
        ObCurTraceId::init(zero_addr);
        // just do it
        ret = task->process();
        if (OB_FAIL(ret)) {
          LOG_WARN("task process failed, start retry", "max retry time",
              task->get_retry_times(), "retry interval", task->get_retry_interval(),
              K(ret));
          if (task->get_retry_times() > 0) {
            task->set_retry_times(task->get_retry_times() - 1);
            task->set_last_execute_time(ObTimeUtility::current_time());
            if (OB_FAIL(queue_.push(task))) {
              LOG_ERROR("push task to queue failed", K(ret));
            } else {
              rescheduled = true;
            }
          }
        }
        if (!rescheduled) {
          task->~ObAsyncTask();
          allocator_.free(task);
          task = NULL;
        }
      }
    }
  }
  LOG_INFO("async task queue stop");
}
int ObAsyncTaskQueue::start()
{
  return use_external_driver_ ? OB_SUCCESS : logical_start();
}
void ObAsyncTaskQueue::stop()
{
  if (!use_external_driver_) {
    logical_stop();
  }
}
void ObAsyncTaskQueue::wait()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (use_external_driver_) {
    clear_external_tasks_();
  } else {
    logical_wait();
    ObAsyncTask *task = NULL;
    while (queue_.size() > 0 && OB_SUCCESS == pop(task)) {
      task->~ObAsyncTask();
      allocator_.free(task);
      task = NULL;
    }
  }
}

int ObAsyncTaskQueue::pop(ObAsyncTask *&task)
{
  int ret = OB_SUCCESS;
  void *vp = NULL;
  const int64_t timeout = 1000 * 1000;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ret = queue_.pop(vp, timeout);
    if (OB_FAIL(ret)) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("queue pop failed", K(ret));
      }
    } else {
      task = static_cast<ObAsyncTask *>(vp);
    }
  }
  return ret;
}

int ObAsyncTaskQueue::try_pop(ObAsyncTask *&task)
{
  int ret = OB_SUCCESS;
  void *vp = NULL;
  task = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(queue_.pop(vp, 0))) {
  } else {
    task = static_cast<ObAsyncTask *>(vp);
  }
  return ret;
}

int ObAsyncTaskQueue::process_one_task(
    bool &processed,
    int64_t &next_ready_ts,
    bool &has_more_ready)
{
  int ret = OB_SUCCESS;
  processed = false;
  next_ready_ts = 0;
  has_more_ready = false;
  ObAsyncTask *task = external_pending_task_;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!use_external_driver_) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    external_pending_task_ = NULL;
    if (OB_ISNULL(task)) {
      const int pop_ret = try_pop(task);
      if (OB_ENTRY_NOT_EXIST == pop_ret) {
      } else if (OB_SUCCESS != pop_ret) {
        ret = pop_ret;
        LOG_WARN("pop externally driven async task failed", K(ret));
      }
    }

    if (OB_SUCC(ret) && OB_NOT_NULL(task)) {
      const int64_t now = ObTimeUtility::current_time();
      const int64_t ready_ts = get_external_task_ready_ts_(*task, now);
      if (ready_ts > now) {
        external_pending_task_ = task;
        next_ready_ts = ready_ts;
      } else {
        bool rescheduled = false;
        ObAddr zero_addr;
        ObCurTraceId::TraceId trace_id;
        trace_id.init(zero_addr);
        ObTraceIdGuard trace_id_guard(trace_id);
        const int task_ret = task->process();
        processed = true;
        on_external_task_processed_(*task, task_ret);
        if (OB_SUCCESS != task_ret) {
          LOG_WARN_RET(task_ret, "task process failed, start retry",
              "max retry time", task->get_retry_times(),
              "retry interval", task->get_retry_interval());
          if (task->get_retry_times() > 0
              && can_retry_external_task_(*task)) {
            task->set_retry_times(task->get_retry_times() - 1);
            task->set_last_execute_time(ObTimeUtility::current_time());
            const int push_ret = queue_.push(task);
            if (OB_SUCCESS != push_ret) {
              LOG_ERROR_RET(push_ret, "push retry task to queue failed");
            } else {
              rescheduled = true;
            }
          }
        }
        if (!rescheduled) {
          task->~ObAsyncTask();
          allocator_.free(task);
          task = NULL;
        }
        has_more_ready = queue_.size() > 0;
      }
    }
  }
  return ret;
}

int64_t ObAsyncTaskQueue::get_external_task_ready_ts_(
    const ObAsyncTask &task,
    const int64_t now) const
{
  UNUSED(now);
  return task.get_last_execute_time() > 0
      ? task.get_last_execute_time() + task.get_retry_interval()
      : 0;
}

bool ObAsyncTaskQueue::can_retry_external_task_(
    const ObAsyncTask &task) const
{
  UNUSED(task);
  return true;
}

void ObAsyncTaskQueue::on_external_task_processed_(
    ObAsyncTask &task,
    const int process_ret)
{
  UNUSEDx(task, process_ret);
}

void ObAsyncTaskQueue::clear_external_tasks_()
{
  if (OB_NOT_NULL(external_pending_task_)) {
    external_pending_task_->~ObAsyncTask();
    allocator_.free(external_pending_task_);
    external_pending_task_ = NULL;
  }
  ObAsyncTask *task = NULL;
  while (OB_SUCCESS == try_pop(task)) {
    if (OB_NOT_NULL(task)) {
      task->~ObAsyncTask();
      allocator_.free(task);
      task = NULL;
    }
  }
}

}//end namespace share
}//end namespace oceanbase
