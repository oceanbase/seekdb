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

#ifndef OCEANBASE_QUEUE_OB_PRIORITY_QUEUE_
#define OCEANBASE_QUEUE_OB_PRIORITY_QUEUE_

#include "lib/queue/ob_link_queue.h"
#include "lib/lock/ob_scond.h"

namespace oceanbase
{
namespace common
{

template <int PRIOS, class QueueType = ObLinkQueue>
class ObPriorityQueueImpl
{
public:
  enum { PRIO_CNT = PRIOS };

  ObPriorityQueueImpl() : sem_(), queue_(), size_(0), limit_(INT64_MAX) {}
  ~ObPriorityQueueImpl() {}

  void set_limit(int64_t limit) { limit_ = limit; }
  inline int64_t size() const { return ATOMIC_LOAD(&size_); }

  int init(int64_t limit, const char* name) {
    UNUSED(name);
    limit_ = limit;
    return OB_SUCCESS;
  }

  int push(ObLink* data, int priority = 0)
  {
    int ret = OB_SUCCESS;
    if (ATOMIC_FAA(&size_, 1) > limit_) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_UNLIKELY(NULL == data) || OB_UNLIKELY(priority < 0) || OB_UNLIKELY(priority >= PRIO_CNT)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "push error, invalid argument", KP(data), K(priority));
    } else if (OB_FAIL(queue_[priority].push(data))) {
      // do nothing
    } else {
      IGNORE_RETURN sem_.signal();
    }
    if (OB_FAIL(ret)) {
      (void)ATOMIC_FAA(&size_, -1);
    }
    return ret;
  }

  int push_front(ObLink* data, int priority)
  {
    int ret = OB_SUCCESS;
    ATOMIC_FAA(&size_, 1);
    if (OB_UNLIKELY(NULL == data) || OB_UNLIKELY(priority < 0) || OB_UNLIKELY(priority >= PRIO_CNT)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "push front error, invalid argument", KP(data), K(priority));
    } else if (OB_FAIL(queue_[priority].push_front(data))) {
      // do nothing
    } else {
      IGNORE_RETURN sem_.signal();
    }
    if (OB_FAIL(ret)) {
      (void)ATOMIC_FAA(&size_, -1);
    }
    return ret;
  }

  int pop(ObLink*& data, int64_t timeout_us = 0)
  {
    int ret = OB_ENTRY_NOT_EXIST;
    if (OB_UNLIKELY(timeout_us < 0)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(ERROR, "timeout is invalid", K(ret), K(timeout_us));
    } else {
      for(int i = 0; OB_ENTRY_NOT_EXIST == ret  && i < PRIO_CNT; i++) {
        if (OB_SUCCESS == queue_[i].pop(data)) {
          ret = OB_SUCCESS;
        }
      }
      if (OB_FAIL(ret)) {
        auto key = sem_.get_key();
        {
          sem_.wait(key, timeout_us);
        }
        data = NULL;
      } else {
        (void)ATOMIC_FAA(&size_, -1);
      }
    }
    return ret;
  }

  void destroy()
  {
    clear();
  }

  void clear()
  {
    ObLink* p = NULL;
    while(OB_SUCCESS == pop(p, 0))
      ;
  }

  // Broadcast to wake up every worker currently blocked in pop().
  void wake_all()
  {
    (void)sem_.signal(INT32_MAX);
  }

  QueueType* get_queue(const int i)
  {
    return &queue_[i];
  }

  int64_t get_prio_cnt() const
  {
    return PRIO_CNT;
  }
private:
  SimpleCond sem_;
  QueueType queue_[PRIO_CNT];
  int64_t size_ CACHE_ALIGNED;
  int64_t limit_ CACHE_ALIGNED;
  DISALLOW_COPY_AND_ASSIGN(ObPriorityQueueImpl);
};

template <int PRIOS>
using ObPriorityQueue = ObPriorityQueueImpl<PRIOS, ObLinkQueue>;

template <int PRIOS>
using ObPriorityQueue16 = ObPriorityQueueImpl<PRIOS, ObLinkQueue16>;

using ObTLinkQueue16 = ObPriorityQueue16<1>;

template <int HIGH_PRIOS, int NORMAL_PRIOS=0, int LOW_PRIOS=0>
class ObPriorityQueue2
{
public:
  enum { PRIO_CNT = HIGH_PRIOS + NORMAL_PRIOS + LOW_PRIOS };

  ObPriorityQueue2() : queue_(), limit_(INT64_MAX) {}
  ~ObPriorityQueue2() {}

  void set_limit(int64_t limit) { limit_ = limit; }
  int32_t get_queue_num() const { return 1; }
  inline int64_t size() const { return ATOMIC_LOAD(&queue_.size); }
  int64_t queue_size(const int prio) const
  {
    return queue_.q[prio].size();
  }
  int64_t get_prio_cnt() const
  {
    return PRIO_CNT;
  }
  ObLinkQueue* get_queue(const int queue_idx, const int prio)
  {
    ObLinkQueue *queue = NULL;

    if (0 == queue_idx && prio >= 0 && prio < PRIO_CNT) {
      queue = &queue_.q[prio];
    }

    return queue;
  }
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    common::databuff_printf(buf, buf_len, pos, "total_size=%ld ", size());
    for(int j = 0; j < PRIO_CNT; j++) {
      common::databuff_printf(buf, buf_len, pos, "queue[0][%d]=%ld ", j, queue_.q[j].size());
    }
    return pos;
  }

  int push(ObLink* data, int priority,  bool fixed_wakeup_order = false)
  {
    int ret = OB_SUCCESS;
    int64_t extra;

    if (priority < HIGH_PRIOS) {
      extra = 2048;
    } else if (priority < NORMAL_PRIOS + HIGH_PRIOS) {
      extra = 1024;
    } else {
      extra = 0;
    }

    if (ATOMIC_FAA(&queue_.size, 1) > limit_ + extra) {
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_UNLIKELY(NULL == data) || OB_UNLIKELY(priority < 0) || OB_UNLIKELY(priority >= PRIO_CNT)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(WARN, "push error, invalid argument", KP(data), K(priority));
    } else if (OB_FAIL(queue_.q[priority].push(data))) {
      // do nothing
    } else {
      if (priority < HIGH_PRIOS) {
        queue_.cond.signal(1, 0, fixed_wakeup_order);
      } else if (priority < NORMAL_PRIOS + HIGH_PRIOS) {
        queue_.cond.signal(1, 1, fixed_wakeup_order);
      } else {
        queue_.cond.signal(1, 2, fixed_wakeup_order);
      }
    }

    if (OB_FAIL(ret)) {
      (void)ATOMIC_FAA(&queue_.size, -1);
    }
    return ret;
  }

  void wakeup(int priority = PRIO_CNT - 1) {
    queue_.cond.signal(1, priority);
  }

  int pop(ObLink*& data, int64_t timeout_us, int32_t index = -1)
  {
    return do_pop(data, PRIO_CNT, timeout_us, index);
  }

  int pop_normal(ObLink*& data, int64_t timeout_us)
  {
    return do_pop(data, HIGH_PRIOS + NORMAL_PRIOS, timeout_us);
  }

  int pop_high(ObLink*& data, int64_t timeout_us)
  {
    return do_pop(data, HIGH_PRIOS, timeout_us);
  }

private:

  inline int try_pop(ObLink*& data, int64_t plimit)
  {
    int ret = OB_ENTRY_NOT_EXIST;

    for(int i = 0; OB_ENTRY_NOT_EXIST == ret  && i < plimit; i++) {
      if (OB_SUCCESS == queue_.q[i].pop(data)) {
        ret = OB_SUCCESS;
      }
    }

    return ret;
  }
  inline int do_pop(ObLink*& data, int64_t plimit, int64_t timeout_us, int32_t index = -1)
  {
    int ret = OB_ENTRY_NOT_EXIST;

    if (OB_UNLIKELY(timeout_us < 0)) {
      ret = OB_INVALID_ARGUMENT;
      COMMON_LOG(ERROR, "timeout is invalid", K(ret), K(timeout_us));
    } else {
      if (plimit <= HIGH_PRIOS) {
        queue_.cond.prepare(0, index);
      } else if (plimit <= NORMAL_PRIOS + HIGH_PRIOS) {
        queue_.cond.prepare(1, index);
      } else {
        queue_.cond.prepare(2, index);
      }
      if (OB_SUCC(try_pop(data, plimit))) {
      } else if (OB_SUCCESS == queue_.cond.wait(timeout_us)) {
      }

      if (OB_SUCCESS == ret) {
        (void)ATOMIC_FAA(&queue_.size, -1);
      } else {
        data = NULL;
      }
    }
    return ret;
  }

  class Queue_ {
  public:
    Queue_(): size(0) {}
    ~Queue_() {}
    SCondTemp<3> cond;
    ObLinkQueue q[PRIO_CNT];
    int64_t size;

  } CACHE_ALIGNED;
  Queue_ queue_;
  int64_t limit_;
  DISALLOW_COPY_AND_ASSIGN(ObPriorityQueue2);
};
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_QUEUE_OB_PRIORITY_QUEUE_
