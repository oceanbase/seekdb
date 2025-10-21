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

#ifndef __OB_RS_RESTORE_COMMON_UTIL_H__
#define __OB_RS_RESTORE_COMMON_UTIL_H__

#include "share/ob_rpc_struct.h"
#include "share/ob_common_rpc_proxy.h"
#include "share/scn.h"

namespace oceanbase
{
namespace share
{
struct ObLSAttr;
}
namespace rootserver
{

class TenantRestoreStatus
{
public:
  enum Status : int8_t
  {
    INVALID = -1,
    IN_PROGRESS = 0,
    SUCCESS = 1,
    FAILED = 2,
  };

public:
  TenantRestoreStatus() : status_(INVALID) {}
  TenantRestoreStatus(int8_t status) : status_(status) {}

  bool is_finish() { return SUCCESS == status_ || FAILED == status_; }
  bool is_success() { return SUCCESS == status_; }
  bool is_failed() { return FAILED == status_; }

  TenantRestoreStatus operator=(const TenantRestoreStatus &other) { status_ = other.status_; return *this; }
  bool operator == (const TenantRestoreStatus &other) const { return status_ == other.status_; }
  bool operator != (const TenantRestoreStatus &other) const { return status_ != other.status_; }

  TO_STRING_KV(K_(status));
private:
  int8_t status_;
};

class ObRestoreCommonUtil
{
public:
  static int create_all_ls(common::ObMySQLProxy *sql_proxy,
             const uint64_t tenant_id,
             const share::schema::ObTenantSchema &tenant_schema,
             const common::ObIArray<share::ObLSAttr> &ls_attr_array,
             const uint64_t source_tenant_id = OB_INVALID_TENANT_ID);
  static int finish_create_ls(common::ObMySQLProxy *sql_proxy,
             const share::schema::ObTenantSchema &tenant_schema,
             const common::ObIArray<share::ObLSAttr> &ls_attr_array);
  static int try_update_tenant_role(common::ObMySQLProxy *sql_proxy,
                                    const uint64_t tenant_id,
                                    const share::SCN &restore_scn,
                                    const bool is_clone,
                                    bool &sync_satisfied);
  static int process_schema(common::ObMySQLProxy *sql_proxy,
                            const uint64_t tenant_id);
  static int check_tenant_is_existed(ObMultiVersionSchemaService *schema_service,
                                     const uint64_t tenant_id,
                                     bool &is_existed);

private:
  DISALLOW_COPY_AND_ASSIGN(ObRestoreCommonUtil);
};
}
}

#endif /* __OB_RS_RESTORE_COMMON_UTIL_H__ */
