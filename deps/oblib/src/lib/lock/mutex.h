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

#ifndef MUTEX_H
#define MUTEX_H

#include <pthread.h>
#include "lib/lock/ob_lock.h"

namespace obutil
{

/**
 * A wrapper class of pthread mutex.
 * This class is intended to be used in other low level construct only.
 * In most situations, you should use ObLatch or ObMutex.
 *
 */
class ObUtilMutex
{
public:

  typedef ObLockT<ObUtilMutex> Lock;
  typedef ObTryLockT<ObUtilMutex> TryLock;

  ObUtilMutex();
  ~ObUtilMutex();

  void lock() const;
  bool trylock() const;
  void unlock() const;
  bool will_unlock() const;

private:

  ObUtilMutex(const ObUtilMutex&);
  ObUtilMutex& operator=(const ObUtilMutex&);

  struct LockState
  {
    pthread_mutex_t* mutex;
  };

  void unlock(LockState&) const;
  void lock(LockState&) const;
  mutable pthread_mutex_t _mutex;

  friend class Cond;
};
typedef ObUtilMutex Mutex;
}//end namespace
#endif
