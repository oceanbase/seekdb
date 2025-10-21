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

#include "ob_rwlock.h"
#include "lib/allocator/ob_malloc.h"

using namespace oceanbase;
using namespace obsys;

int ObRLock::lock() const
{
  return pthread_rwlock_rdlock(rlock_);
}

int ObRLock::trylock() const
{
  return pthread_rwlock_tryrdlock(rlock_);
}

int ObRLock::unlock() const
{
  return pthread_rwlock_unlock(rlock_);
}

int ObWLock::lock() const
{
  return pthread_rwlock_wrlock(wlock_);
}

int ObWLock::trylock() const
{
  return pthread_rwlock_trywrlock(wlock_);
}

int ObWLock::unlock() const
{
  return pthread_rwlock_unlock(wlock_);
}

ObRWLock::ObRWLock(LockMode lockMode)
  : rlock_(&rwlock_),
    wlock_(&rwlock_)
{
  pthread_rwlockattr_t attr;
  pthread_rwlockattr_init(&attr);
  if (lockMode == READ_PRIORITY) {
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_READER_NP);
  } else if (lockMode == WRITE_PRIORITY) {
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
  }
  pthread_rwlock_init(&rwlock_, &attr);
}

ObRWLock::~ObRWLock()
{
  pthread_rwlock_destroy(&rwlock_);
}
