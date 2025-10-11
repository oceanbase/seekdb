/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

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
  {"initialize", no_argument,       0, COMMAND_OPTION_INITIALIZE},
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
  value = str;
  key = value.split_on('=');
  if (key.empty()) {
    ret = OB_INVALID_ARGUMENT;
    MPRINT("Invalid variable. Variable should be in the format of key=value, but got: '%.*s'", str.length(), str.ptr());
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
      MPRINT("[Maybe Memory Error] split_key_value failed, ret=%d", ret);
    } else if (tmp_key.empty()) {
      ret = OB_INVALID_ARGUMENT;
      MPRINT("[Maybe Memory Error] Invalid argument, should be in the format of key=value, but got '%s'", value);
    } else if (OB_FAIL(array.push_back(std::make_pair(tmp_key, tmp_value)))) {
      MPRINT("[Maybe Memory Error] push_back to parameters_ failed, ret=%d", ret);
    }
  }
  return ret;
}

/**
 * 将相对路径转换为绝对路径
 * @note 需要确保路径存在
 */
static int to_absolute_path(ObSqlString &dir)
{
  int ret = OB_SUCCESS;
  if (!dir.empty() && dir.ptr()[0] != '\0' && dir.ptr()[0] != '/') {
    char real_path[OB_MAX_FILE_NAME_LENGTH] = {0};
    if (NULL == realpath(dir.ptr(), real_path)) {
      MPRINT("Failed to get absolute path for %.*s, system error=%s", dir.length(), dir.ptr(), strerror(errno));
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(dir.assign(real_path))) {
      MPRINT("[Maybe Memory Error] Failed to assign absolute path. Please try again.");
    }
  }
  return ret;
}

int ObCommandLineParser::handle_option(int option, const char* value, ObServerOptions& opts)
{
  int ret = OB_SUCCESS;

  switch (option) {
    case COMMAND_OPTION_INITIALIZE: { // initialize
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
      opts.devname_ = value;
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
          MPRINT("malformed log level, candicates are: "
            "    ERROR,USER_ERR,WARN,INFO,TRACE,DEBUG");
          MPRINT("!! Back to INFO log level.");
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
  } else if (!opts.initialize_ && !opts.variables_.empty()) {
    opts.variables_.reset();
    MPRINT("Variable is only available in initialize mode, reset it.");
  }

  // 设置nodaemon逻辑：初始化模式下，默认不以daemon方式运行
  if (OB_FAIL(ret)) {
  } else if (opts.initialize_ && !opts.nodaemon_) {
    opts.nodaemon_ = true;
    MPRINT("Initialize mode, execute not as a daemon.");
  }

  // 初始化时，如果没有指定data_dir和redo_dir，需要设置默认值
  if (OB_FAIL(ret) || !opts.initialize_) {
  } else if (opts.data_dir_.empty() &&
        OB_FAIL(opts.data_dir_.append_fmt("%s/store", opts.base_dir_.ptr()))) {
        MPRINT("[Maybe Memory Error] Failed to assign data dir. Please try again.");
  } else if (opts.redo_dir_.empty() &&
      OB_FAIL(opts.redo_dir_.append_fmt("%s/redo", opts.data_dir_.ptr()))) {
        MPRINT("[Maybe Memory Error] Failed to assign redo dir. Please try again.");
  }

  // 创建base-dir, data-dir, redo-dir
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.base_dir_.ptr()))) {
    MPRINT("Failed to create base-dir: %s. error: %s", opts.base_dir_.ptr(), strerror(errno));
  } else if (!opts.data_dir_.empty() && OB_FAIL(FileDirectoryUtils::create_full_path(opts.data_dir_.ptr()))) {
    MPRINT("Failed to create data-dir: %s. error: %s", opts.data_dir_.ptr(), strerror(errno));
  } else if (!opts.redo_dir_.empty() && OB_FAIL(FileDirectoryUtils::create_full_path(opts.redo_dir_.ptr()))) {
    MPRINT("Failed to create redo-dir: %s. error: %s", opts.redo_dir_.ptr(), strerror(errno));
  }

  // 如果data_dir_和redo_dir_不是空并且不是绝对路径，需要切换成绝对路径
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(to_absolute_path(opts.base_dir_))) {
  } else if (OB_FAIL(to_absolute_path(opts.data_dir_))) {
    // error already printed
  } else if (OB_FAIL(to_absolute_path(opts.redo_dir_))) {
    // error already printed
  } else {
    MPRINT("Starting OceanBase with:");
    MPRINT("    base-dir=%s", opts.base_dir_.ptr());
    if (!opts.data_dir_.empty()) {
      MPRINT("    data-dir=%s", opts.data_dir_.ptr());
    }
    if (!opts.redo_dir_.empty()) {
      MPRINT("    redo-dir=%s", opts.redo_dir_.ptr());
    }
  }

  return ret;
}

void ObCommandLineParser::print_help() const
{
  MPRINT("%s", COPYRIGHT);
  MPRINT();
  MPRINT("Usage: observer [OPTIONS]\n");
  MPRINT("Options:");
  MPRINT("  --initialize                    whether to perform initialization");
  MPRINT("  --variable <key=value>          system variables, format: key=value. Note: Only available in initialize mode. Can be specified multiple times.");
  MPRINT("  --port, -P <port>               the port, default is 2881");
  MPRINT("  --nodaemon                      whether to not run as a daemon");
  MPRINT("  --use-ipv6, -6                  whether to use ipv6");
  MPRINT("  --devname <name>                The name of network adapter");
  MPRINT("  --base-dir <dir>                The base directory which oceanbase process will run in. (default: current directory)");
  MPRINT("  --data-dir <dir>                The data directory which oceanbase will store data in. Default is ${base-dir}/store in initialize mode.");
  MPRINT("  --redo-dir <dir>                The redo log directory which oceanbase will store redo log in. Default is ${data-dir}/redo in initialize mode.");
  MPRINT("  --log-level <level>             The server log level");
  MPRINT("  --parameter <key=value>         system parameters, format: key=value. Can be specified multiple times.");
  MPRINT("  --version, -V                   show version message and exit");
  MPRINT("  --help, -h                      show this message and exit");
  MPRINT();
}

void ObCommandLineParser::print_version()
{
  // 这里应该调用原有的print_version函数
  // 为了简化，我们直接输出版本信息
#ifndef ENABLE_SANITY
  const char *extra_flags = "";
#else
  const char *extra_flags = "|Sanity";
#endif
  MPRINT("observer (%s %s)\n", "OceanBase_Lite", PACKAGE_VERSION);
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
