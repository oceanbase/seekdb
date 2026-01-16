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
#include <getopt.h>
#include <locale.h>
#include <malloc.h>
#include <sys/time.h>
#include <sys/resource.h>
// easy complains in compiling if put the right position.
#include <link.h>
#include <dlfcn.h>

using namespace oceanbase::obsys;
using namespace oceanbase;
using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::diagnose;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::omt;

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
  char softlink_path[PATH_MAX] = {0};
  snprintf(softlink_path, PATH_MAX, "%s/seekdb", PID_DIR);
  char target_path[PATH_MAX] = {0};
  ssize_t read_len = readlink("/proc/self/exe", target_path, PATH_MAX - 1);
  if (read_len < 0) {
    ret = OB_IO_ERROR;
    MPRINT("failed to readlink /proc/self/exe, errno=%s", strerror(errno));
  }
  if (OB_FAIL(ret)) {
  } else if (FALSE_IT(FileDirectoryUtils::unlink_symlink(softlink_path))) {
  } else if (OB_FAIL(FileDirectoryUtils::symlink(target_path, softlink_path))) {
    ret = OB_IO_ERROR;
    MPRINT("create seekdb softlink failed, errno=%s", strerror(errno));
  }
  return ret;
}
static int dump_config_to_json()
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(g_config_mem_attr);
  ObJsonArray j_arr(&allocator);
  ObJsonBuffer j_buf(&allocator);
  FILE *out_file = nullptr;
  const char *out_path = "./ob_all_available_parameters.json";
  if (OB_FAIL(ObServerConfig::get_instance().to_json_array(allocator, j_arr))) {
    MPRINT("dump cluster config to json failed, ret=%d\n", ret);
  } else if (OB_FAIL(j_arr.print(j_buf, false))) {
    MPRINT("print json array to buffer failed, ret=%d\n", ret);
  } else if (nullptr == j_buf.ptr()) {
    ret = OB_ERR_NULL_VALUE;
    MPRINT("json buffer is null, ret=%d\n", ret);
  } else if (nullptr == (out_file = fopen(out_path, "w"))) {
    ret = OB_IO_ERROR;
    MPRINT("failed to open file, errno=%d, ret=%d\n", errno, ret);
  } else if (EOF == fputs(j_buf.ptr(), out_file)) {
    ret = OB_IO_ERROR;
    MPRINT("write json buffer to file failed, errno=%d, ret=%d\n", errno, ret);
  }

  if (nullptr != out_file) {
    fclose(out_file);
  }
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
    for (int j = 0; j < info->dlpi_phnum; j++) {
      if (NULL != info->dlpi_phdr) {
        _LOG_INFO(
            "\t\t header %2d: address=%10p",
            j,
            (void *)(info->dlpi_addr + info->dlpi_phdr[j].p_vaddr));
      }
    }
  }
  return 0;
}

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
  print_limit("RLIMIT_LOCKS",RLIMIT_LOCKS);
  print_limit("RLIMIT_MEMLOCK",RLIMIT_MEMLOCK);
  print_limit("RLIMIT_NOFILE",RLIMIT_NOFILE);
  print_limit("RLIMIT_NPROC",RLIMIT_NPROC);
  print_limit("RLIMIT_STACK",RLIMIT_STACK);
  OB_LOG(INFO, "============= *stop server limit report* ===============");
}

static int check_uid_before_start(const char *dir_path)
{
  int ret = OB_SUCCESS;
  uid_t current_uid = UINT_MAX;
  struct stat64 dir_info;

  current_uid = getuid();
  if (0 != ::stat64(dir_path, &dir_info)) {
    /* do nothing */
  } else {
    if (current_uid != dir_info.st_uid) {
      ret = OB_UTL_FILE_ACCESS_DENIED;
      MPRINT("ERROR: current user(uid=%u) that starts seekdb is not the same with the original one(uid=%u), seekdb starts failed!",
              current_uid, dir_info.st_uid);
    }
  }

  return ret;
}

// systemd dynamic loading
static int safe_sd_notify(int unset_environment, const char *state)
{
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
}

int inner_main(int argc, char *argv[])
{
  // temporarily unlimited memory before init config
  set_memory_limit(INT_MAX64);

#ifdef ENABLE_SANITY
  backtrace_symbolize_func = oceanbase::common::backtrace_symbolize;
#endif
  if (0 != pthread_getname_np(pthread_self(), ob_get_tname(), OB_THREAD_NAME_BUF_LEN)) {
    snprintf(ob_get_tname(), OB_THREAD_NAME_BUF_LEN, "seekdb");
  }
  ObStackHeaderGuard stack_header_guard;
  int64_t memory_used = get_virtual_memory_used();
#ifndef OB_USE_ASAN
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
  abort_unless(0 == sigaltstack(&nss, &oss));
  DEFER(sigaltstack(&oss, nullptr));
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

  // change signal mask first.
  if (OB_FAIL(ObSignalHandle::change_signal_mask())) {
    MPRINT("change signal mask failed, ret=%d", ret);
  }

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
    fprintf(stderr, "The log file is in the directory: ");
    fprintf(stderr, opts->base_dir_.ptr());
    if (opts->base_dir_.ptr()[opts->base_dir_.length() - 1] != '/') {
      fprintf(stderr, "/");
    }
    fprintf(stderr, "log/\n");
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
      _LOG_INFO("Virtual memory : %'15ld byte", memory_used);
    }
    // print in log file.
    LOG_INFO("Build basic information for each syslog file", "info", syslog_file_info);
    print_args(argc, argv);
    ObCommandLineParser::print_version();
    print_all_limits();
    dl_iterate_phdr(callback, NULL);

    static const int DEFAULT_MMAP_MAX_VAL = 1024 * 1024 * 1024;
    mallopt(M_MMAP_MAX, DEFAULT_MMAP_MAX_VAL);
    mallopt(M_ARENA_MAX, 1); // disable malloc multiple arena pool

    // turn warn log on so that there's a observer.log.wf file which
    // records all WARN and ERROR logs in log directory.
    ObWarningBuffer::set_warn_log_on(true);
    if (OB_SUCC(ret)) {
      const bool embed_mode = opts->embed_mode_;
      const bool initialize = opts->initialize_;
      // Create worker to make this thread having a binding
      // worker. When ObThWorker is creating, it'd be aware of this
      // thread has already had a worker, which can prevent binding
      // new worker with it.
      lib::Worker worker;
      lib::Worker::set_worker_to_thread_local(&worker);
      ObServer &observer = ObServer::get_instance();
      LOG_INFO("seekdb starts", "seekdb_version", PACKAGE_STRING);
      // to speed up bootstrap phase, need set election INIT TS
      // to count election keep silence time as soon as possible after seekdb process started
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

int main(int argc, char *argv[])
{
  int ret = OB_SUCCESS;
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
  return ret;
}
