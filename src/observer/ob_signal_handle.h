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

#ifndef OCEANBASE_OBSERVER_OB_SIGNAL_HANDLE_
#define OCEANBASE_OBSERVER_OB_SIGNAL_HANDLE_

#ifdef _WIN32
#include <windows.h>
#include "include/easy_define.h"
#include "lib/ob_errno.h"
#ifndef SIGPIPE
#define SIGPIPE   13
#endif
#ifndef SIGUSR1
#define SIGUSR1   10
#endif
#ifndef SIGKILL
#define SIGKILL    9
#endif
#ifndef SIG_BLOCK
#define SIG_BLOCK  0
#endif
static inline int sigemptyset(sigset_t *s) { *s = 0; return 0; }
static inline int sigaddset(sigset_t *s, int sig) {
  if (sig < 0 || sig >= 64) return -1;
  *s |= (1ULL << sig);
  return 0;
}
static inline int pthread_sigmask(int, const sigset_t*, sigset_t*) { return 0; }
static inline int sigtimedwait(const sigset_t*, void*, const struct timespec *ts) {
  DWORD ms = 1000;
  if (ts) {
    ms = (DWORD)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
    if (ms == 0) ms = 1;
  }
  Sleep(ms);
  return -1;
}
#else
#include <signal.h>
#endif
#include "share/ob_thread_pool.h"

namespace oceanbase
{
namespace observer
{
class ObSignalHandle: public share::ObThreadPool
{
public:
  ObSignalHandle() : thread_id_{} {}
  ~ObSignalHandle() {
    stop();
#ifdef __APPLE__
    // On macOS, sigwait() may block indefinitely. Send SIGUSR1 to wake up the thread.
    // SIGUSR1 is in the signal set being waited on (see add_signums_to_set).
    if (thread_id_ != 0) {
      pthread_kill(thread_id_, SIGUSR1);
    }
#endif
    wait();
  }
  virtual void run1();
  //should be called in main thread. Change signal mask to block these signals.
  static int change_signal_mask();
  //add signals to signal set.
  static int add_signums_to_set(sigset_t &sig_set);
  //deal signals. Called in the signal handle thread.
  static int deal_signals(int signum);

  // Store thread id for macOS wakeup
  pthread_t thread_id_;
};

}
}

#endif
