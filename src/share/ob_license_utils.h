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

#ifndef OCEANBASE_SHARE_OB_LICENSE_UTILS_H
#define OCEANBASE_SHARE_OB_LICENSE_UTILS_H

#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_rpc_struct.h"

namespace oceanbase
{
namespace share
{
class ObLicenseUtils
{
public:
  static int load_license(const ObString &file_path);
  static int check_dml_allowed();
  static int get_login_message(char *buf, int64_t buf_len);
  static int check_add_server_allowed(int64_t add_num, obrpc::ObAdminServerArg::AdminServerOp arg = obrpc::ObAdminServerArg::ADD);
  static int check_standby_allowed();
  static int check_olap_allowed(const int64_t tenant_id);
  static int check_for_create_tenant(int current_user_tenant_num, bool is_create_standby);
  static void clear_license_table_if_need();
  static int start_license_mgr();
  static int check_create_table_allowed(uint64_t tenant_id);
};

} // namespace share
} // namespace oceanbase

#endif
