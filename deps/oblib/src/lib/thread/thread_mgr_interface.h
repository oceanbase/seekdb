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

#ifndef OBLIB_THREAD_MGR_INTERFACE_H
#define OBLIB_THREAD_MGR_INTERFACE_H

#include "lib/atomic/ob_atomic.h"
#include "lib/thread/threads.h"
#include "lib/lock/ob_thread_cond.h"

namespace oceanbase {

namespace common {
// used for thread task queue
struct LinkTask : public ObLink {};
}

namespace lib {

class TGRunnable
{
public:
  virtual void run1() = 0;
  bool has_set_stop() const
  {
    IGNORE_RETURN lib::Thread::update_loop_ts();
    return ATOMIC_LOAD(&stop_);
  }
  void set_stop(bool stop)
  {
    stop_ = stop;
  }
  uint64_t get_thread_idx() const
  {
    return thread_idx_;
  }
  void set_thread_idx(uint64_t thread_idx)
  {
    thread_idx_ = thread_idx;
  }
public:
  common::ObThreadCond *cond_ = nullptr;
private:
  bool stop_ = true;
  static TLOCAL(uint64_t, thread_idx_);
};

template <typename TaskType = void>
class TGTaskHandlerTemplate
{
public:
  virtual void handle(TaskType *task) = 0;

  virtual void handle(TaskType *task, volatile bool &is_stoped)
  {}
  // when thread set stop left task will be process by handle_drop (default impl is handle)
  // users should define it's behaviour to manage task memory or some what
  virtual void handle_drop(TaskType *task) {
    handle(task);
  };
  uint64_t get_thread_idx() const
  {
    return thread_idx_;
  }
  void set_thread_idx(uint64_t thread_idx)
  {
    thread_idx_ = thread_idx;
  }
  void set_thread_cnt(int64_t n_threads)
  {
    n_threads_ = n_threads;
  }
  int64_t get_thread_cnt()
  {
    return n_threads_;
  }
private:
  int64_t n_threads_ = 0;
  static TLOCAL(uint64_t, thread_idx_);
};

using TGTaskHandler = TGTaskHandlerTemplate<>;
using TGLinkTaskHandler = TGTaskHandlerTemplate<common::LinkTask>;

template <typename TaskType>
TLOCAL(uint64_t, TGTaskHandlerTemplate<TaskType>::thread_idx_) = 0;

} // end of namespace lib
} // end of namespace oceanbase

#endif /* OBLIB_THREAD_MGR_INTERFACE_H */
