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
#include "share/ob_define.h"
#include "share/scn.h"
#include "share/ob_tenant_role.h"
namespace oceanbase
{
namespace common
{
class ObTimeoutCtx;
class ObISQLClient;
}
namespace share
{

// available range is [start_id, end_id]
class ObIDGenerator
{
public:
  ObIDGenerator()
    : inited_(false),
      step_(0),
      start_id_(common::OB_INVALID_ID),
      end_id_(common::OB_INVALID_ID),
      current_id_(common::OB_INVALID_ID)
  {}
  ObIDGenerator(const uint64_t step)
    : inited_(false),
      step_(step),
      start_id_(common::OB_INVALID_ID),
      end_id_(common::OB_INVALID_ID),
      current_id_(common::OB_INVALID_ID)
  {}

  virtual ~ObIDGenerator() {}
  void reset();

  int init(const uint64_t step,
           const uint64_t start_id,
           const uint64_t end_id);
  int next(uint64_t &current_id);

  int get_start_id(uint64_t &start_id) const;
  int get_current_id(uint64_t &current_id) const;
  int get_end_id(uint64_t &end_id) const;
  int get_id_cnt(uint64_t &cnt) const;
  TO_STRING_KV(K_(inited), K_(step), K_(start_id), K_(end_id), K_(current_id));
protected:
  bool inited_;
  uint64_t step_;
  uint64_t start_id_;
  uint64_t end_id_;
  uint64_t current_id_;
};

class ObShareUtil
{
public:
  // priority to set timeout_ctx: ctx > worker > default_timeout
  static int set_default_timeout_ctx(common::ObTimeoutCtx &ctx, const int64_t default_timeout);
  // priority to get timeout: ctx > worker > default_timeout
  static int get_abs_timeout(const int64_t default_timeout, int64_t &abs_timeout);
  static int get_ctx_timeout(const int64_t default_timeout, int64_t &timeout);

  // generate the count of arb replica of a log stream
  // @params[in]  tenant_id, which tenant to check
  // @params[in]  ls_id, which log stream to check
  // @params[out] arb_replica_num, the result
  static int generate_arb_replica_num(
      const uint64_t tenant_id,
      const ObLSID &ls_id,
      int64_t &arb_replica_num);

  static int fetch_current_data_version(
             common::ObISQLClient &client,
             const uint64_t tenant_id,
             uint64_t &data_version);

  // parse GCONF.all_server_list
  // @params[in]  excluded_server_list, servers which will not be included in the output
  // @params[out] config_all_server_list, servers in (GCONF.all_server_list - excluded_server_list)
  static int parse_all_server_list(
    const ObArray<ObAddr> &excluded_server_list,
    ObArray<ObAddr> &config_all_server_list);
  // get ora_rowscn from one row
  // @params[in]: tenant_id, the table owner
  // @params[in]: sql, the sql should be "select ORA_ROWSCN from xxx", where count() is 1
  // @params[out]: the ORA_ROWSCN
  static int get_ora_rowscn(
    common::ObISQLClient &client,
    const uint64_t tenant_id,
    const ObSqlString &sql,
    SCN &ora_rowscn);
  static bool is_tenant_enable_rebalance(const uint64_t tenant_id);
  static bool is_tenant_enable_transfer(const uint64_t tenant_id);
  static int mtl_get_tenant_role(const uint64_t tenant_id, ObTenantRole::Role &tenant_role);
  static int mtl_check_if_tenant_role_is_primary(const uint64_t tenant_id, bool &is_primary);
  static int mtl_check_if_tenant_role_is_standby(const uint64_t tenant_id, bool &is_standby);
  static int table_get_tenant_role(const uint64_t tenant_id, ObTenantRole &tenant_role);
  static int table_check_if_tenant_role_is_primary(const uint64_t tenant_id, bool &is_primary);
  static int table_check_if_tenant_role_is_standby(const uint64_t tenant_id, bool &is_standby);
  static const char *replica_type_to_string(const ObReplicaType type);
  static ObReplicaType string_to_replica_type(const char *str);
  static ObReplicaType string_to_replica_type(const ObString &str);
  static inline uint64_t compute_server_index(uint64_t server_id) {
    return server_id % (MAX_SERVER_COUNT + 1);
  }
};
}//end namespace share
}//end namespace oceanbase
#endif //OCEANBASE_SHARE_OB_SHARE_UTIL_H_
