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

#ifndef OCEANBASE_ROOTSERVER_OB_SYSTEM_BOOTSTRAP_SERVICE_H_
#define OCEANBASE_ROOTSERVER_OB_SYSTEM_BOOTSTRAP_SERVICE_H_ 1

#include "share/ob_rpc_struct.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObDDLTransController;
class ObMultiVersionSchemaService;
class ObSysParam;
class ObSysVariableSchema;
}
}
namespace rootserver
{
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
  int set_initial_values();

  TO_STRING_KV(K_(item_list));

  ItemList item_list_;

  // process-wide identifiers
  Item ob_max_used_ddl_task_id_;

  // database-wide identifiers
  Item ob_max_used_tablet_id_;
  Item ob_max_used_sys_pl_object_id_;
  Item ob_max_used_object_id_;
};
class ObSystemBootstrapService
{
public:
  ObSystemBootstrapService() : inited_(false), sql_proxy_(NULL), schema_service_(NULL),
      ddl_trans_controller_(NULL) {}

  virtual int initialize_system_data();

  int init(
      common::ObMySQLProxy &sql_proxy,
      share::schema::ObMultiVersionSchemaService &schema_service);

public:
  static int replace_sys_stat(ObSysStat &sys_stat,
      common::ObISQLClient &trans);

private:
  int insert_merge_info_(common::ObMySQLTransaction &trans);

  int init_sys_stats_(common::ObMySQLTransaction &trans);

private:
  int check_inner_stat();

  int init_system_variables(share::schema::ObSysVariableSchema &sys_variable_schema);
  int update_mysql_system_variables(
      share::schema::ObSysParam *sys_params,
      int64_t params_capacity);
  int update_special_system_variables(
      const share::schema::ObSysVariableSchema &sys_variable,
      share::schema::ObSysParam *sys_params,
      int64_t params_capacity);

private:
  bool inited_;
  common::ObMySQLProxy *sql_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;
  share::schema::ObDDLTransController *ddl_trans_controller_;
};
}
}
#endif // OCEANBASE_ROOTSERVER_OB_SYSTEM_BOOTSTRAP_SERVICE_H_
