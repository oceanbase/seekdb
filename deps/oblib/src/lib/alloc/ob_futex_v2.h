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

#ifndef OB_FUTEX_v2_H
#define OB_FUTEX_v2_H

#ifndef ENABLE_SANITY
#else
#include <atomic>
#include "lib/ob_errno.h"
#include "lib/list/ob_dlist.h"
#include <linux/futex.h>
#include "lib/ob_abort.h"
namespace oceanbase {
namespace lib {
class ObFutexV2
{
public:
  ObFutexV2()
      : v_(),
        sys_waiters_()
  {}

  int32_t &val() { return v_; }
  uint32_t &uval() { return reinterpret_cast<uint32_t&>(v_); }

  // This function atomically verifies v_ still equals to argument v,
  // and sleeps awaiting wake call on this futex. If the timeout
  // argument is positive, it contains the maximum duration described
  // in milliseconds of the wait, which is infinite otherwise.
  int wait(int v, int64_t timeout);
  // This function wakes at most n, which is in argument list,
  // routines up waiting on the futex.
  int wake(int64_t n);

public:

private:
  int v_;
  int sys_waiters_;
} CACHE_ALIGNED;

}  // lib
}  // oceanbase

#endif /* ENABLE_SANITY */
#endif /* OB_FUTEX_v2_H */
