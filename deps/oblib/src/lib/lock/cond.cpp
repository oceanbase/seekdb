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

#include "lib/lock/cond.h"
#include "lib/ob_abort.h"

using namespace oceanbase::common;

namespace obutil
{
Cond::Cond()
{

    int rt = pthread_condattr_init(&_attr);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to init cond attr, err=%d", rt);
#ifdef _WIN32
      // On Windows (pthreads4w), pthread_cond_t is an opaque pointer. If init
      // fails (e.g. kernel object exhaustion, errno=ENOSPC=28), subsequent
      // signal/broadcast/wait on the uninitialized _cond dereferences a NULL
      // internal pointer inside pthreadVC3.dll and causes a native crash.
      // Fast-fail here to produce a clear failure instead of a follow-up AV.
      ob_abort();
#endif
    }
    // Set the attribute to use CLOCK_MONOTONIC clock source
    // Note: pthread_condattr_setclock is Linux-specific, not available on macOS
    // On macOS, condition variables use the system clock by default
#ifdef __linux__
    rt = pthread_condattr_setclock(&_attr, CLOCK_MONOTONIC);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to set MONOTONIC Clock, err=%d", rt);
    }
#elif defined(__APPLE__)
    // macOS doesn't support pthread_condattr_setclock, use default system clock
    (void)rt; // Suppress unused variable warning
#endif

    rt = pthread_cond_init(&_cond, &_attr);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to init cond, err=%d", rt);
#ifdef _WIN32
      ob_abort();
#endif
    }
}

Cond::~Cond()
{
  int rt = pthread_condattr_destroy(&_attr);
  if (0 != rt) {
    _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to destroy cond attr, err=%d", rt);
  }
  rt = pthread_cond_destroy(&_cond);
  if (0 != rt) {
    _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to destroy cond, err=%d", rt);
  }
}

void Cond::signal()
{
    const int rt = pthread_cond_signal(&_cond);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to signal condition, err=%d", rt);
    }
}

void Cond::broadcast()
{
    const int rt = pthread_cond_broadcast(&_cond);
    if (0 != rt) {
      _OB_LOG_RET(WARN, OB_ERR_SYS, "Failed to broadcast condition, err=%d", rt);
    }
}
}//end namespace obutil
