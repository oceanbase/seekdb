/**
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

#ifndef _OCEABASE_LIB_THREAD_OB_ADAPTIVE_WORKER_POOL_H_
#define _OCEABASE_LIB_THREAD_OB_ADAPTIVE_WORKER_POOL_H_

#include <atomic>
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace lib
{

/**
 * ObAdaptiveWorkerPool — a CRTP template providing CAS-based dynamic worker
 * scaling. It contains no expansion/contraction *policy*: callers decide the
 * limit and floor for each operation by passing them to try_expand_one() and
 * try_shrink_one().
 *
 * ## Derived contract (CRTP)
 *   bool do_add_worker();       // create + start one worker, return success
 *   int64_t queue_size() const;   // for shrink-to-0 safety guard
 *
 * ## Reaping stopped workers (required)
 * Workers that exit via try_shrink_one() call Worker::stop() and then break
 * out of the worker loop, leaving a "zombie" entry in the workers_ list.
 * The derived class MUST run a periodic background task (e.g. timeup())
 * to reap these stopped workers — remove their nodes from the list and
 * destroy them. See ObServerRuntime::check_worker_count().
 *
 * ## Achieving th_worker-style min/max limits
 * Two independent limits, enforced at different call sites:
 *
 *   min / normal ceiling    — a CPU-derived count the pool should operate
 *                             at under normal load.
 *     recv_request:   try_expand_one(min_limit)  on cold start
 *     worker loop:    try_expand_one(min_limit)  when expand signal fires
 *
 *   max / rescue ceiling    — a memory-derived hard cap never exceeded,
 *                             only used when workers appear deadlocked.
 *     timeup:         try_expand_one(max_limit)  when completion stalls for
 *                                              N seconds with non-empty queue
 *
 * Callers check worker_count() against the min limit to decide *whether* to
 * call try_expand_one(); the CAS loop inside try_expand_one() enforces the
 * hard max cap.
 */
template <typename Derived>
class ObAdaptiveWorkerPool
{
public:
  ObAdaptiveWorkerPool() : idle_cnt_(0), total_cnt_(0) {}

  template <typename PopFn>
  int pop_with_idle(PopFn &&pop_fn, bool &expand) {
    idle_enter();
    int ret = pop_fn();
    expand = (idle_exit() == 1);
    return ret;
  }

  /// CAS-based expansion up to the given limit.
  bool try_expand_one(int64_t limit)
  {
    int64_t cur = total_cnt_.load(std::memory_order_relaxed);
    while (cur < limit) {
      if (total_cnt_.compare_exchange_weak(cur, cur + 1,
              std::memory_order_acq_rel, std::memory_order_relaxed)) {
        while (!self().do_add_worker()) {
          ob_usleep(1000);
        }
        return true;
      }
    }
    return false;
  }

  /// A non-blocking expansion attempt for pools that can safely retry from
  /// their queue/manager path. Unlike try_expand_one(), worker creation
  /// failure rolls the reservation back instead of spinning in the caller.
  bool try_expand_one_once(int64_t limit)
  {
    int64_t cur = total_cnt_.load(std::memory_order_relaxed);
    while (cur < limit) {
      if (total_cnt_.compare_exchange_weak(cur, cur + 1,
              std::memory_order_acq_rel, std::memory_order_relaxed)) {
        if (!self().do_add_worker()) {
          total_cnt_.fetch_sub(1, std::memory_order_acq_rel);
          return false;
        }
        return true;
      }
    }
    return false;
  }

  /// CAS-based shrink down to the given floor.
  /// Refuses to shrink to 0 when queue is non-empty.
  bool try_shrink_one(int64_t floor)
  {
    int64_t cur = total_cnt_.load(std::memory_order_relaxed);
    while (cur > floor) {
      if (total_cnt_.compare_exchange_weak(cur, cur - 1,
              std::memory_order_acq_rel, std::memory_order_relaxed)) {
        if (cur - 1 == 0 && self().queue_size() > 0) {
          total_cnt_.fetch_add(1, std::memory_order_relaxed);
          return false;
        }
        return true;
      }
    }
    return false;
  }

protected:
  int64_t idle_count() const { return idle_cnt_.load(std::memory_order_relaxed); }
  int64_t worker_count() const { return total_cnt_.load(std::memory_order_relaxed); }
  void reset_worker_counts()
  {
    idle_cnt_.store(0, std::memory_order_release);
    total_cnt_.store(0, std::memory_order_release);
  }

private:
  int64_t idle_enter() { return idle_cnt_.fetch_add(1, std::memory_order_relaxed); }
  int64_t idle_exit() { return idle_cnt_.fetch_sub(1, std::memory_order_relaxed); }
  Derived &self() { return *static_cast<Derived *>(this); }
  const Derived &self() const { return *static_cast<const Derived *>(this); }

  std::atomic<int64_t> idle_cnt_;
  std::atomic<int64_t> total_cnt_;
};

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OCEABASE_LIB_THREAD_OB_ADAPTIVE_WORKER_POOL_H_ */
