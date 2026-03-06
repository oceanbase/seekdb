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

#include "dumpsst/ob_admin_dumpsst_executor.h"
#include "io_bench/ob_admin_io_executor.h"
#include "server_tool/ob_admin_server_executor.h"
#include "backup_tool/ob_admin_dump_backup_data_executor.h"
#include "dump_enum_value/ob_admin_dump_enum_value_executor.h"
#include "log_tool/ob_admin_log_tool_executor.h"
#include "slog_tool/ob_admin_slog_executor.h"
#include "io_bench/ob_admin_io_adapter_bench.h"
#include "io_device/ob_admin_test_io_device_executor.h"
#include "object_storage_driver_quality/ob_admin_object_storage_driver_quality.h"
#include "deps/oblib/src/lib/alloc/malloc_hook.h"
#include "tools/ob_admin/sql_tool/ob_admin_uncompress_plan_executor.h"

using namespace oceanbase::common;
using namespace oceanbase::tools;

void print_usage()
{
  fprintf(stderr, "\nUsage: ob_admin io_bench\n"
         "       ob_admin slog_tool\n"
         "       ob_admin dumpsst\n"
         "       ob_admin dump_enum_value\n"
         "       ob_admin dump_backup\n"
         "       ob_admin log_tool ## './ob_admin log_tool' for more detail\n"
         "       ob_admin -h127.0.0.1 -p2883 xxx\n"
         "       ob_admin -h127.0.0.1 -p2883 (-sintl/-ssm -mbkmi/-mlocal) [command]\n"
         "              ## The options in parentheses take effect when ssl enabled.\n"
         "       ob_admin -S unix_domain_socket_path xxx");
}

int get_log_base_directory(char *log_file_name, const int64_t log_file_name_len,
                           char *log_file_rs_name, const int64_t log_file_rs_name_len)
{
  int ret = OB_SUCCESS;
  const char *log_file_name_ptr = "ob_admin.log";
  const char *log_file_rs_name_ptr = "ob_admin_rs.log";
  // the format of log file name  is 'ob_admin_log_dir' + "/" + ob_admin.log + '\0'
  const int tmp_log_file_name_len = 1 + strlen(log_file_name_ptr) + 1;
  const int tmp_log_file_rs_name_len = 1 + strlen(log_file_rs_name_ptr) + 1;

  if (NULL == log_file_name || 0 >= log_file_name_len
      || NULL == log_file_rs_name || 0 >= log_file_rs_name_len) {
    ret = OB_INVALID_ARGUMENT;
    fprintf(stderr, "\ninvalid argument, errno:%d\n", errno);
  } else {
    const char *ob_admin_log_dir = getenv("OB_ADMIN_LOG_DIR");
    int64_t ob_admin_log_dir_len = 0;
    bool is_directory = false;
    if (NULL == ob_admin_log_dir) {
      // to improve the readability of the ob_admin results, the default path of log is set to /dev/null.
      // and prompts the user to set the path manually when logging is required.
      if (OB_FAIL(databuff_printf(log_file_name, log_file_name_len, "/dev/null"))) {
        fprintf(stderr, "\nUnexpected error, databuff_printf failed\n");
      } else if (OB_FAIL(databuff_printf(log_file_rs_name, log_file_rs_name_len, "/dev/null"))) {
        fprintf(stderr, "\nUnexpected error, databuff_printf failed\n");
      } else {
        fprintf(stderr, "\033[1;33m[NOTICE]\033[m If specific log is required, you need to set the environment variable OB_ADMIN_LOG_DIR.\n"
                        "         for example: export OB_ADMIN_LOG_DIR=~/.ob_admin_log\n"
                        "         please notice that log files should not be outputted to OceanBase's clog directory.\n");
      }
    } else if (FALSE_IT(ob_admin_log_dir_len = strlen(ob_admin_log_dir))) {
    } else if (OB_FAIL(FileDirectoryUtils::is_directory(ob_admin_log_dir, is_directory))) {
      fprintf(stderr, "\nCheck is_directory failed, we will not generate ob_admin.log(errno:%d)\n", ret);
    } else if (!is_directory) {
      fprintf(stderr, "\nThe OB_ADMIN_LOG_DIR(%s) environment variable is not a directory, we will not generate ob_admin.log\n"
                      "If log files are required, please notice that log files should not be outputted to\n"
                      "OceanBase's clog directory.\n", ob_admin_log_dir);
      ret = OB_ENTRY_NOT_EXIST;
    } else if (0 != access(ob_admin_log_dir, W_OK)) {
      fprintf(stderr, "\nPermission denied, currently OB_ADMIN_LOG_DIR(%s) environment variable, we will not generate ob_admin.log\n"
                      "If log files are required, please notice that log files should not be outputted to\n"
                      "OceanBase's clog directory.\n", ob_admin_log_dir);
      ret = OB_ENTRY_NOT_EXIST;
    } else if (ob_admin_log_dir_len + tmp_log_file_name_len > log_file_name_len
               || ob_admin_log_dir_len + tmp_log_file_rs_name_len > log_file_rs_name_len) {
      fprintf(stderr, "\nLog file name length too longer, please modify log's directory via export $OB_ADMIN_LOG_DIR=xxx\n"
                      "If log files are required, please notice that log files should not be outputted to\n"
                      "OceanBase's clog directory, currently OB_ADMIN_LOG_DIR(%s) environment variable.\n", ob_admin_log_dir);
      ret = OB_SIZE_OVERFLOW;
    } else if (OB_FAIL(databuff_printf(log_file_name, log_file_name_len, "%s/%s", ob_admin_log_dir, log_file_name_ptr))) {
      fprintf(stderr, "\nUnexpected error, databuff_printf failed\n");
    } else if (OB_FAIL(databuff_printf(log_file_rs_name, log_file_rs_name_len, "%s/%s", ob_admin_log_dir, log_file_rs_name_ptr))) {
      fprintf(stderr, "\nUnexpected error, databuff_printf failed\n");
    } else {
    }
  }
  return ret;
}

int main(int argc, char *argv[])
{
#ifndef __APPLE__
  init_malloc_hook();
#endif
  int ret = 0;
  char log_file_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  char log_file_rs_name[OB_MAX_FILE_NAME_LENGTH] = {'\0'};
  if (OB_FAIL(get_log_base_directory(log_file_name, sizeof(log_file_name), log_file_rs_name, sizeof(log_file_rs_name)))) {
  } else {
    OB_LOGGER.set_log_level("INFO");
    OB_LOGGER.set_file_name(log_file_name, true);
  }
  const char *log_level = getenv("OB_ADMIN_LOG_LEVEL");
  if (NULL != log_level) {
    OB_LOGGER.set_log_level(log_level);
  }
  std::ostringstream ss;
  copy(argv, argv + argc, std::ostream_iterator<char*>(ss, " "));
  _OB_LOG(INFO, "cmd: [%s]", ss.str().c_str());

  ObAdminExecutor *executor = NULL;
  if (argc < 2) {
    print_usage();
  } else {
    if (0 == strcmp("io_bench", argv[1])) {
      executor = new ObAdminIOExecutor();
    } else if (0 == strcmp("dump_enum_value", argv[1])) {
      executor = new ObAdminDumpEnumValueExecutor();
    } else if (0 == strcmp("dumpsst", argv[1])) {
      executor = new ObAdminDumpsstExecutor();
    } else if (0 == strcmp("log_tool", argv[1])) {
      executor = new ObAdminLogExecutor();
    } else if (0 == strcmp("dump_backup", argv[1])) {
      executor = new ObAdminDumpBackupDataExecutor();
    } else if (0 == strcmp("slog_tool", argv[1])) {
      executor = new ObAdminSlogExecutor();
    } else if (0 == strcmp("io_adapter_benchmark", argv[1])) {
      executor = new ObAdminIOAdapterBenchmarkExecutor();
    } else if (0 == strcmp("test_io_device", argv[1])) {
      executor = new ObAdminTestIODeviceExecutor();
    } else if (0 == strcmp("io_driver_quality", argv[1])) {
      executor = new ObAdminObjectStorageDriverQualityExecutor();
    } else if (0 == strcmp("uncompress_plan", argv[1])) {
      executor = new ObAdminUncompressPlanExecutor();
    } else if (0 == strncmp("-h", argv[1], 2) || 0 == strncmp("-S", argv[1], 2)) {
      executor = new ObAdminServerExecutor();
    } else {
      print_usage();
    }

    if (NULL != executor) {
      if (OB_FAIL(executor->execute(argc, argv))) {
        COMMON_LOG(WARN, "Fail to executor cmd, ", K(ret));
      }
      delete executor;
    }
  }
  return ret;
}
