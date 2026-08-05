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

#ifndef OCEANBASE_SQL_OB_TABLE_EXECUTOR_
#define OCEANBASE_SQL_OB_TABLE_EXECUTOR_
#include "common/object/ob_object.h"
#include "lib/container/ob_se_array.h"
#include "common/sql_mode/ob_sql_mode.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_rpc_struct.h"
#include "sql/ob_sql_context.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
struct ObPartition;
struct ObSubPartition;
struct ObBasePartition;
class ObMultiVersionSchemaService;
}
}
namespace obcall
{
struct ObAlterTableArg;
}
namespace query
{
class ObILocalCommandService;
class ObIQueryRuntimeEnvironment;
class ObIRootCommandService;
}
namespace common
{
class ObIAllocator;
struct ObExprCtx;
class ObNewRow;
// namespace sqlclient
// {
class ObMySQLProxy;
// }
}
namespace sql
{
class ObExecContext;
class ObRawExpr;
class ObCreateTableStmt;
class ObTableStmt;
class ObExprRegexpSessionVariables;

class ObCreateTableExecutor
{
  class ObInsSQLPrinter : public ObISqlPrinter
  {
  public:
    ObInsSQLPrinter(ObCreateTableStmt *stmt,
                    ObSchemaGetterGuard *schema_guard,
                    ObObjPrintParams print_params,
                    const ParamStore *param_store,
                    bool do_osg) :
      stmt_(stmt),
      schema_guard_(schema_guard),
      print_params_(print_params),
      param_store_(param_store),
      do_osg_(do_osg)
      {}
    virtual int inner_print(char *buf, int64_t buf_len, int64_t &res_len) override;
  private:
    ObCreateTableStmt *stmt_;
    ObSchemaGetterGuard *schema_guard_;
    ObObjPrintParams print_params_;
    const ParamStore *param_store_;
    bool do_osg_;
  };
public:
  ObCreateTableExecutor();
  virtual ~ObCreateTableExecutor();
  int execute(ObExecContext &ctx, ObCreateTableStmt &stmt);
  int set_index_arg_list(ObExecContext &ctx, ObCreateTableStmt &stmt);
  int execute_ctas(ObExecContext &ctx, ObCreateTableStmt &stmt);
private:
  int prepare_stmt(ObCreateTableStmt &stmt, const ObSQLSessionInfo &my_session, ObString &create_table_name);
  int prepare_ins_arg(ObCreateTableStmt &stmt,
                      const ObSQLSessionInfo *my_session,
                      ObSchemaGetterGuard *schema_guard,
                      const ParamStore *param_store,
                      ObSqlString &ins_sql);
  int prepare_alter_arg(ObCreateTableStmt &stmt, const ObSQLSessionInfo *my_session, const ObString &create_table_name, obcall::ObAlterTableArg &alter_table_arg);
  int prepare_drop_arg(const ObCreateTableStmt &stmt, const ObSQLSessionInfo *my_session, obcall::ObTableItem &table_item, obcall::ObDropTableArg &drop_table_arg);
};

class ObAlterTableStmt;
class ObAlterTableExecutor
{
public:
  ObAlterTableExecutor();
  virtual ~ObAlterTableExecutor();
  int execute(ObExecContext &ctx, ObAlterTableStmt &stmt);
private:
  static const int64_t TIME_INTERVAL_PER_PART_US = 50 * 1000; // 50ms
  static const int64_t MAX_WAIT_CHECK_SCHEMA_VERSION_INTERVAL_US = 120LL * 1000000LL; // 120s
  static const int64_t MIN_WAIT_CHECK_SCHEMA_VERSION_INTERVAL_US = 20LL * 1000000LL; // 20s
  static const int64_t WAIT_US = 500 * 1000; // 500ms
  static const int64_t GET_ASSOCIATED_SNAPSHOT_TIMEOUT = 9000000LL; // 9s
  int check_constraint_validity(ObExecContext &ctx,
      obcall::ObAlterTableArg &alter_table_arg,
      common::ObIAllocator &allocator,
      obcall::ObAlterTableRes &res,
      ObString first_stmt,
      const bool need_modify_notnull_validate);

  int alter_table_rpc_v2(
      obcall::ObAlterTableArg &alter_table_arg,
      obcall::ObAlterTableRes &res,
      common::ObIAllocator &allocator,
      ObSQLSessionInfo *my_session,
      query::ObIRootCommandService &root_commands,
      query::ObIQueryRuntimeEnvironment &runtime_environment,
      query::ObILocalCommandService &local_commands);

  int alter_table_exchange_partition_rpc(
      obcall::ObExchangePartitionArg &exchange_partition_arg,
      obcall::ObAlterTableRes &res,
      ObSQLSessionInfo *my_session,
      query::ObIRootCommandService &root_commands);

  int need_check_constraint_validity(obcall::ObAlterTableArg &alter_table_arg, bool &need_check);


  int check_alter_partition(
      ObExecContext &ctx,
      ObAlterTableStmt &stmt,
      const obcall::ObAlterTableArg &arg);
  int resolve_alter_column_partition_expr(
      const share::schema::ObColumnSchemaV2 &col_schema,
      const share::schema::ObTableSchema &table_schema,
      ObSchemaGetterGuard &schema_guard,
      ObSQLSessionInfo &session_info,
      common::ObIAllocator &allocator,
      const bool is_sub_part,
      ObRawExprResType &dst_res_type);
  template<class T>
  int calc_range_part_high_bound(
      const ObPartitionFuncType part_func_type,
      const ObString &col_name,
      const ObRawExprResType &dst_res_type,
      T &part,
      ObExecContext &ctx);
  int calc_range_values_exprs(
      const share::schema::ObColumnSchemaV2 &col_schema,
      const share::schema::ObTableSchema &orig_table_schema,
      share::schema::ObTableSchema &new_table_schema,
      ObSchemaGetterGuard &schema_guard,
      ObSQLSessionInfo &session_info,
      common::ObIAllocator &allocator,
      ObExecContext &ctx,
      const bool is_subpart);
  template<class T>
  int calc_list_part_rows(
    const ObPartitionFuncType part_func_type,
    const ObString &col_name,
    const ObRawExprResType &dst_res_type,
    const T &orig_part,
    T &new_part,
    ObExecContext &ctx,
    common::ObIAllocator &allocator);
  int calc_list_values_exprs(
      const share::schema::ObColumnSchemaV2 &col_schema,
      const share::schema::ObTableSchema &orig_table_schema,
      share::schema::ObTableSchema &new_table_schema,
      ObSchemaGetterGuard &schema_guard,
      ObSQLSessionInfo &session_info,
      common::ObIAllocator &allocator,
      ObExecContext &ctx,
      const bool is_subpart);
  int check_alter_part_key(ObExecContext &ctx,
                           obcall::ObAlterTableArg &arg);

  int set_index_arg_list(ObExecContext &ctx, ObAlterTableStmt &stmt);

  int refresh_schema_for_table();
  int populate_based_schema_obj_info_(obcall::ObAlterTableArg &alter_table_arg);

private:
  //DISALLOW_COPY_AND_ASSIGN(ObAlterTableExecutor);
};

class ObCommentExecutor
{
public:
  ObCommentExecutor();
  virtual ~ObCommentExecutor();
  int execute(ObExecContext &ctx, ObAlterTableStmt &stmt);
private:
  // because of the lack of the assign in alter table schema and alter column schema, this function is implemented for
  // assigning args needed for parallel comment.
  int assign_alter_to_comment_(const obcall::ObAlterTableArg &alter_table_arg, obcall::ObSetCommentArg &set_comment_arg);
};

class ObDropTableStmt;
class ObDropTableExecutor
{
public:
  ObDropTableExecutor();
  virtual ~ObDropTableExecutor();
  int execute(ObExecContext &ctx, ObDropTableStmt &stmt);
private:
};

class ObRenameTableStmt;
class ObRenameTableExecutor
{
public:
  ObRenameTableExecutor();
  virtual ~ObRenameTableExecutor();
  int execute(ObExecContext &ctx, ObRenameTableStmt &stmt);
private:

};

class ObTruncateTableStmt;
class ObTruncateTableExecutor
{
public:
  ObTruncateTableExecutor();
  virtual ~ObTruncateTableExecutor();
  int execute(ObExecContext &ctx, ObTruncateTableStmt &stmt);
private:
  int check_use_parallel_truncate(const obcall::ObTruncateTableArg &arg, bool &use_parallel_truncate);

};

class ObCreateTableLikeStmt;
class ObCreateTableLikeExecutor
{
public:
  ObCreateTableLikeExecutor();
  virtual ~ObCreateTableLikeExecutor();
  int execute(ObExecContext &ctx, ObCreateTableLikeStmt &stmt);
private:

};

class ObForkTableStmt;
class ObForkTableExecutor
{
public:
  ObForkTableExecutor();
  virtual ~ObForkTableExecutor();
  int execute(ObExecContext &ctx, ObForkTableStmt &stmt);
private:

};

class ObRecyclebinRestoreTableStmt;
class ObRecyclebinRestoreTableExecutor
{
public:
  ObRecyclebinRestoreTableExecutor() {}
  virtual ~ObRecyclebinRestoreTableExecutor() {}
  int execute(ObExecContext &ctx, ObRecyclebinRestoreTableStmt &stmt);
private:
};

class ObPurgeTableStmt;
class ObPurgeTableExecutor
{
public:
  ObPurgeTableExecutor() {}
  virtual ~ObPurgeTableExecutor() {}
  int execute(ObExecContext &ctx, ObPurgeTableStmt &stmt);
private:
};

class ObOptimizeTableStmt;
class ObOptimizeTableExecutor
{
public:
  ObOptimizeTableExecutor() = default;
  virtual ~ObOptimizeTableExecutor() = default;
  int execute(ObExecContext &ctx, ObOptimizeTableStmt &stmt);
};

} //end namespace sql
} //end namespace oceanbase


#endif //OCEANBASE_SQL_OB_TABLE_EXECUTOR_
