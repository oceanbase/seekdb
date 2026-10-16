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

#include <cstdlib>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_version.h"

#define MPRINT(format, ...) do { if (!quiet_parser_) fprintf(stderr, format "\n", ##__VA_ARGS__); } while (0)

namespace oceanbase {
namespace observer {

static int get_executable_name(ObSqlString &exe_name)
{
  int ret = OB_SUCCESS;
  char buf[4096] = {0};
#ifdef _WIN32
  DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf) - 1);
  if (len == 0 || len >= sizeof(buf)) {
    ret = OB_ERROR;
  } else {
    const char *last_slash = strrchr(buf, '\\');
    if (nullptr == last_slash) {
      last_slash = strrchr(buf, '/');
    }
    if (nullptr == last_slash) {
      last_slash = buf;
    } else {
      last_slash++;
    }
    if (OB_FAIL(exe_name.assign(last_slash))) {
    }
  }
#else
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (-1 == len) {
    ret = OB_ERROR;
    LOG_WARN("fail to get self exe path", KCSTRING(strerror(errno)));
  } else {
    buf[len] = '\0';
    const char *last_slash = strrchr(buf, '/');
    if (nullptr == last_slash) {
      last_slash = buf;
    } else {
      last_slash++;
    }
    if (OB_FAIL(exe_name.assign(last_slash))) {
    }
  }
#endif
  return ret;
}

/**
 * Command line option enumeration
 * Starting from 1000 to avoid conflicts with getopt_long short option characters
 * Short options use ASCII character values (e.g. 'P'=80, 'V'=86, 'h'=104, '6'=54)
 * Long options use values above 1000 to distinguish them
 */
enum ObCommandOption {
  COMMAND_OPTION_INITIALIZE = 1000,
  COMMAND_OPTION_VARIABLE,
  COMMAND_OPTION_NODAEMON,
  COMMAND_OPTION_EMBEDDED,
  COMMAND_OPTION_BASE_DIR,
  COMMAND_OPTION_DATA_DIR,
  COMMAND_OPTION_REDO_DIR,
  COMMAND_OPTION_LOG_LEVEL,
  COMMAND_OPTION_PARAMETER,
  COMMAND_OPTION_ROLE,
#ifdef _WIN32
  COMMAND_OPTION_INSTALL_SERVICE,
  COMMAND_OPTION_REMOVE_SERVICE,
  COMMAND_OPTION_SERVICE,
#endif
};

/**
 * Split string into key and value by '='
 */
int ObCommandLineParser::split_key_value(const ObString &str, ObString &key, ObString &value) const
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

int ObCommandLineParser::append_key_value(const char *value, ObServerOptions::KeyValueArray &array) const
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

int ObCommandLineParser::handle_tilde(ObSqlString &dir) const
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
      MPRINT("Initialize option is deprecated, please start seekdb directly.");
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
        opts.log_level_specified_ = true;
        if (OB_FAIL(OB_LOGGER.level_str2int(value, opts.log_level_))) {
          MPRINT("Invalid log level. Back to default log level.");
          ret = OB_SUCCESS;
          opts.log_level_ = DEFAULT_LOG_LEVEL;
        }
      }
      break;
    }
    case COMMAND_OPTION_PARAMETER: { // parameter
      ret = append_key_value(value, opts.parameters_);
      break;
    }
    case COMMAND_OPTION_ROLE: { // role
      if (nullptr == value) {
        ret = OB_INVALID_ARGUMENT;
        MPRINT("Invalid argument, the value should not be empty of 'role'");
      } else if (0 != ObString(value).case_compare("PRIMARY")
          && 0 != ObString(value).case_compare("STANDBY")) {
        ret = OB_NOT_SUPPORTED;
        MPRINT("Unsupported role '%s': supported roles are PRIMARY and STANDBY", value);
      } else {
        opts.role_.assign(value);
      }
      break;
    }
    case COMMAND_OPTION_EMBEDDED: { // embedded
      opts.embedded_ = true;
      break;
    }
#ifdef _WIN32
    case COMMAND_OPTION_INSTALL_SERVICE: {
      opts.install_service_ = true;
      break;
    }
    case COMMAND_OPTION_REMOVE_SERVICE: {
      opts.remove_service_ = true;
      break;
    }
    case COMMAND_OPTION_SERVICE: {
      opts.run_as_service_ = true;
      opts.nodaemon_ = true;
      break;
    }
#endif
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

int ObCommandLineParser::parse_args(int argc, const char *const argv[], ObServerOptions& opts, bool command_line)
{
  int ret = OB_SUCCESS;

  constexpr int no_argument = 0;
  constexpr int required_argument = 1;
  struct option { const char *name; int has_arg; int *flag; int val; };
  // Define long options
  constexpr option long_options[] = {
    {"initialize", no_argument,       0, COMMAND_OPTION_INITIALIZE}, // TODO wangyunlai.wyl remove me before 2025-12-01
    {"variable",   required_argument, 0, COMMAND_OPTION_VARIABLE},
    {"port",       required_argument, 0, 'P'},
    {"nodaemon",   no_argument,       0, COMMAND_OPTION_NODAEMON},
    {"use-ipv6",   no_argument,       0, '6'},
    {"base-dir",   required_argument, 0, COMMAND_OPTION_BASE_DIR},
    {"data-dir",   required_argument, 0, COMMAND_OPTION_DATA_DIR},
    {"redo-dir",   required_argument, 0, COMMAND_OPTION_REDO_DIR},
    {"log-level",  required_argument, 0, COMMAND_OPTION_LOG_LEVEL},
    {"parameter",  required_argument, 0, COMMAND_OPTION_PARAMETER},
    {"role", required_argument, 0, COMMAND_OPTION_ROLE},
    {"embedded",   no_argument,       0, COMMAND_OPTION_EMBEDDED},
#ifdef _WIN32
    {"install-service", no_argument, 0, COMMAND_OPTION_INSTALL_SERVICE},
    {"remove-service",  no_argument, 0, COMMAND_OPTION_REMOVE_SERVICE},
    {"service",         no_argument, 0, COMMAND_OPTION_SERVICE},
#endif
    {"version",    no_argument,       0, 'V'},
    {"help",       no_argument,       0, 'h'},
    {0, 0, 0, 0}
  };

  // Define short options string
  constexpr char short_options[] = "P:Vh6";

  quiet_parser_ = !command_line;
  help_requested_ = false;
  version_requested_ = false;
  if (argc < 0 || (argc > 0 && argv == nullptr)) {
    return OB_INVALID_ARGUMENT;
  }
  // Keep parsing local to this call: getopt's process-global state belongs to
  // the host, and must not be changed by an embedded library.
  for (int i = 0; OB_SUCC(ret) && i < argc; ++i) {
    const char *arg = argv[i];
    if (arg == nullptr || arg[0] != '-' || arg[1] == '\0') {
      ret = OB_INVALID_ARGUMENT;
    } else if (std::strcmp(arg, "--") == 0) {
      if (i + 1 != argc) { ret = OB_INVALID_ARGUMENT; }
      break;
    } else if (arg[1] == '-') {
      const char *name = arg + 2;
      const char *equals = std::strchr(name, '=');
      const size_t length = equals == nullptr ? std::strlen(name) : equals - name;
      const option *found = nullptr;
      for (const option *opt = long_options; opt->name != nullptr; ++opt) {
        if (std::strlen(opt->name) == length && std::strncmp(name, opt->name, length) == 0) {
          found = opt;
          break;
        }
      }
      const char *value = equals == nullptr ? nullptr : equals + 1;
      if (found == nullptr || (found->has_arg == no_argument && equals != nullptr)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        if (found->has_arg == required_argument && value == nullptr) {
          value = ++i < argc ? argv[i] : nullptr;
        }
        if (found->has_arg == required_argument && value == nullptr) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          ret = handle_option(found->val, value, opts);
        }
      }
    } else {
      for (const char *short_arg = arg + 1; OB_SUCC(ret) && *short_arg != '\0'; ++short_arg) {
        const char *spec = std::strchr(short_options, *short_arg);
        if (spec == nullptr || *short_arg == ':') {
          ret = OB_INVALID_ARGUMENT;
        } else if (spec[1] == ':') {
          const char *value = short_arg[1] != '\0' ? short_arg + 1 : (++i < argc ? argv[i] : nullptr);
          ret = value == nullptr ? OB_INVALID_ARGUMENT : handle_option(*short_arg, value, opts);
          break;
        } else {
          ret = handle_option(*short_arg, nullptr, opts);
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    return ret;
  } else if (help_requested_ || version_requested_) {
    if (command_line) {
      if (help_requested_) { print_help(); } else { print_version(); }
      return OB_ITER_END;
    }
    return OB_INVALID_ARGUMENT;
  }

  // Set default values
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

  return ret;
}

void ObCommandLineParser::print_help() const
{
  ObSqlString exe_name;
  (void) get_executable_name(exe_name);

  MPRINT("Copyright (c) 2011-present OceanBase Inc.");
  MPRINT();
  MPRINT("Usage: %s [OPTIONS]\n", exe_name.ptr());
  MPRINT("Options:");
  MPRINT("  --nodaemon                      whether to not run as a daemon");
  MPRINT("  --port, -P <port>               the port, default is 2881");
  MPRINT("  --use-ipv6, -6                  whether to use ipv6");
  MPRINT("  --base-dir <dir>                The base/work directory which seekdb process will run in(default: current directory). ");
  MPRINT("                                  NOTE: You must specify this option if you will start seekdb at other directory.");
  MPRINT("  --data-dir <dir>                The data directory which seekdb will store data in. Default is ${base-dir}/store in initialize mode.");
  MPRINT("  --redo-dir <dir>                The redo log directory which seekdb will store redo log in. Default is ${data-dir}/redo in initialize mode.");
  MPRINT("  --log-level <level>             The server log level. Can be one of [ERROR, WARN, INFO, EDIAG, WDIAG, TRACE, DEBUG]");
  MPRINT("  --variable <key=value>          system variables, format: key=value. Note: This takes effect only during the initial startup. Can be specified multiple times.");
  MPRINT("  --parameter <key=value>         system parameters, format: key=value. Can be specified multiple times.");
  MPRINT("  --role <role>                   server role: PRIMARY (default) or STANDBY");
  MPRINT("  --embedded                      run seekdb in embedded mode (default: off)");
  MPRINT("  --version, -V                   show version message and exit");
  MPRINT("  --help, -h                      show this message and exit");
#ifdef _WIN32
  MPRINT();
  MPRINT("Windows Service options:");
  MPRINT("  --install-service               install seekdb as a Windows service");
  MPRINT("  --remove-service                remove the seekdb Windows service");
#endif
  MPRINT();
  MPRINT();
  MPRINT("Below are some parameters that you may be interesed in:");
  MPRINT("  --parameter datafile_size=<size>          data file initial size (e.g. 20G)");
  MPRINT("  --parameter datafile_maxsize=<size>       data file maximum size (e.g. 50G), can be expanded when needed");
  MPRINT("  --parameter log_disk_size=<size>          log/redo/clog disk size (e.g. 10G)");
  MPRINT("      The default unit is byte. You can use suffixes: K, M, G, T (e.g. 1024M, 20G)");
  MPRINT();
}

void ObCommandLineParser::print_version() const
{
  ObSqlString exe_name;
  (void) get_executable_name(exe_name);
  MPRINT("%s (%s %s %s)\n", exe_name.ptr(), OB_OCEANBASE_NAME, OB_SEEKDB_NAME, PACKAGE_VERSION);
  MPRINT("REVISION: %s", build_version());
  MPRINT("BUILD_BRANCH: %s", build_branch());
  MPRINT("BUILD_TIME: %s %s", build_date(), build_time());
  MPRINT("BUILD_FLAGS: %s", build_flags());
  MPRINT("BUILD_INFO: %s\n", build_info());
  MPRINT("Copyright (c) 2011-present OceanBase Inc.");
  MPRINT();
}

} // namespace observer
} // namespace oceanbase
