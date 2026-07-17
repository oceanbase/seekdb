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

#ifndef OCEANBASE_SHARE_LOCK_QSYNC_LOCK_H
#define OCEANBASE_SHARE_LOCK_QSYNC_LOCK_H

#include "lib/allocator/ob_qsync.h"

namespace oceanbase
{
namespace common
{

class ObQSyncLock
{
public:
  ObQSyncLock() : write_flag_(0) {}
  ~ObQSyncLock() {}
  int init(const lib::ObMemAttr &mem_attr);
  bool is_inited() const { return true; }
  void destroy();
  int rdlock();
  void rdunlock();
  int wrlock();
  void wrunlock();
  int try_wrlock();
  int try_rdlock();
private:
  static const int64_t TRY_SYNC_COUNT = 16;
  static const int64_t MAX_REF_CNT = 48;
private:
  int64_t write_flag_ CACHE_ALIGNED;
  common::ObQSyncWrapper<MAX_REF_CNT> qsync_;
};

class ObQSyncLockWriteGuard
{
public:
  ObQSyncLockWriteGuard(ObQSyncLock &lock) : lock_(lock) {
    lock_.wrlock();
  }
  ~ObQSyncLockWriteGuard() {
    lock_.wrunlock();
  }
private:
  ObQSyncLock &lock_;
};

class ObQSyncLockReadGuard
{
public:
  ObQSyncLockReadGuard(ObQSyncLock &lock) : lock_(lock) {
    lock_.rdlock();
  }
  ~ObQSyncLockReadGuard() {
    lock_.rdunlock();
  }
private:
  ObQSyncLock &lock_;
};


}
}

#endif
