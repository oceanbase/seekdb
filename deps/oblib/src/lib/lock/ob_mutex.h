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

#ifndef OB_MUTEX_H_
#define OB_MUTEX_H_

#include "lib/ob_define.h"
#include "lib/stat/ob_latch_define.h"
#include "lib/lock/ob_lock_guard.h"
#include "lib/lock/ob_latch.h"

namespace oceanbase
{
namespace lib
{
class ObMutex {
public:
  explicit ObMutex(uint32_t latch_id = common::ObLatchIds::DEFAULT_MUTEX)
      : latch_(), latch_id_(latch_id)
  {
  }
  ~ObMutex() { }
  inline int lock(const int64_t abs_timeout_us = INT64_MAX) { return latch_.lock(latch_id_, abs_timeout_us); }
  inline int trylock() { return latch_.try_lock(latch_id_); }
  inline int unlock() { return latch_.unlock(); }
  void enable_record_stat(bool enable) { latch_.enable_record_stat(enable); }
  void set_latch_id(const uint32_t latch_id) { latch_id_ = latch_id; }
private:
  common::ObLatchMutex latch_;
  uint32_t latch_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMutex);
};

typedef ObLockGuard<ObMutex> ObMutexGuard;
typedef ObLockGuardWithTimeout<ObMutex> ObMutexGuardWithTimeout;

} // end of namespace lib
} // end of namespace oceanbase


// belows for proxy
typedef pthread_mutex_t ObMutex0;

namespace oceanbase
{
namespace common
{

static inline int mutex_init(ObMutex0 *m)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 != pthread_mutex_init(m, NULL))) {
    ret = OB_ERR_SYS;
    LIB_LOG(ERROR, "mutex init fail", K(ret));
  }
  return ret;
}

static inline int mutex_destroy(ObMutex0 *m)
{
  return (0 == pthread_mutex_destroy(m)) ? OB_SUCCESS : OB_ERR_SYS;
}

static inline int mutex_acquire(ObMutex0 *m)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 != pthread_mutex_lock(m))) {
    ret = OB_ERR_SYS;
    LIB_LOG(ERROR, "mutex acquire fail", K(ret));
  }
  return ret;
}

static inline int mutex_release(ObMutex0 *m)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 != pthread_mutex_unlock(m))) {
    ret = OB_ERR_SYS;
    LIB_LOG(ERROR, "mutex release fail", K(ret));
  }
  return ret;
}

static inline bool mutex_try_acquire(ObMutex0 *m)
{
  return (0 == pthread_mutex_trylock(m));
}

} // end of namespace common
} // end of namespace oceanbase

#endif // OB_MUTEX_H_
