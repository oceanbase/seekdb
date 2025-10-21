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
 * 创建并检查目录为空，并且有写权限
 * @param dir_path 目录路径
 * @return 返回值
 * @retval OB_SUCCESS 成功
 * @retval OB_INVALID_ARGUMENT 参数错误
 * @retval OB_ERR_UNEXPECTED 目录不存在或不可写
 */
static int create_and_check_dir_writable(const char *dir_path, bool check_is_empty = true)
{
  int ret = OB_SUCCESS;
  bool is_writable = false;
  bool is_empty = false;
  bool is_exists = false;
  if (OB_ISNULL(dir_path) || strlen(dir_path) == 0) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("[Internal Error] Invalid argument. dir path is null.");
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(FileDirectoryUtils::is_exists(dir_path, is_exists))) {
    MPRINT("Check directory exists failed. path='%s'. system error=%s", dir_path, strerror(errno));
  } else if (is_exists && check_is_empty) {
    if (OB_FAIL(FileDirectoryUtils::is_empty_directory(dir_path, is_empty))) {
      MPRINT("Check directory empty failed. path='%s', system error=%s", dir_path, strerror(errno));
    } else if (!is_empty) {
      ret = OB_INVALID_ARGUMENT;
      MPRINT("Directory is not empty. path=%s", dir_path);
    }
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(dir_path))) {
    MPRINT("Create directory failed. path='%s', system error=%s", dir_path, strerror(errno));
  }

  if (FAILEDx(FileDirectoryUtils::is_writable(dir_path, is_writable))) {
    MPRINT("Check directory writable failed. path=%s, system error=%s", dir_path, strerror(errno));
  } else if (!is_writable) {
    ret = OB_ERR_UNEXPECTED;
    MPRINT("Directory is not writable. path='%s'", dir_path);
  } else {
    MPRINT("Directory is created and writable. path='%s'", dir_path);
  }
  return ret;
}

/**
 * 初始化部署环境
 * @details 初始化observer进程需要的目录，比如 base_dir、data_dir和redo_dir。
 * @param opts 配置选项
 * @return 返回值
 * @retval OB_SUCCESS 成功
 * @retval OB_INVALID_ARGUMENT 参数错误
 * @retval OB_ERR_UNEXPECTED 目录不存在或不可写
 */
static int init_deploy_env(const ObServerOptions &opts)
{
  int ret = OB_SUCCESS;

  const char *sstable_dir = "sstable";
  const char *slog_dir = "slog";

  // 确保我们需要的几个目录是空的，其它目录可以保留。比如用户可以在base_dir下创建 plugin_dir、bin等目录。
  ObSqlString log_dir;
  ObSqlString run_dir;
  ObSqlString etc_dir;
  if (OB_FAIL(log_dir.assign_fmt("%s/%s", opts.base_dir_.ptr(), LOG_DIR))) {
    MPRINT("[Maybe Memory Error] Failed to assign log dir.");
  } else if (OB_FAIL(run_dir.assign_fmt("%s/%s", opts.base_dir_.ptr(), PID_DIR))) {
    MPRINT("[Maybe Memory Error] Failed to assign run dir.");
  } else if (OB_FAIL(etc_dir.assign_fmt("%s/%s", opts.base_dir_.ptr(), CONF_DIR))) {
    MPRINT("[Maybe Memory Error] Failed to assign etc dir.");
  } else if (OB_FAIL(create_and_check_dir_writable(log_dir.ptr(), false/*check_is_empty*/))) {
    MPRINT("Failed to create and check log dir.");
  } else if (OB_FAIL(create_and_check_dir_writable(run_dir.ptr(), false/*check_is_empty*/))) {
    MPRINT("Failed to create and check run dir.");
  } else if (OB_FAIL(create_and_check_dir_writable(etc_dir.ptr(), false/*check_is_empty*/))) {
    MPRINT("Failed to create and check etc dir.");
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(create_and_check_dir_writable(opts.redo_dir_.ptr()))) {
    MPRINT("Failed to create and check redo dir.");
  }

  ObSqlString data_dir;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(data_dir.assign_fmt("%s/%s", opts.data_dir_.ptr(), sstable_dir))) {
    MPRINT("[Maybe Memory Error] Failed to assign base dir.");
  } else if (OB_FAIL(create_and_check_dir_writable(data_dir.ptr()))) {
    MPRINT("Failed to create and check sstable dir.");
  } else if (OB_FAIL(data_dir.assign_fmt("%s/%s", opts.data_dir_.ptr(), slog_dir))) {
    MPRINT("[Maybe Memory Error] Failed to create slog dir variable.");
  } else if (OB_FAIL(create_and_check_dir_writable(data_dir.ptr()))) {
    MPRINT("Failed to create and check slog dir.");
  }

  return ret;
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

  // 解析参数，结果直接设置到opts中
  if (OB_FAIL(parser.parse_args(argc, argv, opts))) {
    MPRINT("Failed to parse command line arguments, ret=%d", ret);
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
      MPRINT("ERROR: current user(uid=%u) that starts observer is not the same with the original one(uid=%u), observer starts failed!",
              current_uid, dir_info.st_uid);
    }
  }

  return ret;
}

void print_all_thread(const char* desc, uint64_t tenant_id)
{
  MPRINT("============= [%s] begin to show unstopped thread =============", desc);
  DIR *dir = opendir("/proc/self/task");
  if (dir == NULL) {
    MPRINT("fail to print all thread");
  } else {
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
      char *tid = entry->d_name;
      if (tid[0] != '.') { // pass . and ..
        char path[256];
        sprintf(path, "/proc/self/task/%s/comm", tid);
        FILE *file = fopen(path, "r");
        if (file == NULL) {
          MPRINT("fail to print thread tid: %s", tid);
        } else {
          char thread_name[256];
          if (fgets(thread_name, sizeof(thread_name), file) != nullptr) {
            size_t len = strlen(thread_name);
            if (len > 0 && thread_name[len - 1] == '\n') {
              thread_name[len - 1] = '\0';
            }
            if (!is_server_tenant(tenant_id)) {
              char tenant_id_str[20];
              snprintf(tenant_id_str, sizeof(tenant_id_str), "T%lu_", tenant_id);
              if (0 == strncmp(thread_name, tenant_id_str, strlen(tenant_id_str))) {
                MPRINT("[CHECK_KILL_GRACEFULLY][T%lu][%s] detect unstopped thread, tid: %s, name: %s", tenant_id, desc, tid, thread_name);
              }
            } else {
              MPRINT("[CHECK_KILL_GRACEFULLY][%s] detect unstopped thread, tid: %s, name: %s", desc, tid, thread_name);
            }
          }
          fclose(file);
        }
      }
    }
  }
  closedir(dir);
  MPRINT("============= [%s] finish to show unstopped thread =============", desc);
}

int inner_main(int argc, char *argv[])
{
  // temporarily unlimited memory before init config
  set_memory_limit(INT_MAX64);

#ifdef ENABLE_SANITY
  backtrace_symbolize_func = oceanbase::common::backtrace_symbolize;
#endif
  if (0 != pthread_getname_np(pthread_self(), ob_get_tname(), OB_THREAD_NAME_BUF_LEN)) {
    snprintf(ob_get_tname(), OB_THREAD_NAME_BUF_LEN, "observer");
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
  static const int  LOG_FILE_SIZE             = 256 * 1024 * 1024;
  const char *const LOG_FILE_NAME             = "log/observer.log";
  const char *const PID_FILE_NAME             = "run/observer.pid";
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

  opts->log_level_ = OB_LOG_LEVEL_WARN;
  if (FAILEDx(parse_args(argc, argv, *opts))) {
  }

  if (OB_FAIL(ret)) {
  } else if (opts->initialize_) {
    if (OB_FAIL(init_deploy_env(*opts))) {
      MPRINT("Failed to initialize deploy environment.");
    }
  }

  if (OB_FAIL(ret)) {
  } else if (0 != chdir(opts->base_dir_.ptr())) {
    ret = OB_ERR_UNEXPECTED;
    MPRINT("Failed to change working directory to base dir. path='%s', system error='%s'",
      opts->base_dir_.ptr(), strerror(errno));
  } else {
    MPRINT("Change working directory to base dir. path='%s'", opts->base_dir_.ptr());
    if (opts->initialize_) {
      MPRINT("Start to initialize observer, please wait...");
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(check_uid_before_start(CONF_DIR))) {
    MPRINT("Fail check_uid_before_start, please use the initial user to start observer!");
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(PID_DIR))) {
    MPRINT("create pid dir fail: ./run/");
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(LOG_DIR))) {
    MPRINT("create log dir fail: ./log/");
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(CONF_DIR))) {
    MPRINT("create log dir fail: ./etc/");
  } else if (OB_FAIL(ObEncryptionUtil::init_ssl_malloc())) {
    MPRINT("failed to init crypto malloc");
  } else if (!opts->nodaemon_) {
    MPRINT("Start observer server as a daemon.");
    if (OB_FAIL(start_daemon(PID_FILE_NAME))) {
      MPRINT("Start observer server as a daemon failed.");
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    ObCurTraceId::get_trace_id()->set("Y0-0000000000000001-0-0");
    CURLcode curl_code = curl_global_init(CURL_GLOBAL_ALL);
    OB_ASSERT(CURLE_OK == curl_code);

    const char *syslog_file_info = ObServerUtils::build_syslog_file_info(ObAddr());
    OB_LOGGER.set_log_level(opts->log_level_);
    OB_LOGGER.set_max_file_size(LOG_FILE_SIZE);
    OB_LOGGER.set_new_file_info(syslog_file_info);
    OB_LOGGER.set_file_name(LOG_FILE_NAME, opts->initialize_/*no_redirect_flag*/);
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
      LOG_INFO("observer starts", "observer_version", PACKAGE_STRING);
      // to speed up bootstrap phase, need set election INIT TS
      // to count election keep silence time as soon as possible after observer process started
      ATOMIC_STORE(&palf::election::INIT_TS, palf::election::get_monotonic_ts());
      if (OB_FAIL(observer.init(*opts, log_cfg))) {
        LOG_ERROR("observer init fail", K(ret));
      }
      OB_DELETE(ObServerOptions, mem_attr, opts);
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(observer.start(embed_mode))) {
        LOG_ERROR("observer start fail", K(ret));
      } else if (initialize) {
        MPRINT("observer initialized successfully, you can start observer server now");
        _exit(0);
      } else if (OB_FAIL(observer.wait())) {
        LOG_ERROR("observer wait fail", K(ret));
      }

      if (OB_FAIL(ret)) {
        if (initialize) {
          MPRINT("observer start failed. Please check the log file for details.");
        }
        _exit(1);
      }
      print_all_thread("BEFORE_DESTROY", OB_SERVER_TENANT_ID);
      observer.destroy();
    }
    curl_global_cleanup();
    unlink(PID_FILE_NAME);
  }

  LOG_INFO("observer exits", "observer_version", PACKAGE_STRING);
  print_all_thread("AFTER_DESTROY", OB_SERVER_TENANT_ID);
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
