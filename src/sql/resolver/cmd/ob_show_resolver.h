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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_SHOW_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_SHOW_RESOLVER_
#include "sql/resolver/dml/ob_select_resolver.h"
#include "lib/hash/ob_hashset.h"
namespace oceanbase
{
namespace sql
{
class ObShowResolver : public ObSelectResolver
{
public:
  class ObShowResolverContext;
  explicit ObShowResolver(ObResolverParams &params);
  virtual ~ObShowResolver();
  int resolve(const peg::Node &statement);
private:
  class ObSqlStrGenerator;
  struct ObShowSqlSet;
  struct CheckTableInfo
  {
    CheckTableInfo() : db_name_(), table_name_(), db_exist_(false),
                        table_exist_(false), is_view_(false),
                        valid_(true) {}
    int hash(uint64_t &res) const { res = db_name_.hash(table_name_.hash()); return OB_SUCCESS; }
    bool operator==(const CheckTableInfo &other) const
    {
      return db_name_ == other.db_name_ && table_name_ == other.table_name_;
    }
    ObString db_name_;
    ObString table_name_;
    bool db_exist_;
    bool table_exist_;
    bool is_view_;
    bool valid_;
    TO_STRING_KV(K_(db_name),
                K_(table_name),
                K_(table_exist),
                K_(db_exist),
                K_(is_view),
                K_(valid));
  };
  typedef common::hash::ObHashSet<ObShowResolver::CheckTableInfo, common::hash::NoPthreadDefendMode> TableInfoSet;
  int resolve_show_create_user(const peg::Node &statement, ObShowResolverContext &context,
                               ObString &select_sql, ObSqlStrGenerator &sql_gen);
  int resolve_show_check_table(const peg::Node &statement, ObShowResolverContext &context,
                               ObString &select_sql);
  int resolve_check_table_info(const peg::Node &table, ObIAllocator &allocator,
                              ObIArray<CheckTableInfo> &infos, TableInfoSet &tables);
  int append_check_table_row(ObSqlString &sql, const ObString &table,
                             const char *message_type, const ObString &message);
  int get_database_info(const peg::Node *database, const ObString &session_database,
                       ObShowResolverContext &context, uint64_t &id);
  int resolve_show_from_table(const peg::Node &object, const peg::Node *database,
      peg::NodeKind kind, ObString &database_name, uint64_t &database_id,
      ObString &table_name, uint64_t &table_id, bool &is_view);
  int resolve_show_from_database(const peg::Node &database, uint64_t &id, ObString &name);
  int resolve_schema_object_name(const peg::Node &object, ObString &database,
                                uint64_t &database_id, ObString &name);
  int resolve_show_from_routine(const peg::Node &object, bool procedure,
      ObString &database, uint64_t &database_id, ObString &name, uint64_t &id, int64_t &type);
  int resolve_show_from_trigger(const peg::Node &object, ObString &database,
      uint64_t &database_id, ObString &name, uint64_t &id, ObString &table_name);
  int parse_and_resolve_select_sql(const ObString &sql);
  int resolve_like_or_where_clause(ObShowResolverContext &context);
  int process_select_type(ObSelectStmt *select, stmt::StmtType type, bool count);
  int resolve_column_ref_expr(const ObQualifiedName &name, ObRawExpr *&expression) override;
  int check_db_access_for_show_sql(const ObShowResolverContext &context,
      ObSessionPrivInfo &privilege, const ObIArray<uint64_t> &roles);
  ObString show_table_alias_;
  DISALLOW_COPY_AND_ASSIGN(ObShowResolver);
};

struct ObShowResolver::ObShowSqlSet
{
#define DECLARE_SHOW_CLAUSE_SET(SHOW_STMT_TYPE)     \
  static const char *SHOW_STMT_TYPE##_SELECT;       \
  static const char *SHOW_STMT_TYPE##_SUBQUERY;     \
  static const char *SHOW_STMT_TYPE##_ORA_SUBQUERY; \
  static const char *SHOW_STMT_TYPE##_LIKE

  static const char *SUBQERY_ALIAS;
  DECLARE_SHOW_CLAUSE_SET(SHOW_TABLES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_TABLES_LIKE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_FULL_TABLES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_FULL_TABLES_LIKE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CHARSET);
  DECLARE_SHOW_CLAUSE_SET(SHOW_VARIABLES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_GLOBAL_VARIABLES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_COLUMNS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_FULL_COLUMNS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_EXTENDED_COLUMNS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_EXTENDED_FULL_COLUMNS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_DATABASE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_DATABASE_EXISTS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_INDEXES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_EXTENDED_INDEXES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_COLLATION);
  DECLARE_SHOW_CLAUSE_SET(SHOW_TRACE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_TRACE_JSON);
  DECLARE_SHOW_CLAUSE_SET(SHOW_ENGINES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PROFILE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_ENGINE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_OPEN_TABLES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PRIVILEGES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_GRANTS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PROCESSLIST);
  DECLARE_SHOW_CLAUSE_SET(SHOW_FULL_PROCESSLIST);
  DECLARE_SHOW_CLAUSE_SET(SHOW_SYS_PROCESSLIST);
  DECLARE_SHOW_CLAUSE_SET(SHOW_SYS_FULL_PROCESSLIST);
  DECLARE_SHOW_CLAUSE_SET(SHOW_TABLE_STATUS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PROCEDURE_STATUS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_WARNINGS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_COUNT_WARNINGS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_ERRORS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_COUNT_ERRORS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PARAMETERS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PARAMETERS_WITH_DEFAULT_VALUE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PARAMETERS_UNSYS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_PARAMETERS_COMPAT);
  DECLARE_SHOW_CLAUSE_SET(SHOW_SESSION_STATUS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_GLOBAL_STATUS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_DATABASES);
  DECLARE_SHOW_CLAUSE_SET(SHOW_DATABASES_LIKE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_DATABASES_STATUS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_DATABASES_STATUS_LIKE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_TABLE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_VIEW);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_PROCEDURE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_FUNCTION);
  DECLARE_SHOW_CLAUSE_SET(SHOW_RECYCLEBIN);
  DECLARE_SHOW_CLAUSE_SET(SHOW_SYS_RECYCLEBIN);
  DECLARE_SHOW_CLAUSE_SET(SHOW_TRIGGERS);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_TRIGGER);
  DECLARE_SHOW_CLAUSE_SET(SHOW_TRIGGERS_LIKE);
  DECLARE_SHOW_CLAUSE_SET(SHOW_CREATE_USER);
};// ObShowSqlSet

class ObShowResolver::ObSqlStrGenerator
{
public:
  ObSqlStrGenerator()
      : sql_buf_(NULL),
        sql_buf_pos_(0)
        {}
  virtual ~ObSqlStrGenerator() {}
  int init(common::ObIAllocator *alloc);
  virtual int gen_select_str(const char *select_str, ...);
  virtual int gen_from_str(const char *subquery_str, ...);
  virtual int gen_limit_str(int64_t offset, int64_t row_cnt);
  void assign_sql_str(common::ObString &sql_str);
private:
  char *sql_buf_;
  int64_t sql_buf_pos_;
  DISALLOW_COPY_AND_ASSIGN(ObSqlStrGenerator);
};// ObSqlstrgenerator

class ObShowResolver::ObShowResolverContext
{
public:
  ObShowResolverContext() : stmt_type_(stmt::T_NONE), parse_tree_(nullptr) {}
  ObString show_database_name_;
  stmt::StmtType stmt_type_;
  const peg::Node *parse_tree_;
  ObString column_name_;
  ObString like_column_;
};

} // sql
} // oceanbase
#endif /* OCEANBASE_SQL_RESOLVER_CMD_OB_SHOW_RESOLVER_ */
