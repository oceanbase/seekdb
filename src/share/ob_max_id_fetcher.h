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

#ifndef OCEANBASE_SHARE_OB_MAX_ID_FETCHER_H_
#define OCEANBASE_SHARE_OB_MAX_ID_FETCHER_H_

#include "share/ob_define.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
class ObMySQLProxy;
class ObString;
}
namespace share
{
// represent the different max_used_xxx_id type in __all_sys_stat table
enum ObMaxIdType
{
  OB_MAX_USED_TENANT_ID_TYPE,
  OB_MAX_USED_UNIT_CONFIG_ID_TYPE,
  OB_MAX_USED_UNIT_ID_TYPE,
  OB_MAX_USED_RESOURCE_POOL_ID_TYPE,
  OB_MAX_USED_SERVER_ID_TYPE,
  OB_MAX_USED_DDL_TASK_ID_TYPE,
  OB_MAX_USED_UNIT_GROUP_ID_TYPE,
  OB_MAX_USED_NORMAL_ROWID_TABLE_TABLET_ID_TYPE, /* used for tablet_id */
  OB_MAX_USED_EXTENDED_ROWID_TABLE_TABLET_ID_TYPE,     /* used for tablet_id */
  OB_MAX_USED_LS_ID_TYPE,
  OB_MAX_USED_LS_GROUP_ID_TYPE,
  OB_MAX_USED_SYS_PL_OBJECT_ID_TYPE, /* used for sys package object id */
  OB_MAX_USED_OBJECT_ID_TYPE,        /* used for all kinds of user schema objects */
  OB_MAX_USED_LOCK_OWNER_ID_TYPE,
  OB_MAX_USED_REWRITE_RULE_VERSION_TYPE,
  OB_MAX_USED_TTL_TASK_ID_TYPE,

  /* OB_MAX_USED_TABLE_ID_TYPE ~ OB_MAX_USED_RLS_CONTEXT_ID_TYPE ObMaxIdType will be changed to OB_MAX_USED_OBJECT_ID_TYPE and won't be persisted. */
  OB_MAX_USED_TABLE_ID_TYPE,
  OB_MAX_USED_DATABASE_ID_TYPE,
  OB_MAX_USED_USER_ID_TYPE,
  OB_MAX_USED_TABLEGROUP_ID_TYPE,
  OB_MAX_USED_SEQUENCE_ID_TYPE,
  OB_MAX_USED_OUTLINE_ID_TYPE,
  OB_MAX_USED_CONSTRAINT_ID_TYPE,
  OB_MAX_USED_SYNONYM_ID_TYPE,
  OB_MAX_USED_UDF_ID_TYPE,
  OB_MAX_USED_UDT_ID_TYPE,
  OB_MAX_USED_ROUTINE_ID_TYPE,
  OB_MAX_USED_PACKAGE_ID_TYPE,
  OB_MAX_USED_KEYSTORE_ID_TYPE,
  OB_MAX_USED_MASTER_KEY_ID_TYPE,
  OB_MAX_USED_LABEL_SE_POLICY_ID_TYPE,
  OB_MAX_USED_LABEL_SE_COMPONENT_ID_TYPE,
  OB_MAX_USED_LABEL_SE_LABEL_ID_TYPE,
  OB_MAX_USED_LABEL_SE_USER_LEVEL_ID_TYPE,
  OB_MAX_USED_TABLESPACE_ID_TYPE,
  OB_MAX_USED_TRIGGER_ID_TYPE,
  OB_MAX_USED_PROFILE_ID_TYPE,
  OB_MAX_USED_AUDIT_ID_TYPE,
  OB_MAX_USED_DBLINK_ID_TYPE,
  OB_MAX_USED_DIRECTORY_ID_TYPE,
  OB_MAX_USED_CONTEXT_ID_TYPE,
  OB_MAX_USED_PARTITION_ID_TYPE,
  OB_MAX_USED_RLS_POLICY_ID_TYPE,
  OB_MAX_USED_RLS_GROUP_ID_TYPE,
  OB_MAX_USED_RLS_CONTEXT_ID_TYPE,
  /* the following ObMaxIdType will be persisted. */
  OB_MAX_USED_SERVICE_NAME_ID_TYPE,
  OB_MAX_USED_STORAGE_ID_TYPE, /* used for storage id of zone storage */
  OB_MAX_USED_STORAGE_OP_ID_TYPE, /* used for storage op id of zone storage */
  OB_MAX_USED_CATALOG_ID_TYPE,
  OB_MAX_USED_CCL_RULE_ID_TYPE,
  OB_MAX_USED_EXTERNAL_RESOURCE_ID_TYPE,  // OB_MAX_USED_EXTERNAL_RESOURCE_ID_TYPE will be changed to OB_MAX_USED_OBJECT_ID_TYPE and won't be persisted.
  OB_MAX_USED_LOCATION_ID_TYPE,
  OB_MAX_USED_AI_MODEL_ID_TYPE,
  OB_MAX_USED_AI_MODEL_ENDPOINT_ID_TYPE,
  OB_MAX_ID_TYPE,
};

class ObMaxIdFetcher
{
public:
  explicit ObMaxIdFetcher(common::ObMySQLProxy &proxy);
  explicit ObMaxIdFetcher(common::ObMySQLProxy &proxy, const int32_t group_id);
  virtual ~ObMaxIdFetcher();

  // For generate new object_ids
  int fetch_new_max_id(const uint64_t tenant_id, ObMaxIdType id_type,
                       uint64_t &max_id, const uint64_t initial = UINT64_MAX,
                       const int64_t size = 1);
  int update_server_max_id(const uint64_t max_server_id, const uint64_t next_max_server_id);
  // For generate new tablet_ids
  int fetch_new_max_ids(const uint64_t tenant_id, ObMaxIdType id_type,
                        uint64_t &max_id, uint64_t size);
  int update_max_id(common::ObISQLClient &sql_client, const uint64_t tenant_id,
                    ObMaxIdType max_id_type, const uint64_t max_id);
  // return OB_ENTRY_NOT_EXIST for %id_type not exist in __all_sys_table
  static int fetch_max_id(common::ObISQLClient &sql_client, const uint64_t tenant_id,
                   ObMaxIdType id_type, uint64_t &max_id);
  static const char *get_max_id_name(const ObMaxIdType max_id_type);
  static const char *get_max_id_info(const ObMaxIdType max_id_type);
  static int str_to_uint(const common::ObString &str, uint64_t &value);
private:
  static bool valid_max_id_type(const ObMaxIdType max_id_type)
  { return max_id_type >= 0 && max_id_type < OB_MAX_ID_TYPE; }
  // insert ignore into __all_sys_stat table
  int insert_initial_value(common::ObISQLClient &sql_client, uint64_t tenant_id,
      ObMaxIdType max_id_type, const uint64_t initial_value);

private:
  static const char *max_id_name_info_[OB_MAX_ID_TYPE][2];
  int convert_id_type(const ObMaxIdType &src, ObMaxIdType &dst);

private:
  common::ObMySQLProxy &proxy_;
  static lib::ObMutex mutex_;
  int32_t group_id_;

  DISALLOW_COPY_AND_ASSIGN(ObMaxIdFetcher);
};

}//end namespace share
}//end namespace oceanbase

#endif //OCEANBASE_SHARE_OB_MAX_ID_FETCHER_H_
