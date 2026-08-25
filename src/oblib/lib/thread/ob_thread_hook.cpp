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

#ifndef OCEANBASE_LIB_THREAD_OB_THREAD_HOOK_H_
#define OCEANBASE_LIB_THREAD_OB_THREAD_HOOK_H_

#define _GNU_SOURCE 1
#include "lib/thread/ob_thread_name.h"
#ifdef _WIN32
#include <sys/timeb.h>
#else
#include <dlfcn.h>
#include <poll.h>
#endif
#ifdef __linux__
#include <sys/epoll.h>
#include <time.h>
#include <pthread.h>
#endif

extern "C" {

#ifdef __linux__
int ob_epoll_wait(int __epfd, struct epoll_event *__events,
                  int __maxevents, int __timeout)
{
  return epoll_wait(__epfd, __events, __maxevents, __timeout);
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

// Windows does not expose a native epoll backend here, but the shared wrapper
// still needs a definition so the Windows build links cleanly.
int ob_win32_epoll_wait_impl(int epfd, struct epoll_event *events, int maxevents, int timeout)
{
  (void)epfd;
  (void)events;
  (void)maxevents;
  (void)timeout;
  return -1;
}

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

  num_events = kevent(__epfd, NULL, 0, kev, __maxevents, (__timeout == -1) ? NULL : &timeout_ts);

  if (num_events < 0) {
    ret = -1;
  } else {
    for (int i = 0; i < num_events; ++i) {
      __events[i].events = 0;
      if (kev[i].filter == EVFILT_READ) __events[i].events |= EPOLLIN;
      if (kev[i].filter == EVFILT_WRITE) __events[i].events |= EPOLLOUT;
      if (kev[i].flags & EV_EOF) __events[i].events |= EPOLLHUP;
      if (kev[i].flags & EV_ERROR) __events[i].events |= EPOLLERR;
      __events[i].data.ptr = kev[i].udata;
    }
    ret = num_events;
  }
  return ret;
}
#endif

int ob_pthread_cond_timedwait_us(pthread_cond_t *__restrict __cond,
                                 pthread_mutex_t *__restrict __mutex,
                                 int64_t timeout_us,
                                 bool use_monotonic)
{
  int ret = 0;
#ifdef __APPLE__
  (void)use_monotonic;
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
  ret = pthread_cond_timedwait(__cond, __mutex, &abstime);
#else
  struct timespec abstime;
  clock_gettime(use_monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME, &abstime);
  abstime.tv_sec += static_cast<time_t>(timeout_us / 1000000);
  abstime.tv_nsec += static_cast<long>((timeout_us % 1000000) * 1000);
  if (abstime.tv_nsec >= 1000000000L) {
    abstime.tv_sec += 1;
    abstime.tv_nsec -= 1000000000L;
  }
  ret = pthread_cond_timedwait(__cond, __mutex, &abstime);
#endif
  return ret;
}

#if defined(_WIN32)
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

#define FUTEX_WAIT          0
#define FUTEX_WAKE          1
#define FUTEX_WAIT_PRIVATE  128
#define FUTEX_WAKE_PRIVATE  129

int futex_hook(uint32_t *uaddr, int futex_op, uint32_t val, const struct timespec* timeout)
{
  int ret = 0;
  int base_op = futex_op & 0x7F;

  if (base_op == FUTEX_WAIT) {
    uint32_t timeout_us = 0;
    if (timeout != nullptr) {
      timeout_us = (uint32_t)(timeout->tv_sec * 1000000 + timeout->tv_nsec / 1000);
    }

    if (*uaddr != val) {
      errno = EAGAIN;
      return -1;
    }

    ret = __ulock_wait(UL_COMPARE_AND_WAIT, (void*)uaddr, (uint64_t)val, timeout_us);

    if (ret < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        return 0;
      }
      return -1;
    }
    return 0;
  } else if (base_op == FUTEX_WAKE) {
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

#endif // OCEANBASE_LIB_THREAD_OB_THREAD_HOOK_H_
