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

#include "ob_futex.h"
#ifdef __linux__
#include <linux/futex.h>
#include <unistd.h>
#include <sys/syscall.h>
#endif

#ifdef __APPLE__
#include <pthread.h>
#include <errno.h>
#include <sys/time.h>
#include <unordered_map>
#include <mutex>
#include <condition_variable>

// macOS futex emulation using pthread condition variables
// We use ulock syscalls available in Darwin for a more efficient implementation
extern "C" {
int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);
int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);
}

// ulock operation flags
#define UL_COMPARE_AND_WAIT 1
#define ULF_WAKE_ALL        0x00000100

int futex_wake(volatile int *p, int val)
{
  // Use Darwin's __ulock_wake for efficient wake
  // If val is large enough to wake all, use ULF_WAKE_ALL flag
  if (val >= INT32_MAX) {
    return __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL, (void*)p, 0);
  }
  // Otherwise wake one at a time
  int woken = 0;
  for (int i = 0; i < val; i++) {
    int ret = __ulock_wake(UL_COMPARE_AND_WAIT, (void*)p, 0);
    if (ret >= 0) {
      woken++;
    } else {
      break;
    }
  }
  return woken;
}

int futex_wait(volatile int *p, int val, const timespec *timeout)
{
  uint32_t timeout_us = 0;
  if (timeout != nullptr) {
    timeout_us = (uint32_t)(timeout->tv_sec * 1000000 + timeout->tv_nsec / 1000);
  } else {
    timeout_us = 0; // infinite wait
  }

  // Check if value has changed before waiting
  if (*p != val) {
    return EAGAIN;
  }

  int ret = __ulock_wait(UL_COMPARE_AND_WAIT, (void*)p, (uint64_t)val, timeout_us);
  if (ret < 0) {
    if (errno == ETIMEDOUT) {
      return ETIMEDOUT;
    } else if (errno == EAGAIN || errno == EINTR) {
      // Value changed or interrupted - not an error
      return 0;
    }
    return errno;
  }
  return 0;
}
#endif

static struct timespec make_timespec(int64_t us)
{
  timespec ts;
  ts.tv_sec = us / 1000000;
  ts.tv_nsec = 1000 * (us % 1000000);
  return ts;
}

extern "C" {
#ifdef __linux__
int __attribute__((weak)) futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout)
{
  return syscall(SYS_futex, uaddr, futex_op, val, timeout);
}
#elif defined(__APPLE__)
// macOS: futex_hook is not used directly, we use __ulock_* syscalls instead
// Keep this stub for compatibility
int __attribute__((weak)) futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout)
{
  (void)uaddr;
  (void)futex_op;
  (void)val;
  (void)timeout;
  return 0;
}
#endif
}

using namespace oceanbase::lib;
using namespace oceanbase::common;

namespace oceanbase {
namespace lib {

int ObFutex::wait(int v, int64_t timeout)
{
  int ret = OB_SUCCESS;
  const auto ts = make_timespec(timeout);
  ATOMIC_INC(&sys_waiters_);
  int eret = futex_wait(&v_, v, &ts);
  if (OB_UNLIKELY(eret != 0)) {
    if (OB_UNLIKELY(eret == ETIMEDOUT)) {
      // only return timeout error code, others treat as success.
      ret = OB_TIMEOUT;
    }
  }
  ATOMIC_DEC(&sys_waiters_);
  return ret;
}

int ObFutex::wake(int64_t n)
{
  int cnt = 0;
  if (n >= INT32_MAX) {
    cnt = futex_wake(&v_, INT32_MAX);
  } else {
    cnt = futex_wake(&v_, static_cast<int32_t>(n));
  }
  return cnt;
}

}  // lib
}  // oceanbase
