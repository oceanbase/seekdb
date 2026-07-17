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

#ifndef OCEANBASE_STORAGE_OB_LS_LOCK_
#define OCEANBASE_STORAGE_OB_LS_LOCK_

#include "lib/lock/ob_latch.h"

namespace oceanbase
{
namespace storage
{

static const int64_t LSLOCKLSSTATE      = 1L;
static const int64_t LSLOCKLOGSTATE     = 1L << 1;
static const int64_t LSLOCKTXSTATE      = 1L << 2;
static const int64_t LSLOCKSTORAGESTATE = 1L << 3;

static const int64_t LSLOCKSIZE = 4;
static const int64_t LSLOCKMASK = (1L << LSLOCKSIZE) - 1;
static const int64_t LSLOCKALL = LSLOCKMASK;

class ObLSLockGuard;
class ObLSTryLockGuard;
class ObLS;

class ObLSLock
{
  friend ObLSLockGuard;
  friend ObLSTryLockGuard;
  static const int64_t LOCK_CONFLICT_WARN_TIME = 100 * 1000; // 100 ms
public:
  typedef common::ObLatch RWLock;

  ObLSLock();
  ~ObLSLock();

  ObLSLock(const ObLSLock&) = delete;
  ObLSLock& operator=(const ObLSLock&) = delete;
private:
  int64_t lock(const ObLS *ls, int64_t hold, int64_t change, const int64_t abs_timeout_us = INT64_MAX);
  int64_t try_lock(const ObLS *ls, int64_t hold, int64_t change);
  void unlock(int64_t target);

  RWLock locks_[LSLOCKSIZE];
};

class ObLSLockGuard
{
public:
  ObLSLockGuard(ObLS *ls,
                ObLSLock &lock,
                int64_t hold,
                int64_t change,
                const bool trylock = false);
  ObLSLockGuard(ObLS *ls,
                ObLSLock &lock,
                int64_t hold,
                int64_t change,
                const int64_t abs_timeout_us);
  // lock all by default.
  // WARNING: make sure ls is not null.
  ObLSLockGuard(ObLS *ls, const bool rdlock = false);
  ~ObLSLockGuard();

  bool locked() const { return mark_ != 0; }

  ObLSLockGuard(const ObLSLockGuard&) = delete;
  ObLSLockGuard& operator=(const ObLSLockGuard&) = delete;
private:
  ObLSLock &lock_;
  int64_t mark_;
  int64_t start_ts_;
  const ObLS *ls_;
};

class ObLSStateGuard
{
public:
  ObLSStateGuard(ObLS *ls);
  ~ObLSStateGuard();
private:
  ObLS *ls_;
  int64_t begin_state_seq_;
};

} // storage
} // oceanbase

#endif /* OCEANBASE_STORAGE_OB_LS_LOCK_ */
