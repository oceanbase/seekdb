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

#include "common/ob_queue_thread.h"

extern "C" {
int ob_pthread_create(void **ptr, void *(*start_routine) (void *), void *arg);
void ob_pthread_join(void *ptr);
}

namespace oceanbase
{
namespace common
{
ObCond::ObCond(const int64_t spin_wait_num) : spin_wait_num_(spin_wait_num),
                                              bcond_(false),
                                              last_waked_time_(0)
{
  pthread_mutex_init(&mutex_, NULL);
  if (0 != pthread_cond_init(&cond_, NULL)) {
    _OB_LOG_RET(ERROR, common::OB_ERR_SYS, "pthread_cond_init failed");
  }
}

ObCond::~ObCond()
{
  pthread_mutex_destroy(&mutex_);
  pthread_cond_destroy(&cond_);
}

void ObCond::signal()
{
  if (false == ATOMIC_CAS(&bcond_, false, true)) {
    __sync_synchronize();
    (void)pthread_mutex_lock(&mutex_);
    (void)pthread_cond_signal(&cond_);
    (void)pthread_mutex_unlock(&mutex_);
  }
}

int ObCond::wait()
{
  int ret = OB_SUCCESS;
  bool need_wait = true;
  if ((last_waked_time_ + BUSY_INTERVAL) > ::oceanbase::common::ObTimeUtility::current_time()) {
    //return OB_SUCCESS;
    for (int64_t i = 0; i < spin_wait_num_; i++) {
      if (true == ATOMIC_CAS(&bcond_, true, false)) {
        need_wait = false;
        break;
      }
      PAUSE();
    }
  }
  if (need_wait) {
    pthread_mutex_lock(&mutex_);
    while (OB_SUCC(ret) && false == ATOMIC_CAS(&bcond_, true, false)) {
      int tmp_ret = ob_pthread_cond_wait(&cond_, &mutex_);
      if (ETIMEDOUT == tmp_ret) {
        ret = OB_TIMEOUT;
        break;
      }
    }
    pthread_mutex_unlock(&mutex_);
  }
  if (OB_SUCC(ret)) {
    last_waked_time_ = ::oceanbase::common::ObTimeUtility::current_time();
  }
  return ret;
}
int ObCond::timedwait(const int64_t time_us)
{
  int ret = OB_SUCCESS;
  bool need_wait = true;
  if ((last_waked_time_ + BUSY_INTERVAL) > ::oceanbase::common::ObTimeUtility::current_time()) {
    for (int64_t i = 0; i < spin_wait_num_; i++) {
      if (true == ATOMIC_CAS(&bcond_, true, false)) {
        need_wait = false;
        break;
      }
      PAUSE();
    }
  }
  if (need_wait) {
    pthread_mutex_lock(&mutex_);
    while (OB_SUCC(ret) && false == ATOMIC_CAS(&bcond_, true, false)) {
      // Use portable timed wait with relative timeout to avoid clock drift issues on macOS
      int tmp_ret = ob_pthread_cond_timedwait_us(&cond_, &mutex_, time_us);
      if (ETIMEDOUT == tmp_ret) {
        ret = OB_TIMEOUT;
        break;
      }
    }
    pthread_mutex_unlock(&mutex_);
  }
  if (OB_SUCC(ret)) {
    last_waked_time_ = ::oceanbase::common::ObTimeUtility::current_time();
  }
  return ret;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

S2MQueueThread::S2MQueueThread() : thread_num_(0),
                                   thread_conf_iter_(0),
                                   thread_conf_lock_(ObLatchIds::DEFAULT_DRW_LOCK),
                                   queued_num_(),
                                   queue_rebalance_(false)
{
  memset((void *)each_queue_len_, 0, sizeof(each_queue_len_));
}

S2MQueueThread::~S2MQueueThread()
{
  destroy();
}


void S2MQueueThread::destroy()
{
  for (int64_t i = 0; i < thread_num_; i++) {
    ThreadConf &tc = thread_conf_array_[i];
    tc.run_flag = false;
    tc.stop_flag = true;
    tc.queue_cond.signal();
    ob_pthread_join(tc.pd);
  }
  for (int64_t i = 0; i < thread_num_; i++) {
    ThreadConf &tc = thread_conf_array_[i];
    tc.high_prio_task_queue.destroy();
    tc.spec_task_queue.destroy();
    tc.comm_task_queue.destroy();
    tc.low_prio_task_queue.destroy();
  }
  balance_filter_.destroy();
  thread_num_ = 0;
}








void *S2MQueueThread::rebalance_(int64_t &idx, const ThreadConf &cur_thread)
{
  void *ret = NULL;
  // Note: debug only
  RLOCAL(int64_t, rebalance_counter);
  for (uint64_t i = 1; (int64_t)i <= thread_num_; i++) {
    RDLockGuard guard(thread_conf_lock_);
    uint64_t balance_idx = (cur_thread.index + i) % thread_num_;
    ThreadConf &tc = thread_conf_array_[balance_idx];
    int64_t try_queue_idx = -1;
    if (tc.using_flag
       ) { //&& (tc.last_active_time + THREAD_BUSY_TIME_LIMIT) < ::oceanbase::common::ObTimeUtility::current_time())
      continue;
    }
    if (NULL == ret) {
      try_queue_idx = HIGH_PRIO_QUEUE;
      IGNORE_RETURN tc.high_prio_task_queue.pop(ret);
    }
    if (NULL == ret) {
      try_queue_idx = NORMAL_PRIO_QUEUE;
      IGNORE_RETURN tc.comm_task_queue.pop(ret);
    }
    if (NULL != ret) {
      idx = try_queue_idx;
      each_queue_len_[idx].inc(-1);
      if (0 == (rebalance_counter++ % 10000)) {
        _OB_LOG(INFO,
                  "task has been rebalance between threads rebalance_counter=%ld cur_idx=%ld balance_idx=%ld",
                  *(&rebalance_counter), cur_thread.index, balance_idx);
      }
      break;
    }
  }
  return ret;
}


void *S2MQueueThread::thread_func_(void *data)
{
  ThreadConf *const tc = (ThreadConf *)data;
  if (NULL == tc
      || NULL == tc->host) {
    _OB_LOG_RET(WARN, common::OB_INVALID_ARGUMENT, "thread_func param null pointer");
  } else {
    tc->host->thread_index() = tc->index;
    void *pdata = tc->host->on_begin();
    bool last_rebalance_got = false;
    while (tc->run_flag)
      // && (0 != tc->high_prio_task_queue.get_total()
      //     || 0 != tc->spec_task_queue.get_total()
      //     || 0 != tc->comm_task_queue.get_total()
      //     || 0 != tc->low_prio_task_queue.get_total()))
    {
      void *task = NULL;
      int64_t start_time = ::oceanbase::common::ObTimeUtility::current_time();
      int64_t idx = -1;
      tc->using_flag = true;
      switch (tc->scheduler_.get()) {
        case 0:
          IGNORE_RETURN tc->high_prio_task_queue.pop(task);
          if (NULL != task) {
            idx = 0;
          }
          break;
        case 1:
          IGNORE_RETURN tc->spec_task_queue.pop(task);
          if (NULL != task) {
            idx = 1;
          }
          break;
        case 2:
          IGNORE_RETURN tc->comm_task_queue.pop(task);
          if (NULL != task) {
            idx = 2;
          }
          break;
        case 3:
          IGNORE_RETURN tc->low_prio_task_queue.pop(task);
          if (NULL != task) {
            idx = 3;
          }
          break;
        default:
          ;
      };
      if (NULL == task) {
        IGNORE_RETURN tc->high_prio_task_queue.pop(task);
        if (NULL != task) {
          idx = 0;
        }
      }
      if (NULL == task) {
        IGNORE_RETURN tc->spec_task_queue.pop(task);
        if (NULL != task) {
          idx = 1;
        }
      }
      if (NULL == task) {
        IGNORE_RETURN tc->comm_task_queue.pop(task);
        if (NULL != task) {
          idx = 2;
        }
      }
      if (NULL == task) {
        IGNORE_RETURN tc->low_prio_task_queue.pop(task);
        if (NULL != task) {
          idx = 3;
        }
      }
      tc->using_flag = false; // not need strict consist, so do not use barrier
      if (idx >= 0 && idx < QUEUE_COUNT) {
        tc->host->each_queue_len_[idx].inc(-1);
      }
      if (NULL != task
          || (tc->host->queue_rebalance_
              && (last_rebalance_got || TC_REACH_TIME_INTERVAL(QUEUE_WAIT_TIME))
              && (last_rebalance_got = (NULL != (task = tc->host->rebalance_(idx, *tc)))))) {
        tc->host->queued_num_.inc(-1);
        tc->host->handle_with_stopflag(task, pdata, tc->stop_flag);
        tc->last_active_time = ::oceanbase::common::ObTimeUtility::current_time();
      } else {
        tc->queue_cond.timedwait(QUEUE_WAIT_TIME);
      }
      tc->host->on_iter();
      v4si queue_len =
      {
        tc->high_prio_task_queue.get_total(),
        tc->spec_task_queue.get_total(),
        tc->comm_task_queue.get_total(),
        tc->low_prio_task_queue.get_total(),
      };
      tc->scheduler_.update(idx, ::oceanbase::common::ObTimeUtility::current_time() - start_time, queue_len);
    }
    tc->host->on_end(pdata);
  }
  return NULL;
}


////////////////////////////////////////////////////////////////////////////////////////////////////

const int64_t M2SQueueThread::QUEUE_WAIT_TIME = 100 * 1000;

M2SQueueThread::M2SQueueThread() : inited_(false),
                                   pd_(nullptr),
                                   run_flag_(true),
                                   queue_cond_(),
                                   task_queue_(),
                                   idle_interval_(INT64_MAX),
                                   last_idle_time_(0)
{
}

M2SQueueThread::~M2SQueueThread()
{
  destroy();
}


void M2SQueueThread::destroy()
{
  if (nullptr != pd_) {
    run_flag_ = false;
    queue_cond_.signal();
    ob_pthread_join(pd_);
    pd_ = nullptr;
  }

  task_queue_.destroy();

  inited_ = false;
}





}
}
