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

#ifndef OB_ADMIN_EXECUTOR_H_
#define OB_ADMIN_EXECUTOR_H_
#include <stdlib.h>
#include <stdio.h>
#ifndef _WIN32
#include <getopt.h>
#else
// Windows shim for POSIX getopt_long (subset sufficient for ob_admin tools).
// Mirrors the implementation in src/observer/ob_command_line_parser.cpp, exposed
// as inline so it can be shared across all ob_admin translation units.
#include <cstring>
#define no_argument 0
#define required_argument 1
#define optional_argument 2
struct option {
  const char *name;
  int has_arg;
  int *flag;
  int val;
};
inline char *ob_admin_optarg_ptr = nullptr;
inline int ob_admin_optind_val = 1;
#define optarg ob_admin_optarg_ptr
#define optind ob_admin_optind_val
inline int getopt_long(int argc, char *const argv[], const char *short_opts,
                       const struct option *long_opts, int *long_index) {
  (void)long_index;
  ob_admin_optarg_ptr = nullptr;
  if (ob_admin_optind_val >= argc || argv[ob_admin_optind_val] == nullptr) return -1;
  char *arg = argv[ob_admin_optind_val];
  if (arg[0] != '-') return -1;
  if (arg[1] == '-') {
    arg += 2;
    for (int i = 0; long_opts[i].name != nullptr; i++) {
      const char *name = long_opts[i].name;
      size_t nlen = strlen(name);
      if (strncmp(arg, name, nlen) != 0) continue;
      if (arg[nlen] == '=') {
        if (long_opts[i].has_arg == required_argument) {
          ob_admin_optarg_ptr = arg + nlen + 1;
          ob_admin_optind_val++;
          return long_opts[i].val;
        }
      } else if (arg[nlen] == '\0') {
        if (long_opts[i].has_arg == required_argument && ob_admin_optind_val + 1 < argc) {
          ob_admin_optarg_ptr = argv[ob_admin_optind_val + 1];
          ob_admin_optind_val += 2;
          return long_opts[i].val;
        } else if (long_opts[i].has_arg == no_argument) {
          ob_admin_optind_val++;
          return long_opts[i].val;
        }
      }
    }
    return '?';
  }
  char c = arg[1];
  if (c == '\0') return -1;
  const char *p = strchr(short_opts, c);
  if (!p) return '?';
  if (p[1] == ':') {
    if (arg[2] != '\0') {
      ob_admin_optarg_ptr = arg + 2;
    } else if (ob_admin_optind_val + 1 < argc) {
      ob_admin_optarg_ptr = argv[ob_admin_optind_val + 1];
      ob_admin_optind_val++;
    } else {
      return '?';
    }
  }
  ob_admin_optind_val++;
  return (unsigned char)c;
}
// Windows shim: POSIX sleep(seconds) using Win32 Sleep(milliseconds).
// Defined here (the common ob_admin entry header) so every ob_admin TU sees
// exactly one inline definition.
#include <windows.h>
inline unsigned int sleep(unsigned int seconds) {
  Sleep(seconds * 1000);
  return 0;
}
#endif
#include "share/ob_define.h"
#include "storage/blocksstable/ob_block_sstable_struct.h"
#include "share/config/ob_config_manager.h"
#include "observer/ob_server_reload_config.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{

namespace common
{

class ObIODevice;

}
namespace tools
{
class ObAdminExecutor
{
public:
  ObAdminExecutor();
  virtual ~ObAdminExecutor();
  virtual int execute(int argc, char *argv[]) = 0;

protected:
  int prepare_io();
  int prepare_decoder();
  int load_config();
  int set_s3_url_encode_type(const char *type_str) const;
  int set_sts_credential_key(const char *sts_credential);

protected:
  share::ObTenantBase mock_server_tenant_;
  blocksstable::ObStorageEnv storage_env_;
  observer::ObServerReloadConfig reload_config_;
  common::ObConfigManager config_mgr_;
  char data_dir_[common::OB_MAX_FILE_NAME_LENGTH] = {0};
  char slog_dir_[common::OB_MAX_FILE_NAME_LENGTH] = {0};
  char clog_dir_[common::OB_MAX_FILE_NAME_LENGTH] = {0};
  char sstable_dir_[common::OB_MAX_FILE_NAME_LENGTH] = {0};
};

}
}

#endif /* OB_ADMIN_EXECUTOR_H_ */
