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

#include "ob_stat_template.h"

namespace oceanbase
{
namespace common
{

DIRWLock::DIRWLock()
#ifdef NDEBUG
  : lock_(0)
#else
  : lock_(0),
    wlock_tid_(0)
#endif
{
}

DIRWLock::~DIRWLock()
{
  if (OB_UNLIKELY(lock_ != 0)) {
#ifdef NDEBUG
    COMMON_LOG_RET(WARN, OB_ERR_UNEXPECTED, "DIRWLock exit with lock", K_(lock));
#else
    COMMON_LOG_RET(WARN, OB_ERR_UNEXPECTED, "DIRWLock exit with lock", K_(lock), K_(wlock_tid));
#endif
  }
}

int DIRWLock::try_rdlock()
{
  int ret = OB_EAGAIN;
  uint32_t lock = ATOMIC_LOAD(&lock_);
  if (0 == (lock & WRITE_MASK)) {
    if (ATOMIC_BCAS(&lock_, lock, lock + 1)) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}





void DIRWLock::unlock()
{
  uint32_t lock = 0;
  lock = ATOMIC_LOAD(&lock_);
  if (0 != (lock & WRITE_MASK)) {
#ifndef NDEBUG
    if (OB_UNLIKELY(wlock_tid_ != GETTID())) {
      COMMON_LOG_RET(WARN, OB_ERR_UNEXPECTED, "wlock mismatch", K(GETTID()), K_(wlock_tid));
    }
    wlock_tid_ = 0;
#endif
    ATOMIC_STORE(&lock_, 0);
  } else {
    ATOMIC_AAF(&lock_, -1);
  }
#ifdef ENABLE_DEBUG_LOG
  if (ATOMIC_LOAD(&lock_) > WRITE_MASK) {
    abort();
  }
#endif
}

}
}
