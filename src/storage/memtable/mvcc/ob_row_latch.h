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

#ifndef OCEANBASE_MVCC_OB_ROW_LATCH_
#define OCEANBASE_MVCC_OB_ROW_LATCH_
#include "lib/lock/ob_latch.h"
#include "lib/stat/ob_latch_define.h"

namespace oceanbase
{
namespace memtable
{
#define USE_SIMPLE_ROW_LATCH 1
#if USE_SIMPLE_ROW_LATCH
struct ObRowLatch
{
  ObRowLatch(): locked_(false) {}
  ~ObRowLatch() {}
  struct Guard
  {
    Guard(ObRowLatch& host): host_(host) { host.lock();}
    ~Guard() { host_.unlock(); }
    ObRowLatch& host_;
  };
  bool is_locked() const { return ATOMIC_LOAD(&locked_); }
  bool try_lock() { return !ATOMIC_TAS(&locked_, true); }
  void lock() {
    while(!try_lock())
      ;
  }
  void unlock() { ATOMIC_STORE(&locked_, false); }
  bool locked_;
};
#else
struct ObRowLatch
{
ObRowLatch(): latch_() {}
~ObRowLatch() {}
  struct Guard
  {
  Guard(ObRowLatch& host): host_(host) { host.lock();}
   ~Guard() { host_.unlock(); }
    ObRowLatch& host_;
  };
  bool is_locked() const { return latch_.is_locked(); }
  bool try_lock()
  {
    // try_wrlock succeeds and returns OB_SUCCESS;
    return (common::OB_SUCCESS == latch_.try_wrlock(common::ObLatchIds::ROW_CALLBACK_LOCK));
  }
  void lock() { (void)latch_.wrlock(common::ObLatchIds::ROW_CALLBACK_LOCK); }
  void unlock() { (void)latch_.unlock(); }
  common::ObLatch latch_;
};
#endif
typedef ObRowLatch::Guard ObRowLatchGuard;
}; // end namespace mvcc
}; // end namespace oceanbase

#endif /* OCEANBASE_MVCC_OB_ROW_LATCH_ */
