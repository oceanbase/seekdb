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
#include <dlfcn.h>
#include <poll.h>
#include <sys/epoll.h>

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

namespace oceanbase {
namespace omt {
thread_local int in_sys_hook = 0;
}
}

using namespace oceanbase;
using namespace omt;

extern "C" {




#ifdef __USE_XOPEN2K
#endif



#ifdef __USE_XOPEN2K
#endif

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

// ob_usleep wrapper function for C file


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



} /* extern "C" */

#endif /* _OCEABASE_TENANT_PRELOAD_H_ */
