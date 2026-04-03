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
namespace schema
{
class ObTenantSchema;
}
class ObResourcePool;
class ObUnit;
typedef ObFixedLengthString<common::OB_SERVER_VERSION_LENGTH> ObBuildVersion;
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

  static int fetch_current_data_version(
             common::ObISQLClient &client,
             const uint64_t tenant_id,
             uint64_t &data_version);

  // get ora_rowscn from one row
  // @params[in]: tenant_id, the table owner
  // @params[in]: sql, the sql should be "select ORA_ROWSCN from xxx", where count() is 1
  // @params[out]: the ORA_ROWSCN
  static int get_ora_rowscn(
    common::ObISQLClient &client,
    const uint64_t tenant_id,
    const ObSqlString &sql,
    SCN &ora_rowscn);
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
  static int get_sys_ls_readable_scn(SCN &readable_scn);
  static int check_clog_disk_full_or_hang(
             bool &clog_disk_is_full,
             bool &clog_disk_is_hang);
  static int check_data_disk_health_status(
             bool &is_data_disk_healthy);
  static int get_tenant_gts(const uint64_t &tenant_id, SCN &gts_scn);
  static int gen_sys_unit(ObUnit &unit);
  static int gen_sys_resource_pool(ObResourcePool &resource_pool);
  static int gen_default_sys_tenant_schema(schema::ObTenantSchema &tenant_schema);
  static int is_primary_cluster(bool &is_primary);
};
}//end namespace share
}//end namespace oceanbase
#endif //OCEANBASE_SHARE_OB_SHARE_UTIL_H_
