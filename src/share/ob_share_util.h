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

#ifndef OCEANBASE_SHARE_OB_SHARE_UTIL_H_
#define OCEANBASE_SHARE_OB_SHARE_UTIL_H_
#include "common/ob_timeout_ctx.h"
#include "share/ob_define.h"
#include "share/ob_id_generator.h"
#include "share/scn.h"
#include "share/ob_server_role.h"
namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace share
{
namespace schema
{
class ObServerRuntimeSchema;
}
typedef ObFixedLengthString<common::OB_SERVER_VERSION_LENGTH> ObBuildVersion;

class ObShareUtil
{
public:
  static int get_server_ip(const common::ObAddr &self_addr,
                           common::ObIAllocator &allocator,
                           common::ObString &ip_string);
  // priority to set timeout_ctx: ctx > worker > default_timeout
  static int set_default_timeout_ctx(common::ObTimeoutCtx &ctx, const int64_t default_timeout);
  // moved up from rootserver::ObRootUtils(body uses only GCONF.rpc_timeout + set_default_timeout_ctx, share-clean)
  static int get_rs_default_timeout_ctx(common::ObTimeoutCtx &ctx);
  // priority to get timeout: ctx > worker > default_timeout
  static int get_abs_timeout(const int64_t default_timeout, int64_t &abs_timeout);
  static int get_ctx_timeout(const int64_t default_timeout, int64_t &timeout);

  static int fetch_current_data_version(
             common::ObISQLClient &client,
             uint64_t &data_version);

  // get ora_rowscn from one row
  // @params[in]: sql, the sql should be "select ORA_ROWSCN from xxx", where count() is 1
  // @params[out]: the ORA_ROWSCN
  static int get_ora_rowscn(
    common::ObISQLClient &client,
    const ObSqlString &sql,
    SCN &ora_rowscn);
  static int get_server_role(ObServerRole::Role &server_role);
  static int check_if_server_role_is_primary(bool &is_primary);
  static int check_if_server_role_is_standby(bool &is_standby);
  static int get_server_role_state(ObServerRole &server_role);
  static int check_if_server_role_state_is_primary(bool &is_primary);
  static int check_if_server_role_state_is_standby(bool &is_standby);
  // get_sys_ls_readable_scn has been demoted to storage::free function(see end of file storage ns)
  // check_clog_disk_full_or_hang has been demoted to logservice::free function
  static int gen_default_server_runtime_schema(
      common::ObISQLClient &sql_client,
      schema::ObServerRuntimeSchema &runtime_schema);
  static int is_primary_server(bool &is_primary);
};
}//end namespace share
}//end namespace oceanbase
namespace oceanbase { namespace storage {
} }

#endif //OCEANBASE_SHARE_OB_SHARE_UTIL_H_
