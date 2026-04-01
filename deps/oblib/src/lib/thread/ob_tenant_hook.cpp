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
 
#ifndef _OCEABASE_TENANT_PRELOAD_H_
#define _OCEABASE_TENANT_PRELOAD_H_

#define _GNU_SOURCE 1
#include "lib/thread/ob_thread_name.h"
#include "lib/stat/ob_diagnose_info.h"
#include "lib/ash/ob_active_session_guard.h"
#ifdef _WIN32
#include <sys/timeb.h>
#else
#include <dlfcn.h>
#include <poll.h>
#endif
#ifdef __linux__
#include <sys/epoll.h>
#endif

namespace oceanbase {
namespace omt {
thread_local int in_sys_hook = 0;
}
}

#ifdef _WIN32
template<typename Func, typename... Args>
static inline int sys_hook_impl(Func real_func, Args... args) {
  int ret = 0;
  if (!oceanbase::omt::in_sys_hook++) {
    oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT);
    ret = real_func(args...);
  } else {
    ret = real_func(args...);
  }
  oceanbase::omt::in_sys_hook--;
  return ret;
}
#define SYS_HOOK(func_name, ...) sys_hook_impl(real_##func_name, __VA_ARGS__)
#else
#define SYS_HOOK(func_name, ...)                                             \
  ({                                                                         \
    int ret = 0;                                                             \
    if (!in_sys_hook++) {                                                    \
      oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT); \
      ret = real_##func_name(__VA_ARGS__);                                   \
    } else {                                                                 \
      ret = real_##func_name(__VA_ARGS__);                                   \
    }                                                                        \
    in_sys_hook--;                                                           \
    ret;                                                                     \
  })
#endif

using namespace oceanbase;
using namespace omt;

extern "C" {




#ifdef __USE_XOPEN2K
#endif



#ifdef __USE_XOPEN2K
#endif

#ifdef __linux__
int ob_epoll_wait(int __epfd, struct epoll_event *__events,
		              int __maxevents, int __timeout)
{
  static int (*real_epoll_wait)(
      int __epfd, struct epoll_event *__events,
		  int __maxevents, int __timeout) = epoll_wait;
  int ret = 0;
  oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT_FOR_IO_EVENT);
  oceanbase::common::ObBKGDSessInActiveGuard inactive_guard;
  ret = SYS_HOOK(epoll_wait, __epfd, __events, __maxevents, __timeout);
  return ret;
}
#elif defined(_WIN32)
struct epoll_event {
  uint32_t events;
  union {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
  } data;
};
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010

extern "C" int ob_win32_epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout);

int ob_epoll_wait(int __epfd, struct epoll_event *__events,
                  int __maxevents, int __timeout)
{
  return ob_win32_epoll_wait_impl(__epfd, __events, __maxevents, __timeout);
}
#elif defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>

// macOS doesn't have epoll, but we need to define basic types for compilation
// Note: This file may not work correctly on macOS without epoll support
struct epoll_event {
  uint32_t events;
  union {
    void *ptr;
    int fd;
    uint32_t u32;
    uint64_t u64;
  } data;
};
#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010

int ob_epoll_wait(int __epfd, struct epoll_event *__events,
		              int __maxevents, int __timeout)
{
  int ret = 0;
  struct timespec timeout_ts;
  struct kevent kev[__maxevents];
  int num_events = 0;

  if (__timeout >= 0) {
    timeout_ts.tv_sec = __timeout / 1000;
    timeout_ts.tv_nsec = (__timeout % 1000) * 1000000;
  }

  // kqueue doesn't have a direct equivalent of epoll_ctl for adding/modifying events
  // The events are managed by the kqueue instance itself.
  // Here we just wait for events.
  num_events = kevent(__epfd, NULL, 0, kev, __maxevents, (__timeout == -1) ? NULL : &timeout_ts);

  if (num_events < 0) {
    ret = -1; // Error
  } else {
    for (int i = 0; i < num_events; ++i) {
      __events[i].events = 0;
      if (kev[i].filter == EVFILT_READ) __events[i].events |= EPOLLIN;
      if (kev[i].filter == EVFILT_WRITE) __events[i].events |= EPOLLOUT;
      if (kev[i].flags & EV_EOF) __events[i].events |= EPOLLHUP; // EPOLLHUP
      if (kev[i].flags & EV_ERROR) __events[i].events |= EPOLLERR; // EPOLLERR
      __events[i].data.ptr = kev[i].udata;
    }
    ret = num_events;
  }
  return ret;
}
#endif


int ob_pthread_cond_wait(pthread_cond_t *__restrict __cond,
                         pthread_mutex_t *__restrict __mutex) 
{
  static int (*real_pthread_cond_wait)(pthread_cond_t *__restrict __cond,
      pthread_mutex_t *__restrict __mutex) = pthread_cond_wait;
  int ret = 0;
  ret = SYS_HOOK(pthread_cond_wait, __cond, __mutex);
  return ret;
}

int ob_pthread_cond_timedwait(pthread_cond_t *__restrict __cond,
                              pthread_mutex_t *__restrict __mutex,
                              const struct timespec *__restrict __abstime) 
{
  static int (*real_pthread_cond_timedwait)(
      pthread_cond_t *__restrict __cond, pthread_mutex_t *__restrict __mutex,
      const struct timespec *__restrict __abstime) = pthread_cond_timedwait;
  int ret = 0;
  ret = SYS_HOOK(pthread_cond_timedwait, __cond, __mutex, __abstime);
  return ret;
}

int ob_pthread_cond_timedwait_us(pthread_cond_t *__restrict __cond,
                                 pthread_mutex_t *__restrict __mutex,
                                 int64_t timeout_us,
                                 bool use_monotonic)
{
  int ret = 0;
#ifdef __APPLE__
  // On macOS, use pthread_cond_timedwait_relative_np with relative timeout.
  // This avoids clock drift issues because:
  // 1. ObTimeUtility::current_time() uses mach_absolute_time() with a fixed base from startup
  // 2. pthread_cond_timedwait internally uses gettimeofday/system clock (adjusted by NTP)
  // 3. Over time these clocks drift apart, causing pthread_cond_timedwait to return immediately
  //    (thinking timeout already passed), leading to busy-wait and CPU exhaustion
  // pthread_cond_timedwait_relative_np takes a relative timeout, avoiding this issue entirely.
  (void)use_monotonic; // Not needed on macOS - relative timeout works with any clock
  struct timespec reltime;
  reltime.tv_sec = static_cast<time_t>(timeout_us / 1000000);
  reltime.tv_nsec = static_cast<long>((timeout_us % 1000000) * 1000);
  ret = pthread_cond_timedwait_relative_np(__cond, __mutex, &reltime);
#elif defined(_WIN32)
  (void)use_monotonic;
  struct timespec abstime;
  struct _timeb tb;
  _ftime_s(&tb);
  abstime.tv_sec = (time_t)tb.time;
  abstime.tv_nsec = tb.millitm * 1000000L;
  abstime.tv_sec += static_cast<time_t>(timeout_us / 1000000);
  abstime.tv_nsec += static_cast<long>((timeout_us % 1000000) * 1000);
  if (abstime.tv_nsec >= 1000000000L) {
    abstime.tv_sec += 1;
    abstime.tv_nsec -= 1000000000L;
  }
  ret = ob_pthread_cond_timedwait(__cond, __mutex, &abstime);
#else
  // On Linux, compute absolute time using the specified clock source.
  // use_monotonic=true: for pthread_cond configured with pthread_condattr_setclock(CLOCK_MONOTONIC)
  // use_monotonic=false: for pthread_cond initialized with default attributes (CLOCK_REALTIME)
  struct timespec abstime;
  clock_gettime(use_monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME, &abstime);
  abstime.tv_sec += static_cast<time_t>(timeout_us / 1000000);
  abstime.tv_nsec += static_cast<long>((timeout_us % 1000000) * 1000);
  if (abstime.tv_nsec >= 1000000000L) {
    abstime.tv_sec += 1;
    abstime.tv_nsec -= 1000000000L;
  }
  ret = ob_pthread_cond_timedwait(__cond, __mutex, &abstime);
#endif
  return ret;
}

// ob_usleep wrapper function for C file


#ifdef __linux__
int futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout)
{
  static long int (*real_syscall)(long int __sysno, ...) = syscall;
  int ret = 0;
  if (futex_op == FUTEX_WAIT_PRIVATE) {
    ret = (int)SYS_HOOK(syscall, SYS_futex, uaddr, futex_op, val, timeout, nullptr, 0u);
  } else {
    ret = (int)real_syscall(SYS_futex, uaddr, futex_op, val, timeout, nullptr, 0u);
  }
  return ret;
}
#elif defined(_WIN32)
#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_WAIT_PRIVATE  128
#define FUTEX_WAKE_PRIVATE  129

int futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout)
{
  int ret = 0;
  int base_op = futex_op & 0x7F;

  if (base_op == FUTEX_WAIT) {
    DWORD timeout_ms = INFINITE;
    if (timeout != nullptr) {
      timeout_ms = (DWORD)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    }
    oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT);
    if (!WaitOnAddress(uaddr, &val, sizeof(uint32_t), timeout_ms)) {
      DWORD err = GetLastError();
      if (err == ERROR_TIMEOUT) {
        errno = ETIMEDOUT;
        return -1;
      }
      return -1;
    }
    return 0;
  } else if (base_op == FUTEX_WAKE) {
    if (val >= INT32_MAX) {
      WakeByAddressAll(uaddr);
    } else {
      for (uint32_t i = 0; i < val; i++) {
        WakeByAddressSingle(uaddr);
      }
    }
    return 0;
  }
  return 0;
}
#elif defined(__APPLE__)
// macOS futex emulation using Darwin's ulock syscalls
extern "C" {
int __ulock_wait(uint32_t operation, void *addr, uint64_t value, uint32_t timeout_us);
int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);
}

#define UL_COMPARE_AND_WAIT 1
#define ULF_WAKE_ALL        0x00000100

// Define futex operation constants for macOS
#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_WAIT_PRIVATE  128
#define FUTEX_WAKE_PRIVATE  129

int futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout)
{
  int ret = 0;
  int base_op = futex_op & 0x7F;  // Strip FUTEX_PRIVATE_FLAG

  if (base_op == FUTEX_WAIT) {
    // FUTEX_WAIT operation
    uint32_t timeout_us = 0;
    if (timeout != nullptr) {
      timeout_us = (uint32_t)(timeout->tv_sec * 1000000 + timeout->tv_nsec / 1000);
    }

    // Check if value has changed before waiting
    if (*uaddr != val) {
      errno = EAGAIN;
      return -1;
    }

    // Add WaitGuard for wait operations (same as Linux)
    oceanbase::lib::Thread::WaitGuard guard(oceanbase::lib::Thread::WAIT);
    ret = __ulock_wait(UL_COMPARE_AND_WAIT, (void*)uaddr, (uint64_t)val, timeout_us);

    if (ret < 0) {
      // Match ob_futex.cpp behavior: EAGAIN/EINTR are not fatal errors
      if (errno == EAGAIN || errno == EINTR) {
        return 0;
      }
      return -1;
    }
    return 0;
  } else if (base_op == FUTEX_WAKE) {
    // FUTEX_WAKE operation
    if (val >= INT32_MAX) {
      return __ulock_wake(UL_COMPARE_AND_WAIT | ULF_WAKE_ALL, (void*)uaddr, 0);
    }
    int woken = 0;
    for (uint32_t i = 0; i < val; i++) {
      int wake_ret = __ulock_wake(UL_COMPARE_AND_WAIT, (void*)uaddr, 0);
      if (wake_ret >= 0) {
        woken++;
      } else {
        break;
      }
    }
    return woken;
  }

  return 0;
}
#endif



} /* extern "C" */

#endif /* _OCEABASE_TENANT_PRELOAD_H_ */
