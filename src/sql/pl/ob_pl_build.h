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

#ifndef OCEANBASE_SRC_PL_OB_PL_BUILD_H_
#define OCEANBASE_SRC_PL_OB_PL_BUILD_H_

#include "ob_pl.h"
#include "ob_pl_stmt.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/alloc/ob_malloc_callback.h"
#include "lib/lock/ob_mutex.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObRoutineInfo;
class ObPackageInfo;
}
}
namespace sql
{
class ObRawExpr;
class ObSqlExpression;
}
namespace pl
{
class ObPLPackageAST;
class ObPLPackage;
class ObPLPackageGuard;
class ObPLResolver;

// PL front-end: parses and semantically resolves anonymous blocks, standalone
// routines, and packages into executable AST units consumed by the interpreter.
class ObPLBuilder
{
public:
  ObPLBuilder(common::ObIAllocator &allocator,
               sql::ObSQLSessionInfo &session_info,
               share::schema::ObSchemaGetterGuard &schema_guard,
               ObPLPackageGuard &package_guard,
               common::ObMySQLProxy &sql_proxy) :
    allocator_(allocator),
    session_info_(session_info),
    schema_guard_(schema_guard),
    package_guard_(package_guard),
    sql_proxy_(sql_proxy) {}
  virtual ~ObPLBuilder() {}

  int compile(const ObStmtNodeTree *block,
              const uint64_t stmt_id,
              ObPLFunction &func,
              ParamStore *params,
              bool is_prepare_protocol); //anonymous block interface


  int compile(const uint64_t id, ObPLFunction &func); //Procedure/Function interface

  int analyze_package(const ObString &source, const ObPLBlockNS *parent_ns,
                      ObPLPackageAST &package_ast, bool is_for_trigger);
  int generate_package(const ObString &exec_env, ObPLPackageAST &package_ast, ObPLPackage &package);
  int build_package(const share::schema::ObPackageInfo &package_info, const ObPLBlockNS *parent_ns,
                    ObPLPackageAST &package_ast, ObPLPackage &package); //package
  static int compile_subprogram_table(common::ObIAllocator &allocator,
                                 sql::ObSQLSessionInfo &session_info,
                                 const sql::ObExecEnv &exec_env,
                                 ObPLRoutineTable &routine_table,
                                 ObPLExecutableUnit &compile_unit,
                                 share::schema::ObSchemaGetterGuard &schema_guard);
  static int compile_type_table(const ObPLUserTypeTable &ast_type_table, ObPLExecutableUnit &unit);
  static int check_dep_schema(ObSchemaGetterGuard &schema_guard,
                              const DependenyTableStore &dep_schema_objs);
  static int init_anonymous_ast(ObPLFunctionAST &func_ast,
                                common::ObIAllocator &allocator,
                                sql::ObSQLSessionInfo &session_info,
                                ObMySQLProxy &sql_proxy,
                                share::schema::ObSchemaGetterGuard &schema_guard,
                                pl::ObPLPackageGuard &package_guard,
                                const ParamStore *params,
                                bool is_prepare_protocol = true);
  int check_package_body_legal(const ObPLBlockNS *parent_ns,
                                      const ObPLPackageAST &package_ast);
  static int update_schema_object_dep_info(ObIArray<ObSchemaObjVersion> &dp_tbl,
                                           uint64_t owner_id,
                                           uint64_t dep_obj_id,
                                           uint64_t schema_version,
                                           share::schema::ObObjectType dep_obj_type);
  static int init_function(share::schema::ObSchemaGetterGuard &schema_guard,
                           const sql::ObExecEnv &exec_env,
                           const ObPLRoutineInfo &routine_signature,
                           ObPLFunction &routine);
private:
  int init_function(const share::schema::ObRoutineInfo *proc, ObPLFunction &func);

  int generate_package_cursors(const ObPLPackageAST &package_ast,
                               const ObPLCursorTable &ast_cursor_table,
                               ObPLPackage &package);
  int generate_package_conditions(const ObPLConditionTable &ast_condition_table,
                                  ObPLPackage &package);
  int generate_package_vars(const ObPLPackageAST &package_ast,
                            const ObPLSymbolTable &ast_var_table,
                            ObPLPackage &package);
  int generate_package_types(const ObPLUserTypeTable &ast_type_table,
                             ObPLExecutableUnit &package);
  int generate_package_routines(const ObString &exec_env,
                                ObPLRoutineTable &routine_table,
                                ObPLPackage &package);
  static int compile_types(const ObIArray<const ObUserDefinedType*> &types, ObPLExecutableUnit &unit);
  static int format_object_name(share::schema::ObSchemaGetterGuard &schema_guard,
                                const uint64_t db_id,
                                const uint64_t package_id,
                                ObString &database_name,
                                ObString &package_name);
  int compile(const share::schema::ObRoutineInfo &routine, ObPLFunctionAST &func_ast, ObPLFunction &func);
public:
  // Bind a resolved raw expr's runtime ObExpr into the PL ObSqlExpression.
  static int link_sql_expr_rt(sql::ObRawExpr &raw_expr, sql::ObSqlExpression &sql_expr);
private:
  common::ObIAllocator &allocator_;
  sql::ObSQLSessionInfo &session_info_;
  share::schema::ObSchemaGetterGuard &schema_guard_;
  ObPLPackageGuard &package_guard_;
  common::ObMySQLProxy &sql_proxy_;

  static lib::ObMutex package_dep_info_lock_;
};

class ObPLBuilderEnvGuard
{
public:
  ObPLBuilderEnvGuard(const ObPackageInfo &info,
                       ObSQLSessionInfo &session_info,
                       share::schema::ObSchemaGetterGuard &schema_guard,
                       ObPLAstUnit &compile_unit,
                       int &ret,
                       const ObPLBlockNS *prarent_ns = nullptr);

  ObPLBuilderEnvGuard(const ObRoutineInfo &info,
                       ObSQLSessionInfo &session_info,
                       share::schema::ObSchemaGetterGuard &schema_guard,
                       ObPLAstUnit &compile_unit,
                       int &ret);

  ~ObPLBuilderEnvGuard();

private:
  template<class Info>
  void init(const Info &info,
            ObSQLSessionInfo &sessionInfo,
            share::schema::ObSchemaGetterGuard &schema_guard,
            ObPLAstUnit &compile_unit,
            int &ret,
            const ObPLBlockNS *parent_ns = nullptr);

private:
  int &ret_;
  ObSQLSessionInfo &session_info_;
  ObExecEnv old_exec_env_;
  ObSqlString old_db_name_;
  uint64_t old_db_id_;
  bool need_reset_exec_env_;
  bool need_reset_default_database_;
  ObArenaAllocator allocator_;
};

}
}

#endif /* OCEANBASE_SRC_PL_OB_PL_BUILD_H_ */
