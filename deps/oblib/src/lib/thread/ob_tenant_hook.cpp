/**
 * Copyright (c) 2023 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
