/**
 * Copyright (c) 2024 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the
 * Mulan PubL v2. You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef OCEANBASE_LIB_NET_OB_EPOLL_COMPAT_H_
#define OCEANBASE_LIB_NET_OB_EPOLL_COMPAT_H_

// Cross-platform thin epoll surface used by ob_sql_nio.cpp.
//
// Linux: forwards to <sys/epoll.h>.
// Windows + OB_USE_WEPOLL: forwards to wepoll (AFD/IOCP-backed real epoll).
// Windows without OB_USE_WEPOLL: this header MUST NOT be included; callers
// fall back to the legacy WSAPoll-based shim defined inline in ob_sql_nio.cpp.
//
// Why a side table on Windows:
//  - wepoll's HANDLE is wepoll's own port_state_t* (heap pointer), 8 bytes on
//    x64. OB stores epfd as `int epfd_` in many places. We map int<->HANDLE
//    through a small registry so callers do not change.

#if defined(_WIN32) && defined(OB_USE_WEPOLL)

#include <stdint.h>
#include <mutex>
#include <unordered_map>
#include "wepoll.h"

// wepoll already provides: struct epoll_event, epoll_data_t, EPOLLIN,
// EPOLLPRI, EPOLLOUT, EPOLLERR, EPOLLHUP, EPOLLRDHUP, EPOLLONESHOT,
// EPOLL_CTL_ADD/DEL/MOD. It does NOT define:
//
//   - EPOLLET: wepoll only supports level-triggered mode. Callers that pass
//     EPOLLET expect edge-triggered, but they'd already silently get LT under
//     the legacy WSAPoll shim. We define EPOLLET as 0 so call sites compile
//     and behave identically to the WSAPoll path. If true ET semantics are
//     ever required on Windows, that needs a separate effort.
//   - EPOLL_CLOEXEC: irrelevant on Windows (handles do not inherit by default).

#ifndef EPOLLET
#define EPOLLET 0
#endif
#ifndef EPOLL_CLOEXEC
#define EPOLL_CLOEXEC 0
#endif

namespace oceanbase {
namespace lib {

class ObWepollRegistry
{
public:
  static ObWepollRegistry &instance()
  {
    static ObWepollRegistry inst;
    return inst;
  }
  int add(HANDLE h)
  {
    std::lock_guard<std::mutex> lg(mu_);
    int id = next_id_++;
    map_[id] = h;
    return id;
  }
  HANDLE get(int id) const
  {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = map_.find(id);
    return it == map_.end() ? NULL : it->second;
  }
  HANDLE pop(int id)
  {
    std::lock_guard<std::mutex> lg(mu_);
    auto it = map_.find(id);
    if (it == map_.end()) return NULL;
    HANDLE h = it->second;
    map_.erase(it);
    return h;
  }
private:
  ObWepollRegistry() : next_id_(0x60000001) {}
  mutable std::mutex mu_;
  std::unordered_map<int, HANDLE> map_;
  int next_id_;
};

} // namespace lib
} // namespace oceanbase

static inline int ob_wepoll_create1(int flags)
{
  HANDLE h = epoll_create1(flags);
  if (h == NULL) return -1;
  return ::oceanbase::lib::ObWepollRegistry::instance().add(h);
}

static inline int ob_wepoll_ctl(int epfd, int op, int fd, struct epoll_event *ev)
{
  HANDLE h = ::oceanbase::lib::ObWepollRegistry::instance().get(epfd);
  if (h == NULL) return -1;
  return epoll_ctl(h, op, (SOCKET)fd, ev);
}

static inline int ob_wepoll_wait(int epfd, struct epoll_event *events,
                                 int maxevents, int timeout)
{
  HANDLE h = ::oceanbase::lib::ObWepollRegistry::instance().get(epfd);
  if (h == NULL) return -1;
  return epoll_wait(h, events, maxevents, timeout);
}

static inline int ob_wepoll_close(int epfd)
{
  HANDLE h = ::oceanbase::lib::ObWepollRegistry::instance().pop(epfd);
  if (h == NULL) return -1;
  return epoll_close(h);
}

// Override the libc names within this translation unit's Windows branch so
// existing call sites compile unchanged. epoll_close is intentionally NOT
// macro-redefined: closesocket()-style cleanup is done via ob_wepoll_close
// from explicit teardown paths.
#define epoll_create1 ob_wepoll_create1
#define epoll_ctl     ob_wepoll_ctl
#define epoll_wait    ob_wepoll_wait

#elif !defined(_WIN32)

#include <sys/epoll.h>

#endif  // _WIN32 + OB_USE_WEPOLL

#endif  // OCEANBASE_LIB_NET_OB_EPOLL_COMPAT_H_
