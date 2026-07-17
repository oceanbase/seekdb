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

#ifndef ENABLE_SANITY
#else
#include "ob_futex_v2.h"

inline int futex_v2_wake(volatile int *p, int val)
{
    return static_cast<int>(futex((uint *)p, FUTEX_WAKE_PRIVATE, val, NULL));
}

inline int futex_v2_wait(volatile int *p, int val, const timespec *timeout)
{
  int ret = 0;
  if (0 != futex((uint *)p, FUTEX_WAIT_PRIVATE, val, timeout)) {
    ret = errno;
  }
  return ret;
}

static struct timespec make_timespec(int64_t us)
{
  timespec ts;
  ts.tv_sec = us / 1000000;
  ts.tv_nsec = 1000 * (us % 1000000);
  return ts;
}

using namespace oceanbase::lib;
using namespace oceanbase::common;

namespace oceanbase {
namespace lib {

int ObFutexV2::wait(int v, int64_t timeout)
{
  int ret = OB_SUCCESS;
  const auto ts = make_timespec(timeout);
  ATOMIC_INC(&sys_waiters_);
  int eret = futex_v2_wait(&v_, v, &ts);
  if (OB_UNLIKELY(eret != 0)) {
    if (OB_UNLIKELY(eret == ETIMEDOUT)) {
      // only return timeout error code, others treat as success.
      ret = OB_TIMEOUT;
    }
  }
  ATOMIC_DEC(&sys_waiters_);
  return ret;
}

int ObFutexV2::wake(int64_t n)
{
  int cnt = 0;
  if (n >= INT32_MAX) {
    cnt = futex_v2_wake(&v_, INT32_MAX);
  } else {
    cnt = futex_v2_wake(&v_, static_cast<int32_t>(n));
  }
  return cnt;
}

}  // lib
}  // oceanbase
#endif
