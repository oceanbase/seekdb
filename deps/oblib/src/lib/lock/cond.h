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

#ifndef COND_H
#define COND_H
#include "lib/time/Time.h"
#include "lib/ob_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/wait_event/ob_wait_event.h"
#include "lib/stat/ob_diagnose_info.h"

namespace obutil
{
template<class T> class ObMonitor;

/**
 * A wrapper class of pthread condition that implements a condition variable.
 * @note The condition variable itself is not thread safe and should be protected
 * by a mutex.
 * See also ObThreadCond which is suitable for most situations.
 */
class ObUtilMutex;
typedef ObUtilMutex Mutex;
class Cond
{
public:

  Cond();
  ~Cond();

  void signal();
  void broadcast();
  template <typename Lock> inline bool
  wait(const Lock& lock) const
  {
    bool ret = false;
    if (!lock.acquired()) {
      _OB_LOG(ERROR,"%s","ThreadLockedException");
      ret = false;
    } else {
      ret = wait_impl(lock._mutex);
    }
    return ret;
  }

  template <typename Lock> inline bool
  timed_wait(const Lock& lock, const ObSysTime& timeout) const
  {
    bool ret = false;
    if (!lock.acquired()) {
      _OB_LOG(ERROR,"%s","ThreadLockedException");
      ret = false;
    } else {
      ret = timed_wait_impl(lock._mutex, timeout);
    }
    return ret;
  }
private:
  DISALLOW_COPY_AND_ASSIGN(Cond);
private:

  friend class ObMonitor<Mutex>;

  template <typename M> bool wait_impl(const M&) const;
  template <typename M> bool timed_wait_impl(const M&, const ObSysTime&) const;

  mutable pthread_cond_t _cond;
  mutable pthread_condattr_t _attr;
};

template <typename M> inline bool
Cond::wait_impl(const M& mutex) const
{
  bool ret = true;
  typedef typename M::LockState LockState;

  LockState state;
  mutex.unlock(state);
  oceanbase::common::ObWaitEventGuard
      wait_guard(oceanbase::common::ObWaitEventIds::DEFAULT_COND_WAIT, 0, reinterpret_cast<uint64_t>(this));
  const int rc = ob_pthread_cond_wait(&_cond, state.mutex);
  mutex.lock(state);

  if ( 0 != rc ) {
    _OB_LOG(ERROR,"%s","ThreadSyscallException");
    ret = false;
  }
  return ret;
}

template <typename M> inline bool
Cond::timed_wait_impl(const M& mutex, const ObSysTime& timeout) const
{
  bool ret = true;
  if (timeout < ObSysTime::microSeconds(0)) {
    _OB_LOG(ERROR,"%s","InvalidTimeoutException");
    ret = false;
  } else {
    typedef typename M::LockState LockState;

    LockState state;
    mutex.unlock(state);

    oceanbase::common::ObWaitEventGuard
        wait_guard(oceanbase::common::ObWaitEventIds::DEFAULT_COND_WAIT, timeout.toMicroSeconds(), reinterpret_cast<uint64_t>(this));

    // Use portable timed wait with relative timeout to avoid clock drift issues on macOS
    // Note: Cond class uses pthread_condattr_setclock(CLOCK_MONOTONIC) on Linux, so pass true
    const int rc = ob_pthread_cond_timedwait_us(&_cond, state.mutex, timeout.toMicroSeconds(), true);
    mutex.lock(state);

    if (rc != 0) {
      if ( rc != ETIMEDOUT ) {
        _OB_LOG(ERROR,"%s","ThreadSyscallException");
        ret = false;
      }
    }
  }
  return ret;
}
}// end namespace
#endif
