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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_TRIGGER_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_TRIGGER_RESOLVER_

#include "sql/resolver/ob_stmt_resolver.h"
#include "sql/resolver/ddl/ob_trigger_stmt.h"
namespace oceanbase
{
namespace obrpc
{
struct ObCreateTriggerArg;
}
namespace share
{
namespace schema
{
class ObTriggerInfo;
}
}

namespace sql
{
class ObTriggerResolver : public ObStmtResolver
{
public:
  explicit ObTriggerResolver(ObResolverParams &params)
    : ObStmtResolver(params)
  {}
  virtual ~ObTriggerResolver()
  {}
  virtual int resolve(const ParseNode &parse_tree);
  static int analyze_trigger(ObSchemaGetterGuard &schema_guard,
                             ObSQLSessionInfo *session_info,
                             ObMySQLProxy *sql_proxy,
                             ObIAllocator &allocator,
                             const ObTriggerInfo &trigger_info,
                             const ObString &db_name,
                             ObIArray<ObDependencyInfo> &dep_infos,
                             bool is_alter_compile);
  static int resolve_has_auto_trans(const ParseNode &declare_node,
                                    share::schema::ObTriggerInfo &trigger_info);       
private:
  int resolve_create_trigger_stmt(const ParseNode &parse_node,
                                  obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_drop_trigger_stmt(const ParseNode &parse_node,
                                obrpc::ObDropTriggerArg &trigger_arg);
  int resolve_alter_trigger_stmt(const ParseNode &parse_node,
                                 obrpc::ObAlterTriggerArg &trigger_arg);
  int resolve_trigger_source(const ParseNode &parse_node,
                             obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_simple_dml_trigger(const ParseNode &parse_node,
                                 obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_instead_dml_trigger(const ParseNode &parse_node,
                                  obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_compound_dml_trigger(const ParseNode &parse_node,
                                   obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_compound_timing_point(const ParseNode &parse_node,
                                    obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_dml_event_option(const ParseNode &parse_node,
                               obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_reference_names(const ParseNode *parse_node,
                              obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_trigger_status(int16_t enable_or_disable,
                             obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_when_condition(const ParseNode *parse_node,
                             obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_trigger_body(const ParseNode &parse_node,
                           obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_compound_trigger_body(const ParseNode &parse_node,
                                    obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_dml_event_list(const ParseNode &parse_node,
                             obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_sp_definer(const ParseNode *parse_node,
                         obrpc::ObCreateTriggerArg &trigger_arg);
  int resolve_schema_name(const ParseNode &parse_node,
                          common::ObString &database_name,
                          common::ObString &schema_name);
  int resolve_alter_clause(const ParseNode &alter_clause,
                           share::schema::ObTriggerInfo &tg_info,
                           const ObString &db_name,
                           bool &is_set_status,
                           bool &is_alter_compile);
  int fill_package_info(share::schema::ObTriggerInfo &trigger_info);

  int resolve_base_object(obrpc::ObCreateTriggerArg &trigger_arg, bool search_public_schema);
  int resolve_order_clause(const ParseNode *parse_node, obrpc::ObCreateTriggerArg &trigger_arg);
  int get_drop_trigger_stmt_table_name(ObDropTriggerStmt *stmt);

private:
  static const common::ObString REF_OLD;
  static const common::ObString REF_NEW;
  static const common::ObString REF_PARENT;
  DISALLOW_COPY_AND_ASSIGN(ObTriggerResolver);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_RESOLVER_DDL_OB_TRIGGER_RESOLVER_

