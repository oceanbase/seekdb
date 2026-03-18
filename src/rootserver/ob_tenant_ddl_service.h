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

#ifndef _OCEANBASE_ROOTSERVER_OB_TENANT_DDL_SERVICE_H_
#define _OCEANBASE_ROOTSERVER_OB_TENANT_DDL_SERVICE_H_ 1

#include "share/ob_rpc_struct.h"
#include "share/config/ob_server_config.h"
#include "rootserver/ob_rs_async_rpc_proxy.h"

namespace oceanbase 
{
namespace common
{
using ObAddrIArray = ObIArray<ObAddr>;
using ObAddrArray = ObSEArray<ObAddr, 3>;
class ObServerConfig;
}
namespace share
{
namespace schema
{
class ObDDLTransController;
}
}
namespace rootserver
{
class ObDDLService;
struct ObSysStat
{
  struct Item;
  typedef common::ObDList<Item> ItemList;

  struct Item : public common::ObDLinkBase<Item>
  {
    Item() : name_(NULL), value_(), info_(NULL) {}
    Item(ItemList &list, const char *name, const char *info);

    TO_STRING_KV("name", common::ObString(name_), K_(value), "info", common::ObString(info_));
    const char *name_;
    common::ObObj value_;
    const char *info_;
  };

  ObSysStat();

  // set values after bootstrap
  int set_initial_values(const uint64_t tenant_id);

  TO_STRING_KV(K_(item_list));

  ItemList item_list_;

  // only root tenant own
  Item ob_max_used_tenant_id_;
  Item ob_max_used_unit_config_id_;
  Item ob_max_used_resource_pool_id_;
  Item ob_max_used_unit_id_;
  Item ob_max_used_server_id_;
  Item ob_max_used_ddl_task_id_;
  Item ob_max_used_unit_group_id_;

  // all tenant own
  Item ob_max_used_normal_rowid_table_tablet_id_;
  Item ob_max_used_extended_rowid_table_tablet_id_;
  Item ob_max_used_ls_id_;
  Item ob_max_used_ls_group_id_;
  Item ob_max_used_sys_pl_object_id_;
  Item ob_max_used_object_id_;
  Item ob_max_used_rewrite_rule_version_;
};
class ObTenantDDLService
{
public:
  ObTenantDDLService() : inited_(false), stopped_(false), ddl_service_(NULL),
  rpc_proxy_(NULL), common_rpc_(NULL), schema_service_(NULL),
  ddl_trans_controller_(NULL) {}

  virtual int create_sys_tenant(const obrpc::ObCreateTenantArg &arg,
                                share::schema::ObTenantSchema &tenant_schema);
  void stop() { stopped_ = true; }
  void restart() { stopped_ = false; }
  bool is_stopped() { return stopped_; }

  int init(
      ObDDLService &ddl_service,
      obrpc::ObSrvRpcProxy &rpc_proxy,
      obrpc::ObCommonRpcProxy &common_rpc,
      common::ObMySQLProxy &sql_proxy,
      share::schema::ObMultiVersionSchemaService &schema_service);

  enum AlterLocalityOp
  {
    ALTER_LOCALITY = 0,
    ROLLBACK_ALTER_LOCALITY,
    NOP_LOCALITY_OP,
    ALTER_LOCALITY_OP_INVALID,
  };

  enum AlterLocalityType
  {
    TO_NEW_LOCALITY = 0,
    ROLLBACK_LOCALITY,
    LOCALITY_NOT_CHANGED,
    ALTER_LOCALITY_INVALID,
  };

public:
  static int gen_tenant_init_config(
             const uint64_t tenant_id,
             const uint64_t compatible_version,
             common::ObConfigPairs &tenant_config);
  static int notify_init_tenant_config(
      obrpc::ObSrvRpcProxy &rpc_proxy,
      const common::ObIArray<common::ObConfigPairs> &init_configs);

  static int replace_sys_stat(const uint64_t tenant_id,
      ObSysStat &sys_stat,
      common::ObISQLClient &trans);

private:
  int insert_tenant_merge_info_(const share::schema::ObSchemaOperationType op,
                               const share::schema::ObTenantSchema &tenant_schema,
                               common::ObMySQLTransaction &trans);
  int set_tenant_compatibility_(const obrpc::ObCreateTenantArg &arg, ObTenantSchema &tenant_schema);

  int init_tenant_sys_stats_(const uint64_t tenant_id,
      common::ObMySQLTransaction &trans);

private:
  int check_inner_stat();

  int get_tenant_schema_guard_with_version_in_inner_table(const uint64_t tenant_id,
      share::schema::ObSchemaGetterGuard &schema_guard);

  int publish_schema(const uint64_t tenant_id);
  int publish_schema(const uint64_t tenant_id, const common::ObAddrIArray &addrs);

  int init_system_variables(
      const obrpc::ObCreateTenantArg &arg,
      const ObTenantSchema &tenant_schema,
      ObSysVariableSchema &sys_variable_schema);
  int update_mysql_tenant_sys_var(
      const share::schema::ObTenantSchema &tenant_schema,
      const share::schema::ObSysVariableSchema &sys_variable,
      share::schema::ObSysParam *sys_params,
      int64_t params_capacity);
  int update_oracle_tenant_sys_var(
      const share::schema::ObTenantSchema &tenant_schema,
      const share::schema::ObSysVariableSchema &sys_variable,
      share::schema::ObSysParam *sys_params,
      int64_t params_capacity);
  int update_special_tenant_sys_var(
      const share::schema::ObSysVariableSchema &sys_variable,
      share::schema::ObSysParam *sys_params,
      int64_t params_capacity);

private:
  bool inited_;
  volatile bool stopped_;
  ObDDLService *ddl_service_;
  obrpc::ObSrvRpcProxy *rpc_proxy_;
  obrpc::ObCommonRpcProxy *common_rpc_;
  common::ObMySQLProxy *sql_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  share::schema::ObDDLTransController *ddl_trans_controller_;
};
}
}
#endif // _OCEANBASE_ROOTSERVER_OB_TENANT_DDL_SERVICE_H_
