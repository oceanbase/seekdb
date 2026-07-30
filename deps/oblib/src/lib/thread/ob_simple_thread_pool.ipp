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

#include "lib/thread/ob_thread_name.h"
#include "lib/thread/ob_simple_thread_pool.h"
#ifdef __APPLE__
#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace oceanbase
{
namespace common
{

template <class T>
ObSimpleThreadPoolBase<T>::ObSimpleThreadPoolBase()
    : ObSimpleDynamicThreadPool(),
      is_inited_(false), stop_(false),
      queue_driven_expansion_(false),
      external_driver_(false),
      idle_shrink_timeout_us_(SHRINK_TIMEOUT_US),
      last_pressure_ts_(0),
      run_wrapper_(nullptr),
      cur_worker_idx_(0)
{
}

template <class T>
ObSimpleThreadPoolBase<T>::~ObSimpleThreadPoolBase()
{
  if (is_inited_) {
    destroy();
  }
}

template <class T>
int ObSimpleThreadPoolBase<T>::init(
    const int64_t thread_num, const int64_t task_num_limit,
    const char *name)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (thread_num <= 0 || task_num_limit <= 0 || OB_ISNULL(name)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(queue_.init(task_num_limit, name))) {
    COMMON_LOG(WARN, "task queue init failed", K(ret), K(task_num_limit));
  } else {
    is_inited_ = true;
    stop_ = false;
    ATOMIC_STORE(&last_pressure_ts_, ObTimeUtility::current_time());
    name_ = name;
    
    if (max_thread_cnt_ < 0) {
      max_thread_cnt_ = thread_num;
    }
    if (min_thread_cnt_ < 0) {
      min_thread_cnt_ = 0;
    }
    if (OB_FAIL(ObSimpleThreadPoolDynamicMgr::get_instance().bind(this))) {
      COMMON_LOG(WARN, "bind dynamic mgr failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
    destroy();
  } else {
    COMMON_LOG(INFO, "simple thread pool init success",
               KCSTRING(name), K(thread_num), K(task_num_limit));
  }
  return ret;
}

template <class T>
void ObSimpleThreadPoolBase<T>::destroy()
{
  is_inited_ = false;
  stop_ = true;
  // Stop all workers
  {
    lib::ObMutexGuard g(workers_lock_);
    DLIST_FOREACH_NORET(wnode, workers_) {
      wnode->get_data()->stop();
    }
  }
  // Wait and delete all workers
  {
    lib::ObMutexGuard g(workers_lock_);
    DLIST_FOREACH_REMOVESAFE_NORET(wnode, workers_) {
      Worker *w = wnode->get_data();
      workers_.remove(wnode);
      w->wait();
      w->destroy();
      OB_DELETE(Worker, "QThWker", w);
    }
  }
  IGNORE_RETURN ObSimpleThreadPoolDynamicMgr::get_instance().unbind(this);
  queue_.destroy();
}

template <class T>
int ObSimpleThreadPoolBase<T>::push(TaskType *task)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (NULL == task) {
    ret = OB_INVALID_ARGUMENT;
  } else if (stop_) {
    ret = OB_IN_STOP_STATE;
  } else {
    ret = queue_.push(task);
    if (OB_SIZE_OVERFLOW == ret) {
      ret = OB_EAGAIN;
    }
    if (external_driver_) {
      // The external dispatcher is notified by the owning derived class.
    } else if (queue_driven_expansion_) {
      // ObLightyQueue reserves one pop sequence before an idle worker waits,
      // so its size is <= 0 while queued work is already covered by idle
      // workers. A positive size means there is uncovered backlog.
      if (OB_SUCC(ret) && queue_size() > 0) {
        ATOMIC_STORE(&last_pressure_ts_, ObTimeUtility::current_time());
        this->try_expand_one_once(max_thread_cnt_);
      }
    } else if (this->idle_count() == 0) {
      this->try_expand_one(max_thread_cnt_);
    }
  }
  return ret;
}

template <class T>
int ObSimpleThreadPoolBase<T>::pop_task_for_external_driver(TaskType *&task)
{
  int ret = OB_SUCCESS;
  QElemType *queue_task = NULL;
  task = NULL;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (!external_driver_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(queue_.pop(queue_task))) {
  } else {
    task = static_cast<TaskType *>(queue_task);
  }
  return ret;
}

template <class T>
bool ObSimpleThreadPoolBase<T>::do_add_worker()
{
  if (stop_) {
    return false;
  }
  Worker *w = nullptr;
  {
    lib::ObMutexGuard g(workers_lock_);

    w = OB_NEW(Worker, "QThWker", this, static_cast<int64_t>(cur_worker_idx_++));
    if (OB_ISNULL(w)) {
      return false;
    }
    if (run_wrapper_ != nullptr) {
      w->set_run_wrapper(run_wrapper_);
    }
    if (OB_SUCCESS != w->init()) {
      OB_DELETE(Worker, "QThWker", w);
      return false;
    }
  }
  // Start outside the lock: the new worker may call try_expand_one →
  // do_add_worker, which would deadlock if we held workers_lock_
  if (OB_SUCCESS != w->start()) {
    OB_DELETE(Worker, "QThWker", w);
    return false;
  }
  // Add to list only after successful start, so reap_workers() never
  // sees a worker whose start() is still in flight.
  {
    lib::ObMutexGuard g(workers_lock_);
    bool ok = workers_.add_last(&w->worker_node_);
    abort_unless(ok);
  }
  return true;
}

template <class T>
int ObSimpleThreadPoolBase<T>::set_adaptive_thread(
    int64_t min_thread_num, int64_t max_thread_num)
{
  int ret = OB_SUCCESS;
  if (min_thread_num < 0 || max_thread_num <= 0
      || min_thread_num > max_thread_num) {
    ret = OB_INVALID_ARGUMENT;
    COMMON_LOG(WARN, "set_adaptive_thread failed",
               KP(this), K(min_thread_num), K(max_thread_num));
  } else {
    min_thread_cnt_ = min_thread_num;
    max_thread_cnt_ = max_thread_num;
    if (OB_FAIL(ObSimpleThreadPoolDynamicMgr::get_instance().bind(this))) {
      COMMON_LOG(WARN, "bind dynamic mgr failed", K(ret));
    }
    COMMON_LOG(INFO, "set adaptive thread success",
               KP(this), K(min_thread_num), K(max_thread_num));
  }
  return ret;
}

template <class T>
int ObSimpleThreadPoolBase<T>::set_thread_count(int64_t n_threads)
{
  int ret = OB_SUCCESS;
  if (n_threads <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    max_thread_cnt_ = n_threads;
    if (min_thread_cnt_ > max_thread_cnt_) {
      min_thread_cnt_ = max_thread_cnt_;
    }
    if (OB_FAIL(ObSimpleThreadPoolDynamicMgr::get_instance().bind(this))) {
      COMMON_LOG(WARN, "bind dynamic mgr failed", K(ret));
    }
  }
  return ret;
}

template <class T>
void ObSimpleThreadPoolBase<T>::stop()
{
  stop_ = true;
  notify_stop();
  lib::ObMutexGuard g(workers_lock_);
  DLIST_FOREACH_NORET(wnode, workers_) {
    wnode->get_data()->stop();
  }
}

template <class T>
void ObSimpleThreadPoolBase<T>::wait()
{
  lib::ObMutexGuard g(workers_lock_);
  DLIST_FOREACH_NORET(wnode, workers_) {
    wnode->get_data()->wait();
  }
}

template <class T>
void ObSimpleThreadPoolBase<T>::reap_workers()
{
  {
    // Called only from Mgr background thread
    lib::ObMutexGuard g(workers_lock_);
    DLIST_FOREACH_REMOVESAFE_NORET(wnode, workers_) {
      Worker *w = wnode->get_data();
      if (w->has_set_stop()) {
        workers_.remove(wnode);
        w->wait();
        w->destroy();
        OB_DELETE(Worker, "QThWker", w);
      }
    }
  }
  // A queue-driven pool leaves a token queued when a non-blocking cold-start
  // attempt fails. The existing manager provides its retry path.
  if (queue_driven_expansion_
      && !stop_
      && queue_size() > 0
      && 0 == worker_count()) {
    this->try_expand_one_once(max_thread_cnt_);
  }
}

template <class T>
void ObSimpleThreadPoolBase<T>::Worker::run1()
{
  if (NULL != pool_->name_) {
    lib::set_thread_name(pool_->name_, idx_);
  }
  pool_->cur_worker_idx_ = idx_;
  pool_->run1();
  // Worker is exiting — mark stopped for Mgr to reap
  this->has_set_stop() = true;
}

template <class T>
void ObSimpleThreadPoolBase<T>::run1()
{
  int64_t idle_since = 0;
  while (!stop_ && !lib::Thread::current().has_set_stop()) {
    QElemType *task = NULL;
    bool expand = false;
    int ret = this->pop_with_idle([&]() {
      return queue_.pop(task, QUEUE_WAIT_TIME);
    }, expand);

    if (OB_SUCC(ret)) {
      if (OB_NOT_NULL(task)) {
        idle_since = 0;
        if (expand && !queue_driven_expansion_) {
          this->try_expand_one(max_thread_cnt_);
        }
        handle(static_cast<TaskType *>(task));
        if (queue_driven_expansion_
            // A negative ObLightyQueue size represents idle workers that
            // already reserved future pop sequences, not pending work.
            && queue_size() <= 0
            && ObTimeUtility::current_time()
                - ATOMIC_LOAD(&last_pressure_ts_)
                >= idle_shrink_timeout_us_
            && this->try_shrink_one(min_thread_cnt_)) {
          return;
        }
      }
    } else if (OB_ENTRY_NOT_EXIST == ret) {
      int64_t now = ObTimeUtility::current_time();
      if (idle_since == 0) {
        idle_since = now;
      } else if (now - idle_since >= idle_shrink_timeout_us_) {
        if (this->try_shrink_one(min_thread_cnt_)) {
          return;  // Worker::run1 will set has_set_stop
        }
        idle_since = 0;
      }
    }
  }

  if (stop_) {
    int ret = OB_SUCCESS;
    QElemType *task = NULL;
    while (OB_SUCC(queue_.pop(task))) {
      handle_drop(static_cast<TaskType *>(task));
    }
  }
}

} // namespace common
} // namespace oceanbase
