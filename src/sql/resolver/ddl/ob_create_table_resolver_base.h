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

#ifndef _OB_CREATE_TABLE_RESOLVER_BASE_H
#define _OB_CREATE_TABLE_RESOLVER_BASE_H 1
#include "sql/resolver/ddl/ob_create_table_stmt.h"
#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "lib/container/ob_se_array.h"
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_column_schema.h"
#include "share/config/ob_server_config.h"

namespace oceanbase
{
namespace sql
{

class ObCreateTableResolverBase: public ObDDLResolver
{
public:
  explicit ObCreateTableResolverBase(ObResolverParams &params);
  virtual ~ObCreateTableResolverBase();

protected:
  //resolve partition option only used in ObCreateTableResolver now.
  int resolve_partition_option(ParseNode *node,
                               share::schema::ObTableSchema &table_schema,
                               const bool is_partition_option_node_with_opt);
  int set_table_option_to_schema(share::schema::ObTableSchema &table_schema);
  int add_primary_key_part(const ObString &column_name,
                           ObTableSchema &table_schema,
                           const int64_t cur_rowkey_size,
                           int64_t &pk_data_length,
                           ObColumnSchemaV2 *&col);

  int resolve_column_group_helper(const ParseNode *cg_node, ObTableSchema &table_schema);
  // check this type of table_schema should build column_group or not
  virtual int resolve_column_group(const ParseNode *cg_node) final;
  int resolve_table_organization(omt::ObTenantConfigGuard &tenant_config, ParseNode *node);
protected:
  uint64_t cur_column_group_id_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_CREATE_TABLE_RESOLVER_BASE_H */
