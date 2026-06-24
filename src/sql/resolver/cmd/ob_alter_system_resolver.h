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

#ifndef OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_RESOLVER_
#define OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_RESOLVER_

#include "sql/resolver/cmd/ob_system_cmd_resolver.h"
#include "sql/session/ob_sql_session_info.h" // ObSqlSessionInfo

namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace sql
{

int resolve_tenant_name(
    const ParseNode *node,
    ObString &tenant_name);

class ObSystemCmdStmt;
class ObFreezeStmt;
class ObAlterSystemResolverUtil
{
public:
  static int sanity_check(const ParseNode *parse_tree, ObItemType item_type);

  // resolve opt_ip_port
  static int resolve_server(const ParseNode *parse_tree, common::ObAddr &server);
  // resolve server string (value part of opt_ip_port)
  static int resolve_server_value(const ParseNode *parse_tree, common::ObAddr &server);
  // resolve opt_zone_desc
  static int resolve_zone(const ParseNode *parse_tree, common::ObZone &zone);
  // resolve opt_tenant_name
  static int resolve_tenant(const ParseNode *parse_tree,
                            common::ObFixedLengthString < common::OB_MAX_TENANT_NAME_LENGTH + 1 > &tenant_name);
  static int resolve_ls_id(const ParseNode *parse_tree, int64_t &ls_id);

  static int resolve_replica_type(const ParseNode *parse_tree,
                                  common::ObReplicaType &replica_type);
  static int check_compatibility_for_replica_type(const ObReplicaType replica_type);
  static int resolve_string(const ParseNode *parse_tree, common::ObString &string);
  static int resolve_relation_name(const ParseNode *parse_tree, common::ObString &string);
  // resolve opt_server_or_zone
  template <typename RPC_ARG>
  static int resolve_server_or_zone(const ParseNode *parse_tree, RPC_ARG &arg);


  static int resolve_tablet_id(const ParseNode *opt_tablet_id, ObTabletID &tablet_id);
  static int resolve_tenant(const ParseNode &tenants_node,
                            int64_t &out_count,
                            bool &affect_all,
                            bool &affect_all_user,
                            bool &affect_all_meta);
  static int get_and_verify_tenant_name(const ParseNode* tenant_name_node,
                                        uint64_t &out_tgt_id);
};

typedef common::ObFixedLengthString<common::OB_MAX_TRACE_ID_BUFFER_SIZE + 1> Task_Id;

#define DEF_SIMPLE_CMD_RESOLVER(name)                                   \
  class name : public ObSystemCmdResolver                               \
  {                                                                     \
  public:                                                               \
    name(ObResolverParams &params) : ObSystemCmdResolver(params) {}     \
    virtual ~name() {}                                                  \
    virtual int resolve(const ParseNode &parse_tree);                   \
  };

DEF_SIMPLE_CMD_RESOLVER(ObFlushCacheResolver);

DEF_SIMPLE_CMD_RESOLVER(ObFlushKVCacheResolver);


DEF_SIMPLE_CMD_RESOLVER(ObFlushIlogCacheResolver);

DEF_SIMPLE_CMD_RESOLVER(ObFlushDagWarningsResolver);


DEF_SIMPLE_CMD_RESOLVER(ObAdminZoneResolver);

DEF_SIMPLE_CMD_RESOLVER(ObAdminMergeResolver);




DEF_SIMPLE_CMD_RESOLVER(ObRefreshMemStatResolver);

DEF_SIMPLE_CMD_RESOLVER(ObWashMemFragmentationResolver);

DEF_SIMPLE_CMD_RESOLVER(ObRefreshIOCalibrationResolver);

DEF_SIMPLE_CMD_RESOLVER(ObSetTPResolver);

DEF_SIMPLE_CMD_RESOLVER(ObReloadGtsResolver);

DEF_SIMPLE_CMD_RESOLVER(ObClearMergeErrorResolver);

DEF_SIMPLE_CMD_RESOLVER(ObUpgradeVirtualSchemaResolver);



DEF_SIMPLE_CMD_RESOLVER(ObCancelTaskResolver);



DEF_SIMPLE_CMD_RESOLVER(ObAlterDiskgroupAddDiskResolver);
DEF_SIMPLE_CMD_RESOLVER(ObAlterDiskgroupDropDiskResolver);

class ObAlterSystemSetResolver : public ObSystemCmdResolver
{
public:
  ObAlterSystemSetResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {}
  virtual ~ObAlterSystemSetResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
};

class ObAlterSystemKillResolver : public ObSystemCmdResolver
{
public:
  ObAlterSystemKillResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {}
  virtual ~ObAlterSystemKillResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
};

class ObSetConfigResolver : public ObSystemCmdResolver
{
public:
  ObSetConfigResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {}
  virtual ~ObSetConfigResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
private:
  int convert_param_value(obcall::ObAdminSetConfigItem &item);
};
class ObFreezeResolver : public ObSystemCmdResolver {
public:
  ObFreezeResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {}
  virtual ~ObFreezeResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
private:
  int resolve_major_freeze_(ObFreezeStmt *freeze_stmt, ParseNode *opt_tenant_list_or_tablet_id, const ParseNode *opt_rebuild_column_group);
  int resolve_minor_freeze_(ObFreezeStmt *freeze_stmt,
                            ParseNode *opt_tenant_list_or_ls_or_tablet_id);

  int resolve_tenant_ls_tablet_(ObFreezeStmt *freeze_stmt, ParseNode *opt_tenant_list_or_ls_or_tablet_id);

};

class ObResetConfigResolver : public ObSystemCmdResolver
{
public:
  ObResetConfigResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {}
  virtual ~ObResetConfigResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
};
class ObAlterSystemResetResolver : public ObSystemCmdResolver
{
public:
  ObAlterSystemResetResolver(ObResolverParams &params) : ObSystemCmdResolver(params) {}
  virtual ~ObAlterSystemResetResolver() {}
  virtual int resolve(const ParseNode &parse_tree);
};

DEF_SIMPLE_CMD_RESOLVER(ObEnableSqlThrottleResolver);
DEF_SIMPLE_CMD_RESOLVER(ObDisableSqlThrottleResolver);
DEF_SIMPLE_CMD_RESOLVER(ObSetRegionBandwidthResolver);

#undef DEF_SIMPLE_CMD_RESOLVER

} // end namespace sql
} // end namespace oceanbase
#endif // OCEANBASE_RESOLVER_CMD_OB_ALTER_SYSTEM_RESOLVER_
