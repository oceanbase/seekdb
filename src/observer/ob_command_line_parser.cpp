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

#include "observer/ob_command_line_parser.h"

#include <getopt.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/string/ob_sql_string.h"
#include "lib/file/file_directory_utils.h"
#include "share/ob_version.h"

#define MPRINT(format, ...) fprintf(stderr, format "\n", ##__VA_ARGS__)

namespace oceanbase {
namespace observer {

const char *COPYRIGHT  = "Copyright (c) 2011-present OceanBase Inc.";

static int get_executable_name(ObSqlString &exe_name)
{
  int ret = OB_SUCCESS;
  char buf[PATH_MAX] = {0};
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (-1 == len) {
    ret = OB_ERROR;
    LOG_WARN("fail to get self exe path", KCSTRING(strerror(errno)));
  } else {
    const char *last_slash = strrchr(buf, '/');
    if (nullptr == last_slash) {
      last_slash = buf;
    } else {
      last_slash++;
    }
    if (OB_FAIL(exe_name.assign(last_slash))) {
      LOG_WARN("fail to assign exe name to `sql string`", K(ret));
    }
  }
  return ret;
}

/**
 * 命令行选项枚举
 * 从1000开始是为了避免与getopt_long的短选项字符冲突
 * 短选项使用ASCII字符值（如'P'=80, 'V'=86, 'h'=104, '6'=54）
 * 长选项使用1000以上的数值来区分
 */
enum ObCommandOption {
  COMMAND_OPTION_INITIALIZE = 1000,
  COMMAND_OPTION_VARIABLE,
  COMMAND_OPTION_NODAEMON,
  COMMAND_OPTION_DEVNAME,
  COMMAND_OPTION_BASE_DIR,
  COMMAND_OPTION_DATA_DIR,
  COMMAND_OPTION_REDO_DIR,
  COMMAND_OPTION_LOG_LEVEL,
  COMMAND_OPTION_PARAMETER,
};

// 定义长选项
static struct option long_options[] = {
  {"initialize", no_argument,       0, COMMAND_OPTION_INITIALIZE}, // TODO wangyunlai.wyl remove me before 2025-12-01
  {"variable",   required_argument, 0, COMMAND_OPTION_VARIABLE},
  {"port",       required_argument, 0, 'P'},
  {"nodaemon",   no_argument,       0, COMMAND_OPTION_NODAEMON},
  {"use-ipv6",   no_argument,       0, '6'},
  {"devname",    required_argument, 0, COMMAND_OPTION_DEVNAME},
  {"base-dir",   required_argument, 0, COMMAND_OPTION_BASE_DIR},
  {"data-dir",   required_argument, 0, COMMAND_OPTION_DATA_DIR},
  {"redo-dir",   required_argument, 0, COMMAND_OPTION_REDO_DIR},
  {"log-level",  required_argument, 0, COMMAND_OPTION_LOG_LEVEL},
  {"parameter",  required_argument, 0, COMMAND_OPTION_PARAMETER},
  {"version",    no_argument,       0, 'V'},
  {"help",       no_argument,       0, 'h'},
  {0, 0, 0, 0}
};

// 定义短选项字符串
static const char* short_options = "P:Vh6";


/**
 * 将字符串按照 '=' 分割成 key 和 value
 */
static int split_key_value(const ObString &str, ObString &key, ObString &value)
{
  int ret = OB_SUCCESS;
  if (str.prefix_match("-")) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Expected one argument, but got '%.*s'", str.length(), str.ptr());
  } else  {
    value = str;
    key = value.split_on('=');
    if (key.empty()) {
      ret = OB_INVALID_ARGUMENT;
      MPRINT("Invalid argument. Argument should be in the format of key=value, but got: '%.*s'", str.length(), str.ptr());
    }
  }
  return ret;
}

static int append_key_value(const char *value, ObServerOptions::KeyValueArray &array)
{
  int ret = OB_SUCCESS;
  if (nullptr == value) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Invalid argument, the value should not be empty");
  } else {
    ObString value_str(value);
    ObString tmp_key, tmp_value;
    tmp_value = value_str;
    tmp_key = tmp_value.split_on('=');
    if (OB_FAIL(split_key_value(value_str, tmp_key, tmp_value))) {
      MPRINT("split_key_value failed, ret=%d", ret);
    } else if (tmp_key.empty()) {
      ret = OB_INVALID_ARGUMENT;
      MPRINT("Invalid argument, should be in the format of key=value, but got '%s'", value);
    } else if (OB_FAIL(array.push_back(std::make_pair(tmp_key, tmp_value)))) {
      MPRINT("push_back to parameters_ failed, ret=%d", ret);
    }
  }
  return ret;
}

static int handle_tilde(ObSqlString &dir)
{
  int ret = OB_SUCCESS;
  if ((dir.length() == 1 && dir.ptr()[0] == '~') || (dir.string().prefix_match("~/"))) {
    char *home_dir = getenv("HOME");
    if (nullptr == home_dir) {
      ret = OB_INVALID_ARGUMENT;
      MPRINT("Failed to get home directory, ret=%d", ret);
    } else {
      ObSqlString tmp_dir;
      if (OB_FAIL(tmp_dir.assign_fmt("%s/%s", home_dir, dir.ptr() + 1))) {
        MPRINT("Failed to assign tilde directory, ret=%d", ret);
      } else if (OB_FAIL(dir.assign(tmp_dir))) {
        MPRINT("Failed to assign tilde directory, ret=%d", ret);
      }
    }
  }
  return ret;
}

int ObCommandLineParser::handle_option(int option, const char* value, ObServerOptions& opts)
{
  int ret = OB_SUCCESS;

  switch (option) {
    case COMMAND_OPTION_INITIALIZE: { // initialize
      MPRINT("Initialize option is deprecated, please start observer directly.");
      opts.initialize_ = true;
      break;
    }
    case COMMAND_OPTION_VARIABLE: { // variable
      ret = append_key_value(value, opts.variables_);
      break;
    }
    case 'P': { // port
      if (nullptr == value) {
        ret = OB_INVALID_ARGUMENT;
        MPRINT("Invalid argument, the value should not be empty of 'port'");
      } else {
        // Instead of plain atoi, parse int with complete validation to ensure there are no trailing junk.
        char *endptr = nullptr;
        long port = strtol(value, &endptr, 10);
        // check for conversion errors or trailing non-digit characters
        if (nullptr == value || *value == '\0' || endptr == nullptr || *endptr != '\0') {
          ret = OB_INVALID_ARGUMENT;
          MPRINT("Invalid argument for port: '%s', the value must be an integer within [1, 65535]", value ? value : "(null)");
        }
        if (port <= 0 || port > 65535) {
          ret = OB_INVALID_ARGUMENT;
          MPRINT("Invalid argument, port value out of range [1, 65535], but got %s", value);
        } else {
          opts.port_ = port;
        }
      }
      break;
    }
    case COMMAND_OPTION_NODAEMON: { // nodaemon
      opts.nodaemon_ = true;
      break;
    }
    case '6': { // use-ipv6
      opts.use_ipv6_ = true;
      break;
    }
    case COMMAND_OPTION_DEVNAME: { // devname
      MPRINT("devname is deprecated, igored."); // TODO wangyunlai.wyl remove me before 2025-12-01
      break;
    }
    case COMMAND_OPTION_BASE_DIR: { // base-dir
      opts.base_dir_.assign(value);
      break;
    }
    case COMMAND_OPTION_DATA_DIR: { // data-dir
      opts.data_dir_.assign(value);
      break;
    }
    case COMMAND_OPTION_REDO_DIR: { // redo-dir
      opts.redo_dir_.assign(value);
      break;
    }
    case COMMAND_OPTION_LOG_LEVEL: { // log-level
      if (nullptr == value) {
        ret = OB_INVALID_ARGUMENT;
        MPRINT("Invalid argument, the value should not be empty");
      } else {
        if (OB_FAIL(OB_LOGGER.level_str2int(value, opts.log_level_))) {
          MPRINT("Invalid log level. Back to INFO log level.");
          ret = OB_SUCCESS;
          opts.log_level_ = OB_LOG_LEVEL_WARN;
        }
      }
      break;
    }
    case COMMAND_OPTION_PARAMETER: { // parameter
      ret = append_key_value(value, opts.parameters_);
      break;
    }
    case 'V': { // version
      version_requested_ = true;
      break;
    }
    case 'h': { // help
      help_requested_ = true;
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      MPRINT("Unknown option: %c", option);
      break;
    }
  }

  return ret;
}

int ObCommandLineParser::parse_args(int argc, char* argv[], ObServerOptions& opts)
{
  int ret = OB_SUCCESS;

  // 重置选项状态
  help_requested_ = false;
  version_requested_ = false;

  int option_index = 0;
  int c;

  // 使用getopt_long解析参数
  while (OB_SUCC(ret) && (c = getopt_long(argc, argv, short_options, long_options, &option_index)) != -1) {
    ret = handle_option(c, optarg, opts);
  }

  // 检查是否有非选项参数
  // optind是getopt_long处理完选项参数后，argv中下一个未处理参数的索引
  if (OB_SUCC(ret) && optind < argc) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Invalid argument, unexpected non-option parameter: %s", argv[optind]);
    print_help();
    exit(1);
  }

  // 处理帮助和版本请求
  if (OB_FAIL(ret)) {
  } else if (help_requested_) {
    print_help();
    exit(0);
  } else if (version_requested_) {
    print_version();
    exit(0);
  }

  // 设置默认值
  if (OB_FAIL(ret)) {
  } else if (opts.base_dir_.empty() && OB_FAIL(opts.base_dir_.assign("."))) {
  }

  // handle '~'
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(handle_tilde(opts.base_dir_))) {
    MPRINT("Failed to handle tilde in base directory, ret=%d", ret);
  } else if (OB_FAIL(handle_tilde(opts.data_dir_))) {
    MPRINT("Failed to handle tilde in data directory, ret=%d", ret);
  } else if (OB_FAIL(handle_tilde(opts.redo_dir_))) {
    MPRINT("Failed to handle tilde in redo directory, ret=%d", ret);
  }

  // handle absolute path
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.base_dir_.ptr()))) {
    MPRINT("Failed to create base dir. path='%s', system error=%s", opts.base_dir_.ptr(), strerror(errno));
  } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(opts.base_dir_))) {
    MPRINT("Failed to convert base dir to absolute path. path='%s', system error=%s", opts.base_dir_.ptr(), strerror(errno));
  } else if (opts.base_dir_.ptr()[opts.base_dir_.length() - 1] != '/' && OB_FAIL(opts.base_dir_.append("/"))) {
    MPRINT("Failed to append '/' to base dir. path='%s'", opts.base_dir_.ptr());
  }

  if (OB_FAIL(ret) || opts.data_dir_.empty()) {
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.data_dir_.ptr()))) {
    MPRINT("Failed to create data dir. path='%s', system error=%s", opts.data_dir_.ptr(), strerror(errno));
  } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(opts.data_dir_))) {
    MPRINT("Failed to convert data dir to absolute path. path='%s', system error=%s", opts.data_dir_.ptr(), strerror(errno));
  }
  if (OB_FAIL(ret) || opts.redo_dir_.empty()) {
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.redo_dir_.ptr()))) {
    MPRINT("Failed to create redo dir. path='%s', system error=%s", opts.redo_dir_.ptr(), strerror(errno));
  } else if (OB_FAIL(FileDirectoryUtils::to_absolute_path(opts.redo_dir_))) {
    MPRINT("Failed to convert redo dir to absolute path. path='%s', system error=%s", opts.redo_dir_.ptr(), strerror(errno));
  }
  if (OB_FAIL(ret)) {
  } else if (!opts.data_dir_.empty() && opts.base_dir_.string() == opts.data_dir_.string()) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Data directory cannot be the same as base directory. base_dir='%s', data_dir='%s'",
      opts.base_dir_.ptr(), opts.data_dir_.ptr());
  }
  if (OB_FAIL(ret)) {
  } else if (!opts.redo_dir_.empty() && opts.base_dir_.string() == opts.redo_dir_.string()) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Redo directory cannot be the same as base directory. base_dir='%s', redo_dir='%s'",
      opts.base_dir_.ptr(), opts.redo_dir_.ptr());
  }
  if (OB_FAIL(ret)) {
  } else if (!opts.redo_dir_.empty() && !opts.data_dir_.empty() && opts.data_dir_.string() == opts.redo_dir_.string()) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Redo directory cannot be the same as data directory. data_dir='%s', redo_dir='%s'",
      opts.data_dir_.ptr(), opts.redo_dir_.ptr());
  }

  return ret;
}

void ObCommandLineParser::print_help() const
{
  ObSqlString exe_name;
  (void) get_executable_name(exe_name);

  MPRINT("%s", COPYRIGHT);
  MPRINT();
  MPRINT("Usage: %s [OPTIONS]\n", exe_name.ptr());
  MPRINT("Options:");
  MPRINT("  --nodaemon                      whether to not run as a daemon");
  MPRINT("  --port, -P <port>               the port, default is 2881");
  MPRINT("  --use-ipv6, -6                  whether to use ipv6");
  MPRINT("  --base-dir <dir>                The base/work directory which seekdb process will run in(default: current directory). ");
  MPRINT("                                      NOTE: You must specify this option if you will start observer at other directory.");
  MPRINT("  --data-dir <dir>                The data directory which seekdb will store data in. Default is ${base-dir}/store in initialize mode.");
  MPRINT("  --redo-dir <dir>                The redo log directory which seekdb will store redo log in. Default is ${data-dir}/redo in initialize mode.");
  MPRINT("  --log-level <level>             The server log level. Can be one of [ERROR, WARN, INFO, EDIAG, WDIAG, TRACE, DEBUG]");
  MPRINT("  --variable <key=value>          system variables, format: key=value. Note: This takes effect only during the initial startup. Can be specified multiple times.");
  MPRINT("  --parameter <key=value>         system parameters, format: key=value. Can be specified multiple times.");
  MPRINT("  --version, -V                   show version message and exit");
  MPRINT("  --help, -h                      show this message and exit");
  MPRINT();
  MPRINT();
  MPRINT("Below are some parameters that you may be interesed in:");
  MPRINT("  --parameter datafile_size=<size>          data file initial size (e.g. 20G)");
  MPRINT("  --parameter datafile_maxsize=<size>       data file maximum size (e.g. 50G), can be expanded when needed");
  MPRINT("  --parameter log_disk_size=<size>          log/redo/clog disk size (e.g. 10G)");
  MPRINT("      The default unit is byte. You can use suffixes: K, M, G, T (e.g. 1024M, 20G)");
  MPRINT();
}

void ObCommandLineParser::print_version()
{
#ifndef ENABLE_SANITY
  const char *extra_flags = "";
#else
  const char *extra_flags = "|Sanity";
#endif
  ObSqlString exe_name;
  (void) get_executable_name(exe_name);
  MPRINT("%s (%s %s %s)\n", exe_name.ptr(), OB_OCEANBASE_NAME, OB_SEEKDB_NAME, PACKAGE_VERSION);
  MPRINT("REVISION: %s", build_version());
  MPRINT("BUILD_BRANCH: %s", build_branch());
  MPRINT("BUILD_TIME: %s %s", build_date(), build_time());
  MPRINT("BUILD_FLAGS: %s%s", build_flags(), extra_flags);
  MPRINT("BUILD_INFO: %s\n", build_info());
  MPRINT("%s", COPYRIGHT);
  MPRINT();
}

} // namespace observer
} // namespace oceanbase
