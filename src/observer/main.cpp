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

#define USING_LOG_PREFIX SERVER

#include "lib/alloc/malloc_hook.h"
#include "lib/alloc/ob_malloc_allocator.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/file/file_directory_utils.h"
#include "lib/oblog/ob_easy_log.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "lib/allocator/ob_mem_leak_checker.h"
#include "lib/allocator/ob_libeasy_mem_pool.h"
#include "lib/signal/ob_signal_struct.h"
#include "lib/utility/ob_defer.h"
#include "objit/ob_llvm_symbolizer.h"
#include "observer/ob_command_line_parser.h"
#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"
#include "observer/ob_signal_handle.h"
#include "share/config/ob_server_config.h"
#include "share/ob_tenant_mgr.h"
#include "share/ob_version.h"
#include <curl/curl.h>
#ifndef _WIN32
#include <getopt.h>
#endif
#include <locale.h>
#ifdef __APPLE__
#include <stdlib.h> // malloc.h is not available on macOS, use stdlib.h instead
#include <mach-o/dyld.h> // for _NSGetExecutablePath
#else
#include <malloc.h>
#endif
#ifndef _WIN32
#include <sys/time.h>
#include <sys/resource.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include "observer/ob_win_service.h"
extern "C" void win32_trace(const char *msg) {
  HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
  DWORD written;
  WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
}
#include <signal.h>
#pragma init_seg(compiler)

typedef struct {
  DWORD ThreadId;
  EXCEPTION_POINTERS *ExceptionPointers;
  BOOL ClientPointers;
} MY_MINIDUMP_EXCEPTION_INFORMATION;

typedef BOOL (WINAPI *MiniDumpWriteDumpFn)(
    HANDLE, DWORD, HANDLE, ULONG,
    MY_MINIDUMP_EXCEPTION_INFORMATION*, void*, void*);

static void write_minidump(EXCEPTION_POINTERS *ep) {
  char path[MAX_PATH];
  SYSTEMTIME st;
  GetLocalTime(&st);
  snprintf(path, sizeof(path), "observer_crash_%04d%02d%02d_%02d%02d%02d_%lu.dmp",
           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
           (unsigned long)GetCurrentProcessId());
  HMODULE hDbgHelp = LoadLibraryA("dbghelp.dll");
  if (!hDbgHelp) {
    win32_trace("[WIN32-TRACE] Failed to load dbghelp.dll\r\n");
    return;
  }
  auto pMiniDumpWriteDump = (MiniDumpWriteDumpFn)GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
  if (!pMiniDumpWriteDump) {
    win32_trace("[WIN32-TRACE] MiniDumpWriteDump not found in dbghelp.dll\r\n");
    return;
  }
  HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hFile != INVALID_HANDLE_VALUE) {
    MY_MINIDUMP_EXCEPTION_INFORMATION mei;
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    BOOL ok = pMiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                                 2 /*MiniDumpWithFullMemory*/,
                                 &mei, NULL, NULL);
    CloseHandle(hFile);
    char msg[512];
    snprintf(msg, sizeof(msg), "[WIN32-TRACE] Minidump %s: %s\r\n",
             ok ? "written" : "FAILED", path);
    win32_trace(msg);
  } else {
    win32_trace("[WIN32-TRACE] Failed to create minidump file\r\n");
  }
}

static volatile LONG g_dumping = 0;
static volatile LONG g_crash_count = 0;
static volatile LONG g_bg_crash_count = 0;
static DWORD g_main_thread_id = 0;

static LONG WINAPI win32_vectored_handler(EXCEPTION_POINTERS *ep) {
  DWORD code = ep->ExceptionRecord->ExceptionCode;
  if (code != 0xC0000005 && code != 0xC0000096 && code != 0xC000001D &&
      code != 0xC0000028 && code != 0x80000003) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  LONG crash_seq = InterlockedIncrement(&g_crash_count);
  char buf[4096];
  DWORD tid = GetCurrentThreadId();
  BOOL is_main = (tid == g_main_thread_id);
  int n = snprintf(buf, sizeof(buf),
    "[WIN32-TRACE] Vectored exception #%ld: code=0x%08lX addr=0x%p tid=%lu%s\r\n",
    crash_seq, code, (void*)ep->ExceptionRecord->ExceptionAddress, tid,
    is_main ? " (MAIN)" : " (bg-thread)");
  if (code == 0xC0000005 && ep->ExceptionRecord->NumberParameters >= 2) {
    const char *op = (ep->ExceptionRecord->ExceptionInformation[0] == 0) ? "READ" :
                     (ep->ExceptionRecord->ExceptionInformation[0] == 1) ? "WRITE" : "DEP";
    n += snprintf(buf + n, sizeof(buf) - n,
      "  AccessViolation: %s at 0x%p\r\n", op,
      (void*)ep->ExceptionRecord->ExceptionInformation[1]);
  }
  CONTEXT *ctx = ep->ContextRecord;
  uintptr_t exe_base = (uintptr_t)GetModuleHandleA(NULL);
  uintptr_t rva = (uintptr_t)ctx->Rip - exe_base;
  n += snprintf(buf + n, sizeof(buf) - n,
    "  Rip=0x%p RVA=0x%llX Rsp=0x%p Rbp=0x%p\r\n",
    (void*)ctx->Rip, (unsigned long long)rva, (void*)ctx->Rsp, (void*)ctx->Rbp);

  HMODULE hMod = NULL;
  char modname[MAX_PATH] = {0};
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         (LPCSTR)ctx->Rip, &hMod) && hMod) {
    GetModuleFileNameA(hMod, modname, sizeof(modname));
    uintptr_t mod_rva = (uintptr_t)ctx->Rip - (uintptr_t)hMod;
    n += snprintf(buf + n, sizeof(buf) - n,
      "  Module: %s + 0x%llX\r\n", modname, (unsigned long long)mod_rva);
  }
  {
    n += snprintf(buf + n, sizeof(buf) - n, "  Stack (RIP-based, RVAs):");
    uintptr_t rsp_val = (uintptr_t)ctx->Rsp;
    for (int fi = 0; fi < 64 && n < (int)sizeof(buf) - 64; fi++) {
      uintptr_t *slot = (uintptr_t *)(rsp_val + fi * 8);
      __try {
        uintptr_t val = *slot;
        if (val >= exe_base && val < exe_base + 0x20000000ULL) {
          n += snprintf(buf + n, sizeof(buf) - n, " [sp+%d]=0x%llX",
            fi * 8, (unsigned long long)(val - exe_base));
        }
      } __except(EXCEPTION_EXECUTE_HANDLER) { break; }
    }
    n += snprintf(buf + n, sizeof(buf) - n, "\r\n");
  }
  win32_trace(buf);

  if (IsDebuggerPresent()) {
    win32_trace("[WIN32-TRACE] Debugger attached, passing exception to debugger.\r\n");
    return EXCEPTION_CONTINUE_SEARCH;
  }

  if (is_main) {
    if (InterlockedCompareExchange(&g_dumping, 1, 0) == 0) {
      write_minidump(ep);
    }
    win32_trace("[WIN32-TRACE] Main thread crash, terminating.\r\n");
    TerminateProcess(GetCurrentProcess(), 3);
  }

  LONG bg_seq = InterlockedIncrement(&g_bg_crash_count);
  if (bg_seq <= 3) {
    if (InterlockedCompareExchange(&g_dumping, 1, 0) == 0) {
      write_minidump(ep);
    }
  }
  if (bg_seq > 200) {
    win32_trace("[WIN32-TRACE] Too many bg-thread crashes (>200), terminating.\r\n");
    TerminateProcess(GetCurrentProcess(), 3);
  }
  win32_trace("[WIN32-TRACE] Background thread crash, killing thread only.\r\n");
  ExitThread(1);
  return EXCEPTION_CONTINUE_SEARCH;
}
static struct Win32EarlyInit {
  Win32EarlyInit() {
    g_main_thread_id = GetCurrentThreadId();
    AddVectoredExceptionHandler(1, win32_vectored_handler);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  }
} g_win32_early_init;

void ob_abort(void) {
  char buf[512];
  int n = snprintf(buf, sizeof(buf),
    "[WIN32] ob_abort() called. lbt: %s\r\n",
    oceanbase::common::lbt());
  win32_trace(buf);
  abort();
}
#endif
// easy complains in compiling if put the right position.
#ifdef __APPLE__
// macOS doesn't have link.h, provide stub definitions
#include <dlfcn.h>
#include <stdint.h>
// Simplified stub structure for macOS (not using ELF types)
struct dl_phdr_info {
  uintptr_t dlpi_addr;  // Base address of object
  const char *dlpi_name; // (Null-terminated) name of object
  const void *dlpi_phdr; // Pointer to array of program headers (stub)
  uint16_t dlpi_phnum; // # of items in dlpi_phdr
};
// Stub implementation of dl_iterate_phdr for macOS
static int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info, size_t size, void *data), void *data) {
  (void)callback;
  (void)data;
  // macOS doesn't support dl_iterate_phdr, return 0 (no error but no iterations)
  return 0;
}
#elif defined(_WIN32)
// Windows doesn't have link.h, provide stub definitions
#include <stdint.h>
struct dl_phdr_info {
  uintptr_t dlpi_addr;
  const char *dlpi_name;
  const void *dlpi_phdr;
  uint16_t dlpi_phnum;
};
static int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *info, size_t size, void *data), void *data) {
  (void)callback;
  (void)data;
  return 0;
}
#else
#include <link.h>
#include <dlfcn.h>
#endif

using namespace oceanbase::obsys;
using namespace oceanbase;
using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::diagnose;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::omt;

namespace oceanbase { namespace share { void ob_init_create_func(); } }

#define MPRINT(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)
#define MPRINTx(format, ...)                                                   \
  MPRINT(format, ##__VA_ARGS__);                                               \
  exit(1)

const char  LOG_DIR[]  = "log";
const char  PID_DIR[]  = "run";
const char  CONF_DIR[] = "etc";

static int create_observer_softlink()
{
  int ret = OB_SUCCESS;
  char softlink_path[4096] = {0};
  snprintf(softlink_path, sizeof(softlink_path), "%s/seekdb", PID_DIR);
  char target_path[4096] = {0};
#ifdef _WIN32
  if (0 == GetModuleFileNameA(nullptr, target_path, sizeof(target_path))) {
    ret = OB_IO_ERROR;
    MPRINT("failed to get executable path on Windows");
  }
  if (OB_SUCC(ret)) {
    char link_path_exe[4096] = {0};
    snprintf(link_path_exe, sizeof(link_path_exe), "%s.exe", softlink_path);
    DeleteFileA(link_path_exe);
    if (!CopyFileA(target_path, link_path_exe, FALSE)) {
      if (!CreateHardLinkA(link_path_exe, target_path, NULL)) {
        ret = OB_IO_ERROR;
        MPRINT("create seekdb copy/hardlink failed, err=%lu", GetLastError());
      }
    }
  }
#elif defined(__APPLE__)
  uint32_t size = PATH_MAX;
  if (0 != _NSGetExecutablePath(target_path, &size)) {
    ret = OB_IO_ERROR;
    MPRINT("failed to get executable path on macOS");
  }
  if (OB_SUCC(ret)) {
    char resolved_path[PATH_MAX] = {0};
    if (nullptr == realpath(target_path, resolved_path)) {
      ret = OB_IO_ERROR;
      MPRINT("failed to resolve executable path, errno=%s", strerror(errno));
    } else {
      strncpy(target_path, resolved_path, PATH_MAX - 1);
      target_path[PATH_MAX - 1] = '\0';
    }
  }
  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(FileDirectoryUtils::unlink_symlink(softlink_path))) {
  } else if (OB_FAIL(FileDirectoryUtils::symlink(target_path, softlink_path))) {
    ret = OB_IO_ERROR;
    MPRINT("create seekdb softlink failed, errno=%s", strerror(errno));
  }
#else
  ssize_t read_len = readlink("/proc/self/exe", target_path, sizeof(target_path) - 1);
  if (read_len < 0) {
    ret = OB_IO_ERROR;
    MPRINT("failed to readlink /proc/self/exe, errno=%s", strerror(errno));
  } else {
    target_path[read_len] = '\0';
  }
  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(FileDirectoryUtils::unlink_symlink(softlink_path))) {
  } else if (OB_FAIL(FileDirectoryUtils::symlink(target_path, softlink_path))) {
    ret = OB_IO_ERROR;
    MPRINT("create seekdb softlink failed, errno=%s", strerror(errno));
  }
#endif
  return ret;
}
static void print_args(int argc, char *argv[])
{
  for (int i = 0; i < argc - 1; ++i) {
    fprintf(stderr, "%s ", argv[i]);
  }
  fprintf(stderr, "%s\n", argv[argc - 1]);
}

/**
 * 解析命令行参数
 * @details 解析命令行参数，并初始化ObServerOptions。
 * @param argc 命令行参数个数
 * @param argv 命令行参数
 * @param opts 配置选项
 */
static int parse_args(int argc, char *argv[], ObServerOptions &opts)
{
  int ret = OB_SUCCESS;

  ObCommandLineParser parser;
  bool config_file_exists = false;

  // 解析参数，结果直接设置到opts中
  if (OB_FAIL(parser.parse_args(argc, argv, opts))) {
    MPRINT("Failed to parse command line arguments, ret=%d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.base_dir_.ptr()))) {
    MPRINT("Failed to create base dir. path='%s', system error=%s", opts.base_dir_.ptr(), strerror(errno));
  } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(opts.base_dir_))) {
  }

  return ret;
}

static int callback(struct dl_phdr_info *info, size_t size, void *data)
{
  UNUSED(size);
  UNUSED(data);
  if (OB_ISNULL(info)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "invalid argument", K(info));
  } else {
    _LOG_INFO("name=%s (%d segments)", info->dlpi_name, info->dlpi_phnum);
#if defined(__APPLE__) || defined(_WIN32)
    // macOS/Windows stub: dlpi_phdr is const void* and cannot be accessed
#else
    for (int j = 0; j < info->dlpi_phnum; j++) {
      if (NULL != info->dlpi_phdr) {
        _LOG_INFO(
            "\t\t header %2d: address=%10p",
            j,
            (void *)(info->dlpi_addr + info->dlpi_phdr[j].p_vaddr));
      }
    }
#endif
  }
  return 0;
}

#ifndef _WIN32
static void print_limit(const char *name, const int resource)
{
  struct rlimit limit;
  if (0 == getrlimit(resource, &limit)) {
    if (RLIM_INFINITY == limit.rlim_cur) {
      _OB_LOG(INFO, "[%s] %-24s = %s", __func__, name, "unlimited");
    } else {
      _OB_LOG(INFO, "[%s] %-24s = %ld", __func__, name, limit.rlim_cur);
    }
  }
  if (RLIMIT_CORE == resource) {
    g_rlimit_core = limit.rlim_cur;
  }
}

static void print_all_limits()
{
  OB_LOG(INFO, "============= *begin server limit report * =============");
  print_limit("RLIMIT_CORE",RLIMIT_CORE);
  print_limit("RLIMIT_CPU",RLIMIT_CPU);
  print_limit("RLIMIT_DATA",RLIMIT_DATA);
  print_limit("RLIMIT_FSIZE",RLIMIT_FSIZE);
#ifdef __APPLE__
  // RLIMIT_LOCKS is not available on macOS
#else
  print_limit("RLIMIT_LOCKS",RLIMIT_LOCKS);
#endif
  print_limit("RLIMIT_MEMLOCK",RLIMIT_MEMLOCK);
  print_limit("RLIMIT_NOFILE",RLIMIT_NOFILE);
  print_limit("RLIMIT_NPROC",RLIMIT_NPROC);
  print_limit("RLIMIT_STACK",RLIMIT_STACK);
  OB_LOG(INFO, "============= *stop server limit report* ===============");
}
#else
static void print_all_limits()
{
  (void)0;  // rlimit/getrlimit not available on Windows
}
#endif

static int check_uid_before_start(const char *dir_path)
{
#ifdef _WIN32
  (void)dir_path;
  return OB_SUCCESS;  // skip uid check on Windows
#else
  int ret = OB_SUCCESS;
  uid_t current_uid = UINT_MAX;
#ifdef __APPLE__
  struct stat dir_info;
#else
  struct stat64 dir_info;
#endif

  current_uid = getuid();
#ifdef __APPLE__
  if (0 != ::stat(dir_path, &dir_info)) {
#else
  if (0 != ::stat64(dir_path, &dir_info)) {
#endif
    /* do nothing */
  } else {
    if (current_uid != dir_info.st_uid) {
      ret = OB_UTL_FILE_ACCESS_DENIED;
      MPRINT("ERROR: current user(uid=%u) that starts seekdb is not the same with the original one(uid=%u), seekdb starts failed!",
              current_uid, dir_info.st_uid);
    }
  }

  return ret;
#endif
}

// systemd dynamic loading
static int safe_sd_notify(int unset_environment, const char *state)
{
#ifdef _WIN32
  (void)unset_environment;
  (void)state;
  return 0;  // systemd not on Windows
#else
  typedef int (*sd_notify_func_t)(int unset_environment, const char *state);
  sd_notify_func_t sd_notify_func = nullptr;
  void *systemd_handle = nullptr;
  int ret = OB_SUCCESS;
  systemd_handle = dlopen("libsystemd.so.0", RTLD_LAZY);
  if (nullptr == systemd_handle) {
    systemd_handle = dlopen("libsystemd.so", RTLD_LAZY);
  }
  if (nullptr == systemd_handle) {
      LOG_INFO("systemd library not available, sd_notify will be disabled");
  } else {
    sd_notify_func = (sd_notify_func_t)dlsym(systemd_handle, "sd_notify");
    if (nullptr == sd_notify_func) {
      LOG_WARN("failed to get sd_notify symbol from systemd library");
    } else {
      LOG_INFO("systemd notify initialized successfully");
      // Call sd_notify if available
      sd_notify_func(unset_environment, state);
    }
  }

  // close systemd handle
  if (nullptr != systemd_handle) {
    dlclose(systemd_handle);
    systemd_handle = nullptr;
  }
  return ret;
#endif
}

int inner_main(int argc, char *argv[])
{
  // temporarily unlimited memory before init config
  set_memory_limit(INT_MAX64);

#ifdef ENABLE_SANITY
  backtrace_symbolize_func = oceanbase::common::backtrace_symbolize;
#endif
#if defined(_WIN32) || defined(__ANDROID__)
  snprintf(ob_get_tname(), OB_THREAD_NAME_BUF_LEN, "seekdb");
#else
  if (0 != pthread_getname_np(pthread_self(), ob_get_tname(), OB_THREAD_NAME_BUF_LEN)) {
    snprintf(ob_get_tname(), OB_THREAD_NAME_BUF_LEN, "seekdb");
  }
#endif
  ObStackHeaderGuard stack_header_guard;
  int64_t memory_used = get_virtual_memory_used();
#if !defined(OB_USE_ASAN) && !defined(_WIN32)
  /**
    signal handler stack
   */
  void *ptr = malloc(SIG_STACK_SIZE);
  abort_unless(ptr != nullptr);
  stack_t nss;
  stack_t oss;
  bzero(&nss, sizeof(nss));
  bzero(&oss, sizeof(oss));
  nss.ss_sp = ptr;
  nss.ss_size = SIG_STACK_SIZE;
#ifdef __APPLE__
  // On macOS, sigaltstack might fail or behave differently
  // Just try to set it but don't abort if it fails
  int sigaltstack_ret = sigaltstack(&nss, &oss);
  if (0 == sigaltstack_ret) {
    DEFER(sigaltstack(&oss, nullptr));
  } else {
    // sigaltstack failed, but continue anyway
    free(ptr);
    ptr = nullptr;
  }
#else
  abort_unless(0 == sigaltstack(&nss, &oss));
  DEFER(sigaltstack(&oss, nullptr));
#endif
  ::oceanbase::common::g_redirect_handler = true;
#endif

  // Fake routines for current thread.

#ifndef OB_USE_ASAN
  get_mem_leak_checker().init();
#endif

  ObCurTraceId::SeqGenerator::seq_generator_  = ObTimeUtility::current_time();
  static const int  LOG_FILE_SIZE             = DEFAULT_LOG_FILE_SIZE_MB * 1024 * 1024;
  const char *const LOG_FILE_NAME             = "log/seekdb.log";
  const char *const PID_FILE_NAME             = "run/seekdb.pid";
  int               ret                       = OB_SUCCESS;

  MPRINT("Starting seekdb (%s %s %s) source revision %s.",
    OB_OCEANBASE_NAME, OB_SEEKDB_NAME, PACKAGE_VERSION, build_version());

#ifndef _WIN32
  // change signal mask first (POSIX only).
  if (OB_FAIL(ObSignalHandle::change_signal_mask())) {
    MPRINT("change signal mask failed, ret=%d", ret);
  }
#endif

  lib::ObMemAttr mem_attr(OB_SYS_TENANT_ID, "ObserverAlloc");
  ObServerOptions *opts = nullptr;
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(opts = OB_NEW(ObServerOptions, mem_attr))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    MPRINT("Failed to allocate memory for ObServerOptions.");
  }

  // no diagnostic info attach to main thread.
  ObDisableDiagnoseGuard disable_guard;
  setlocale(LC_ALL, "");
  // Set character classification type to C to avoid printf large string too
  // slow.
  setlocale(LC_CTYPE, "C");
  setlocale(LC_TIME, "en_US.UTF-8");
  setlocale(LC_NUMERIC, "en_US.UTF-8");

  opts->log_level_ = DEFAULT_LOG_LEVEL;
  if (FAILEDx(parse_args(argc, argv, *opts))) {
  }

  if (OB_FAIL(ret)) {
  } else if (0 != chdir(opts->base_dir_.ptr())) {
    ret = OB_ERR_UNEXPECTED;
    MPRINT("Failed to change working directory to base dir. path='%s', system error='%s'",
      opts->base_dir_.ptr(), strerror(errno));
  } else {
    MPRINT("Change working directory to base dir. path='%s'", opts->base_dir_.ptr());
    fprintf(stderr, "The log file is in the directory: '");
    fprintf(stderr, opts->base_dir_.ptr());
    if (opts->base_dir_.ptr()[opts->base_dir_.length() - 1] != '/') {
      fprintf(stderr, "/");
    }
    fprintf(stderr, "log/'\n");
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(check_uid_before_start(CONF_DIR))) {
    MPRINT("Fail check_uid_before_start, please use the initial user to start seekdb!");
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(PID_DIR))) {
    MPRINT("create pid dir fail: ./run/");
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(LOG_DIR))) {
    MPRINT("create log dir fail: ./log/");
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(CONF_DIR))) {
    MPRINT("create log dir fail: ./etc/");
  } else if (FALSE_IT(create_observer_softlink())) {
  } else if (OB_FAIL(ObEncryptionUtil::init_ssl_malloc())) {
    MPRINT("failed to init crypto malloc");
  }
  if (OB_FAIL(ret)) {
  } else if (!opts->nodaemon_ && !opts->initialize_) {
    MPRINT("The seekdb will be started as a daemon process. You can check the server status by client later.");
    MPRINT("    Start seekdb with --nodaemon if you don't want to start as a daemon process.");
    if (OB_FAIL(start_daemon(PID_FILE_NAME))) {
      MPRINT("Start seekdb as a daemon failed. Did you started seekdb already?");
    }
  } else if (opts->nodaemon_) {
    if (OB_FAIL(start_daemon(PID_FILE_NAME, true/*skip_daemon*/))) {
      MPRINT("Start seekdb failed. Did you started seekdb already?");
    }
  }

  if (OB_FAIL(ret)) {
  } else {
    ObCurTraceId::get_trace_id()->set("Y0-0000000000000001-0-0");
    CURLcode curl_code = curl_global_init(CURL_GLOBAL_ALL);
    OB_ASSERT(CURLE_OK == curl_code);

    const char *syslog_file_info = ObServerUtils::build_syslog_file_info();
    OB_LOGGER.set_log_level(opts->log_level_);
    OB_LOGGER.set_max_file_size(LOG_FILE_SIZE);
    OB_LOGGER.set_new_file_info(syslog_file_info);
    OB_LOGGER.set_file_name(LOG_FILE_NAME, true/*no_redirect_flag*/);
    ObPLogWriterCfg log_cfg;
    LOG_INFO("succ to init logger",
             "default file", LOG_FILE_NAME,
             "max_log_file_size", LOG_FILE_SIZE,
             "enable_async_log", OB_LOGGER.enable_async_log());
    if (0 == memory_used) {
      _LOG_INFO("Get virtual memory info failed");
    } else {
#ifdef _WIN32
      _LOG_INFO("Virtual memory : %15ld byte", memory_used);
#else
      _LOG_INFO("Virtual memory : %'15ld byte", memory_used);
#endif
    }
    // print in log file.
    LOG_INFO("Build basic information for each syslog file", "info", syslog_file_info);
    print_all_limits();
    dl_iterate_phdr(callback, NULL);

#if defined(__APPLE__) || defined(__ANDROID__)
    // macOS/Android don't support M_MMAP_MAX and M_ARENA_MAX
#elif defined(__linux__)
    static const int DEFAULT_MMAP_MAX_VAL = 1024 * 1024 * 1024;
    mallopt(M_MMAP_MAX, DEFAULT_MMAP_MAX_VAL);
    mallopt(M_ARENA_MAX, 1); // disable malloc multiple arena pool
#endif

    // turn warn log on so that there's a observer.log.wf file which
    // records all WARN and ERROR logs in log directory.
    ObWarningBuffer::set_warn_log_on(true);
    if (OB_SUCC(ret)) {
      const bool embed_mode = opts->embed_mode_;
      const bool initialize = opts->initialize_;
      lib::Worker worker;
      lib::Worker::set_worker_to_thread_local(&worker);
      lib::init_create_func();
      oceanbase::share::ob_init_create_func();
      lib::create_func_inited_ = true;
      lib::TGMgr::instance();
      {
        auto &tg_mgr = lib::TGMgr::instance();
        int fixed = 0;
        for (int i = 0; i < lib::TGDefIDs::END; i++) {
          if (lib::create_funcs_[i] && !tg_mgr.tgs_[i]) {
            tg_mgr.tgs_[i] = lib::create_funcs_[i]();
            if (tg_mgr.tgs_[i]) fixed++;
          }
        }
      }
      ObServer &observer = ObServer::get_instance();
      LOG_INFO("seekdb starts", "seekdb_version", PACKAGE_STRING);
      ATOMIC_STORE(&palf::election::INIT_TS, palf::election::get_monotonic_ts());
      if (OB_FAIL(observer.init(*opts, log_cfg))) {
        LOG_ERROR("seekdb init fail", K(ret));
      }
      OB_DELETE(ObServerOptions, mem_attr, opts);
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(observer.start(embed_mode))) {
        LOG_ERROR("seekdb start fail", K(ret));
      } else {
        safe_sd_notify(0, "READY=1\n"
                       "STATUS=seekdb is ready and running\n");
      }
      if (initialize) {
        LOG_INFO("seekdb starts in initialize mode, exit now", K(initialize));
        _exit(OB_SUCC(ret) ? 0 : 1);
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(observer.wait())) {
        LOG_ERROR("seekdb wait fail", K(ret));
      }

      if (OB_FAIL(ret)) {
        _exit(1);
      }
      observer.destroy();
    }
    curl_global_cleanup();
    unlink(PID_FILE_NAME);
  }

  LOG_INFO("seekdb exits", "seekdb_version", PACKAGE_STRING);
  return ret;
}

#ifdef OB_USE_ASAN
const char* __asan_default_options()
{
  return "abort_on_error=1:disable_coredump=0:unmap_shadow_on_exit=1:log_path=./log/asan.log";
}
#endif

#ifdef _WIN32
static bool has_arg(int argc, char *argv[], const char *name)
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], name) == 0) return true;
  }
  return false;
}

static const char *get_arg_value(int argc, char *argv[], const char *name)
{
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], name) == 0 && i + 1 < argc && argv[i + 1][0] != '-') {
      return argv[i + 1];
    }
  }
  return nullptr;
}
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
  ::oceanbase::common::g_ob_log_main_entered = true;
#endif
  int ret = OB_SUCCESS;
#ifdef _WIN32
  if (has_arg(argc, argv, "--install-service")) {
    return oceanbase::observer::ob_install_win_service(
        get_arg_value(argc, argv, "--install-service"), argc, argv);
  } else if (has_arg(argc, argv, "--remove-service")) {
    return oceanbase::observer::ob_remove_win_service(
        get_arg_value(argc, argv, "--remove-service"));
  } else if (has_arg(argc, argv, "--service")) {
    return oceanbase::observer::ob_start_as_win_service(
        oceanbase::observer::OB_DEFAULT_SERVICE_NAME, inner_main, argc, argv);
  }
  ret = inner_main(argc, argv);
#else
  size_t stack_size = 1LL<<20;
  void *stack_addr = ::mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (MAP_FAILED == stack_addr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = CALL_WITH_NEW_STACK(inner_main(argc, argv), stack_addr, stack_size);
    if (-1 == ::munmap(stack_addr, stack_size)) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
#endif
  return ret;
}
