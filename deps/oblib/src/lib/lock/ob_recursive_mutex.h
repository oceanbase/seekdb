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

#ifndef OB_RECURSIVE_MUTEX_H_
#define OB_RECURSIVE_MUTEX_H_

#include "lib/lock/ob_latch.h"
#include "lib/lock/ob_lock_guard.h"

namespace oceanbase
{
namespace common
{
class ObRecursiveMutex
{
public:
  explicit ObRecursiveMutex(const uint32_t latch_id);
  ~ObRecursiveMutex();
  int lock();
  int unlock();
  int trylock();
private:
  ObLatch latch_;
  uint32_t latch_id_;
  uint32_t lock_cnt_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRecursiveMutex);
};

inline ObRecursiveMutex::ObRecursiveMutex(const uint32_t latch_id)
  : latch_(), latch_id_(latch_id), lock_cnt_(0)
{
}

inline ObRecursiveMutex::~ObRecursiveMutex()
{
}

inline int ObRecursiveMutex::lock()
{
  int ret = OB_SUCCESS;
  if (latch_.is_wrlocked_by()) {
    ++lock_cnt_;
  } else {
    if (OB_FAIL(latch_.wrlock(latch_id_))) {
      COMMON_LOG(WARN, "Fail to lock ObRecursiveMutex, ", K_(latch_id), K(ret));
    } else {
      ++lock_cnt_;
    }
  }
  return ret;
}

inline int ObRecursiveMutex::unlock()
{
  int ret = OB_SUCCESS;
  if (0 == --lock_cnt_) {
    if (OB_FAIL(latch_.unlock())) {
      COMMON_LOG(WARN, "Fail to unlock the ObRecursiveMutex, ", K_(latch_id), K(ret));
    }
  }
  return ret;
}

inline int ObRecursiveMutex::trylock()
{
  int ret = OB_SUCCESS;
  if (latch_.is_wrlocked_by()) {
    ++lock_cnt_;
  } else {
    if (OB_FAIL(latch_.try_wrlock(latch_id_))) {
      if (OB_UNLIKELY(OB_EAGAIN != ret)) {
        COMMON_LOG(WARN, "Fail to try lock ObRecursiveMutex, ", K_(latch_id), K(ret));
      }
    } else {
      ++lock_cnt_;
    }
  }
  return ret;
}

typedef lib::ObLockGuard<ObRecursiveMutex> ObRecursiveMutexGuard;
}
}
#endif
