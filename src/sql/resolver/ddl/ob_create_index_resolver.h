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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_CREATE_INDEX_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_CREATE_INDEX_RESOLVER_H_ 1
#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "share/ob_rpc_struct.h"
namespace oceanbase
{
namespace obcall
{
struct ObCreateIndexArg;
}
namespace sql
{
class ObCreateIndexStmt;
class ObCreateIndexResolver : public ObDDLResolver
{
public:
  explicit ObCreateIndexResolver(ObResolverParams &params);
  virtual ~ObCreateIndexResolver();

  virtual int resolve(const peg::Node &statement);
  ObCreateIndexStmt *get_create_index_stmt() { return static_cast<ObCreateIndexStmt*>(stmt_); };
protected:
  int resolve_index_name_node(
      const peg::Node &name,
      ObCreateIndexStmt *crt_idx_stmt);
  int resolve_index_table_name_node(
      const peg::Node &relation,
      ObCreateIndexStmt *crt_idx_stmt);
  int resolve_index_column_node(
      rust::Slice<const peg::Node> columns,
      const int64_t index_keyname_value,
      rust::Slice<const peg::Node> options,
      ObCreateIndexStmt *crt_idx_stmt,
      const share::schema::ObTableSchema *tbl_schema);
  int resolve_index_option_node(
      rust::Slice<const peg::Node> options,
      ObCreateIndexStmt *crt_idx_stmt,
      const share::schema::ObTableSchema *tbl_schema,
      bool is_partitioned);
  int check_generated_partition_column(
      share::schema::ObTableSchema &index_schema);
  int add_sort_column(const obcall::ObColumnSortItem &sort_column);
  int set_table_option_to_stmt(
      const share::schema::ObTableSchema &tbl_schema,
      const uint64_t data_table_id,
      bool is_partitioned);
  int fill_session_info_into_arg(const sql::ObSQLSessionInfo *session,
                                 ObCreateIndexStmt *crt_idx_stmt);
  int add_based_udt_info(const share::schema::ObTableSchema &tbl_schema);
private:
  bool is_spec_block_size; // whether block size is specified
  DISALLOW_COPY_AND_ASSIGN(ObCreateIndexResolver);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_DDL_OB_CREATE_INDEX_RESOLVER_H_ */
