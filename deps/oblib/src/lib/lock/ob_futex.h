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

#ifndef OB_FUTEX_H
#define OB_FUTEX_H

#include <atomic>
#include "lib/ob_errno.h"
#include "lib/list/ob_dlist.h"
#include <linux/futex.h>
#include "lib/ob_abort.h"

extern "C" {
extern int futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout);
}

#define futex(...) futex_hook(__VA_ARGS__)

inline int futex_wake(volatile int *p, int val)
{
  return futex((uint32_t *)p, FUTEX_WAKE_PRIVATE, val, NULL);
}

inline int futex_wait(volatile int *p, int val, const timespec *timeout)
{
  int ret = 0;
  if (0 != futex((uint32_t *)p, FUTEX_WAIT_PRIVATE, val, timeout)) {
    ret = errno;
  }
  return ret;
}

namespace oceanbase {
namespace lib {
class ObFutex
{
public:
  ObFutex()
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


#endif /* OB_FUTEX_H */
