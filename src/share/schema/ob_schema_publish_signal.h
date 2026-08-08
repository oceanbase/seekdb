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

#ifndef OB_SCHEMA_PUBLISH_SIGNAL_H_
#define OB_SCHEMA_PUBLISH_SIGNAL_H_

#include "lib/atomic/ob_atomic.h"
#include "lib/lock/ob_thread_cond.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

// Process-wide synchronization seam for components that must re-check state
// after a schema publication. The epoch closes the check/wait lost-wakeup race.
class ObSchemaPublishSignal final
{
public:
  ObSchemaPublishSignal()
      : epoch_(0),
        is_inited_(false)
  {}
  ~ObSchemaPublishSignal()
  {
    destroy();
  }

  int init()
  {
    int ret = common::OB_SUCCESS;
    if (is_inited_) {
      ret = common::OB_INIT_TWICE;
    } else if (OB_FAIL(
        cond_.init(common::ObWaitEventIds::THREAD_IDLING_COND_WAIT))) {
    } else {
      ATOMIC_STORE(&epoch_, 0);
      is_inited_ = true;
    }
    return ret;
  }

  void destroy()
  {
    if (is_inited_) {
      cond_.destroy();
      is_inited_ = false;
      ATOMIC_STORE(&epoch_, 0);
    }
  }

  bool is_inited() const
  {
    return is_inited_;
  }

  int64_t current_epoch() const
  {
    return ATOMIC_LOAD(&epoch_);
  }

  void notify_schema_published()
  {
    advance_and_wake_();
  }

  // A consumer uses this during shutdown. Advancing the epoch means a stop
  // notification cannot be lost immediately before the consumer waits.
  void wake_waiters()
  {
    advance_and_wake_();
  }

  int wait_after(int64_t &observed_epoch, const uint64_t timeout_ms)
  {
    int ret = common::OB_SUCCESS;
    if (!is_inited_) {
      ret = common::OB_NOT_INIT;
    } else {
      common::ObThreadCondGuard guard(cond_);
      if (observed_epoch == ATOMIC_LOAD(&epoch_)) {
        ret = cond_.wait(timeout_ms);
      }
      observed_epoch = ATOMIC_LOAD(&epoch_);
    }
    return ret;
  }

private:
  void advance_and_wake_()
  {
    if (is_inited_) {
      common::ObThreadCondGuard guard(cond_);
      ATOMIC_INC(&epoch_);
      (void)cond_.broadcast();
    }
  }

private:
  common::ObThreadCond cond_;
  int64_t epoch_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObSchemaPublishSignal);
};

} // namespace schema
} // namespace share
} // namespace oceanbase

#endif // OB_SCHEMA_PUBLISH_SIGNAL_H_
