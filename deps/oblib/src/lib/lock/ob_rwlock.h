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

#ifndef OB_RW_LOCK_H
#define OB_RW_LOCK_H

#include <pthread.h>

namespace oceanbase
{
namespace obsys
{
template <class T>
class ObLockGuardBase
{
public:
  [[nodiscard]] ObLockGuardBase(const T& lock, bool block = true) : lock_(lock)
  {
    acquired_ = !(block ? lock_.lock() : lock_.trylock());
  }
  ~ObLockGuardBase()
  {
    if (acquired_) {
      lock_.unlock();
    }
  }
  bool acquired() const { return acquired_; }

private:
    const T& lock_;
    mutable bool acquired_;
};

enum LockMode
{
  NO_PRIORITY,
  WRITE_PRIORITY,
  READ_PRIORITY
};

class ObRLock
{
public:
  explicit ObRLock(pthread_rwlock_t* lock) : rlock_(lock) {}
  ~ObRLock() {}
  int lock() const;
  int trylock() const;
  int unlock() const;
private:
  mutable pthread_rwlock_t* rlock_;
};

class ObWLock
{
public:
  explicit ObWLock(pthread_rwlock_t* lock) : wlock_(lock) {}
  ~ObWLock() {}
  int lock() const;
  int trylock() const;
  int unlock() const;
private:
  mutable pthread_rwlock_t* wlock_;
};

class ObRWLock
{
public:
  ObRWLock(LockMode lockMode = NO_PRIORITY);
  ~ObRWLock();
  ObRLock* rlock() const { return const_cast<ObRLock*>(&rlock_); }
  ObWLock* wlock() const { return const_cast<ObWLock*>(&wlock_); }
private:
  pthread_rwlock_t rwlock_;
  ObRLock rlock_;
  ObWLock wlock_;
};

class ObRLockGuard
{
public:
  [[nodiscard]] ObRLockGuard(const ObRWLock& rwlock, bool block = true) : guard_((*rwlock.rlock()), block) {}
  ~ObRLockGuard(){}
  bool acquired() { return guard_.acquired(); }
private:
  ObLockGuardBase<ObRLock> guard_;
};

class ObWLockGuard
{
public:
  [[nodiscard]] ObWLockGuard(const ObRWLock& rwlock, bool block = true) : guard_((*rwlock.wlock()), block) {}
  ~ObWLockGuard(){}
  bool acquired() { return guard_.acquired(); }
private:
  ObLockGuardBase<ObWLock> guard_;
};
}
}
#endif
