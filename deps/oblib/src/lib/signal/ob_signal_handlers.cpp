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

#define USING_LOG_PREFIX COMMON
#define _GNU_SOURCE 1
#include "lib/profile/ob_trace_id.h"
#include "lib/signal/ob_signal_struct.h"
#ifndef _WIN32
#include <dirent.h>
#include "lib/utility/ob_platform_utils.h"  // Platform compatibility layer
#include "lib/utility/utility.h"
#include "lib/signal/ob_libunwind.h"
#include "lib/utility/ob_hang_fatal_error.h"
#include "lib/utility/ob_common_utility.h"
#include "lib/signal/ob_signal_utils.h"
#else
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#include <io.h>
#include <process.h>
#endif

namespace oceanbase
{
namespace common
{

#ifndef _WIN32

static const int SIG_SET[] = {SIGABRT, SIGBUS, SIGFPE, SIGSEGV, SIGURG, SIGILL};
static constexpr char MINICORE_SHELL_PATH[] = "tools/minicore.sh";
static constexpr char MINICORE_SCRIPT[] = "if [ -e bin/minicore.py ]; then\n"
"  python bin/minicore.py `cat $(pwd)/run/observer.pid` -c -o core.`cat $(pwd)/run/observer.pid`.mini\n"
"fi\n"
"[ $(ls -1 core.*.mini 2>/dev/null | wc -l) -gt 5 ] && ls -1 core.*.mini -t | tail -n 1 | xargs rm -f";

signal_handler_t &get_signal_handler()
{
  struct Wrapper {
    Wrapper() : v_(ob_signal_handler) {}
    signal_handler_t v_;
  };
  RLOCAL(Wrapper, tl_handler);
  return (&tl_handler)->v_;
}

static inline void handler(int sig, siginfo_t *s, void *p)
{
  if (get_signal_handler() != nullptr) {
    get_signal_handler()(sig, s, p);
  }
}

int install_ob_signal_handler()
{
  int ret = OB_SUCCESS;
#ifdef __ANDROID__
  // ART/libsigchain owns fatal signal dispatch for app processes. Installing
  // OceanBase's process-wide crash handlers here interferes with JNI/runtime
  // fault handling and can turn startup failures into opaque instrumentation
  // crashes.
  return ret;
#else
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_NODEFER | SA_ONSTACK;
  sa.sa_sigaction = handler;
  sigemptyset(&sa.sa_mask);
  for (int i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(SIG_SET); i++) {
    if (-1 == sigaction(SIG_SET[i], &sa, nullptr)) {
#ifdef __APPLE__
      // On macOS, some signals may fail to install, just log and continue
      // This is not a fatal error on macOS
#else
      ret = OB_INIT_FAIL;
#endif
    }
  }
  return ret;
#endif
}

bool g_redirect_handler = false;
static __thread int g_coredump_num = 0;

#define COMMON_FMT "timestamp=%ld, tid=%ld, tname=%s, trace_id=%s, lbt=%s"

void coredump_cb(int, int, void*, void*);
void ob_signal_handler(int sig, siginfo_t *si, void *context)
{
  if (!g_redirect_handler) {
    signal(sig, SIG_DFL);
    raise(sig);
  } else {
    coredump_cb(sig, si->si_code, si->si_addr, context);
  }
}


void close_socket_fd()
{
  char path[32];
  char name[32];
  char real_name[32];
  DIR *dir = nullptr;
  struct dirent *fd_file = nullptr;
  int fd = -1;
  int pid = getpid();

  lnprintf(path, 32, "/proc/%d/fd/", pid);
  if (NULL == (dir = opendir(path))) {
  } else {
    while(NULL != (fd_file = readdir(dir))) {
      if (0 != strcmp(fd_file->d_name, ".") && 0 != strcmp(fd_file->d_name, "..")
        && 0 != strcmp(fd_file->d_name, "0") && 0 != strcmp(fd_file->d_name, "1")
        && 0 != strcmp(fd_file->d_name, "2")) {
        lnprintf(name, 32, "/proc/%d/fd/%s", pid, fd_file->d_name);
        if (-1 == readlink(name, real_name, 32)) {
        } else {
          lnprintf(name, 32, "%s", real_name);
          if (NULL != strstr(name, "socket")) {
            fd = atoi(fd_file->d_name);
            close(fd);
          }
        }
      }
    }
  }
  if (NULL != dir) {
    closedir(dir);
  }
}


void coredump_cb(volatile int sig, volatile int sig_code, void* volatile sig_addr, void *context)
{
  if (g_coredump_num++ < 1) {
    close_socket_fd();
    timespec time = {0, 0};
    clock_gettime(CLOCK_REALTIME, &time);
    int64_t ts = time.tv_sec * 1000000 + time.tv_nsec / 1000;
    // thread_name
    char tname[16];
    oceanbase::lib::ob_get_thread_name(tname, sizeof(tname));
    // backtrace
    char bt[512] = {'\0'};
    int64_t len = 0;
    // extra
    const ObFatalErrExtraInfoGuard *extra_info = nullptr; // TODO: May deadlock, ObFatalErrExtraInfoGuard::get_thd_local_val_ptr();
    auto *trace_id = ObCurTraceId::get_trace_id();
    char trace_id_buf[128] = {'\0'};
    if (trace_id != nullptr) {
      int64_t pos = trace_id->to_string(trace_id_buf, sizeof(trace_id_buf));
      if (pos < sizeof(trace_id_buf)) {
        trace_id_buf[pos]= '\0';
      }
    }
    char print_buf[1024];
    const ucontext_t *con = (ucontext_t *)context;
#if defined(__x86_64__)
    int64_t ip = con->uc_mcontext.gregs[REG_RIP];
    int64_t bp = con->uc_mcontext.gregs[REG_RBP]; // stack base
    safe_backtrace(bt, sizeof(bt) - 1, &len);
#elif defined(__aarch64__)
#ifdef __APPLE__
    // macOS ARM64: uc_mcontext is a pointer, use __ss.__pc and __ss.__fp
    int64_t ip = con->uc_mcontext->__ss.__pc;
    int64_t bp = con->uc_mcontext->__ss.__fp;
#else
    int64_t ip = con->uc_mcontext.regs[30];
    int64_t bp = con->uc_mcontext.regs[29];
#endif
    void* addrs[64];
    int n_addr = light_backtrace(addrs, ARRAYSIZEOF(addrs), bp);
    len += safe_parray(bt, sizeof(bt) - 1, (int64_t*)addrs, n_addr);
#else
    int64_t ip = -1;
    int64_t bp = -1;
#endif
    bt[len++] = '\0';
    char rlimit_core[32] = "unlimited";
    if (UINT64_MAX != g_rlimit_core) {
      lnprintf(rlimit_core, sizeof(rlimit_core), "%lu", g_rlimit_core);
    }
    char crash_info[128] = "CRASH ERROR!!!";
    int64_t fatal_error_thread_id = get_fatal_error_thread_id();
    if (-1 != fatal_error_thread_id) {
      lnprintf(crash_info, sizeof(crash_info),
               "Right to Die or Duty to Live's Thread Existed before CRASH ERROR!!!"
               "ThreadId=%ld,", fatal_error_thread_id);
    }
    ssize_t print_len = lnprintf(print_buf, sizeof(print_buf),
                                 "%s IP=%lx, RBP=%lx, sig=%d, sig_code=%d, sig_addr=%p, RLIMIT_CORE=%s, "COMMON_FMT", ",
                                  crash_info, ip, bp, sig, sig_code, sig_addr, rlimit_core,
                                  ts, GETTID(), tname, trace_id_buf, bt);
    ObSqlInfo sql_info = ObSqlInfoGuard::get_tl_sql_info();
    char sql_id[] = "SQL_ID=";
    char sql_string[] = ", SQL_STRING=";
    char end[] = "\n";
    struct iovec iov[6];
    memset(iov, 0, sizeof(iov));
    iov[0].iov_base = print_buf;
    iov[0].iov_len = print_len;
    iov[1].iov_base = sql_id;
    iov[1].iov_len = strlen(sql_id);
    iov[2].iov_base = sql_info.sql_id_.ptr();
    iov[2].iov_len = sql_info.sql_id_.length();
    iov[3].iov_base = sql_string;
    iov[3].iov_len = strlen(sql_string);
    iov[4].iov_base = sql_info.sql_string_.ptr();
    iov[4].iov_len = sql_info.sql_string_.length();
    iov[5].iov_base = end;
    iov[5].iov_len = strlen(end);
    writev(STDERR_FILENO, iov, sizeof(iov) / sizeof(iov[0]));
  }
  // Reset back to the default handler
  signal(sig, SIG_DFL);
  raise(sig);
}

#else // _WIN32

signal_handler_t &get_signal_handler()
{
  static thread_local signal_handler_t tl_handler = ob_signal_handler;
  return tl_handler;
}

int install_ob_signal_handler()
{
  signal(SIGABRT, [](int sig) {
    if (get_signal_handler() != nullptr) {
      get_signal_handler()(sig, nullptr, nullptr);
    }
  });
  signal(SIGFPE, [](int sig) {
    if (get_signal_handler() != nullptr) {
      get_signal_handler()(sig, nullptr, nullptr);
    }
  });
  signal(SIGSEGV, [](int sig) {
    if (get_signal_handler() != nullptr) {
      get_signal_handler()(sig, nullptr, nullptr);
    }
  });
  signal(SIGILL, [](int sig) {
    if (get_signal_handler() != nullptr) {
      get_signal_handler()(sig, nullptr, nullptr);
    }
  });
  return OB_SUCCESS;
}

bool g_redirect_handler = false;
static __thread int g_coredump_num = 0;

void ob_signal_handler(int sig, siginfo_t *si, void *context)
{
  if (!g_redirect_handler) {
    signal(sig, SIG_DFL);
    raise(sig);
  } else {
    if (g_coredump_num++ < 1) {
      char print_buf[512];
      int print_len = snprintf(print_buf, sizeof(print_buf),
                               "CRASH ERROR!!! sig=%d, pid=%d\n", sig, (int)_getpid());
      if (print_len > 0) {
        _write(_fileno(stderr), print_buf, print_len);
      }
    }
    signal(sig, SIG_DFL);
    raise(sig);
  }
}

#endif // _WIN32

} // namespace common
} // namespace oceanbase
