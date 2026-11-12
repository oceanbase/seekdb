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

#define USING_LOG_PREFIX SQL_RESV
#include "ob_alter_routine_resolver.h"
#include "ob_alter_routine_stmt.h"
#include "sql/pl/ob_pl_package.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObAlterRoutineResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;

  CK (OB_NOT_NULL(session_info_));
  CK (OB_NOT_NULL(schema_checker_));
  CK (OB_NOT_NULL(allocator_));
  CK (OB_LIKELY((peg::NodeKind::AlterProcedure == parse_tree.kind()) || (peg::NodeKind::AlterFunction == parse_tree.kind())));

  if (OB_SUCC(ret)) {
    ObAlterRoutineStmt *alter_routine_stmt = NULL;
    const share::schema::ObRoutineInfo *routine_info = NULL;
    const peg::Node &name_node = peg::routine_name(parse_tree);
    ObString db_name;
    ObString sp_name;
    //Step1: resolve routine name and check priv
    OZ (ObResolverUtils::resolve_sp_name(*session_info_, peg::name_parts(name_node), *allocator_, db_name, sp_name));
    //Step2: create alter stmt
    if (OB_SUCC(ret) && OB_ISNULL(alter_routine_stmt = create_stmt<ObAlterRoutineStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    //Step3: got standalone routine info 
    if (OB_FAIL(ret)) {
    } else if (peg::NodeKind::AlterProcedure == parse_tree.kind()) {
      OZ (schema_checker_->get_standalone_procedure_info(
        db_name, sp_name, routine_info));
    } else {
      OZ (schema_checker_->get_standalone_function_info(
       db_name, sp_name, routine_info));
    }
    if (OB_SUCC(ret) && OB_ISNULL(routine_info)) {
      ret = OB_ERR_SP_DOES_NOT_EXIST;
      LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST,
                     peg::NodeKind::AlterProcedure == parse_tree.kind() ? "PROCEDURE" : "FUNCTION",
                     db_name.length(), db_name.ptr(),
                     sp_name.length(), sp_name.ptr());
    }
    // add schema check info
    OZ (ob_add_ddl_dependency(routine_info->get_routine_id(),
                              ROUTINE_SCHEMA,
                              routine_info->get_schema_version(),
                              alter_routine_stmt->get_routine_arg()));
    //Step4: do real alter resolve
    if (OB_FAIL(ret)) {
    } else {
      OX (alter_routine_stmt->get_routine_arg().routine_info_ = *routine_info);
      OZ (resolve_clause_list(peg::routine_characteristics(parse_tree), alter_routine_stmt->get_routine_arg()));
      OX (alter_routine_stmt->get_routine_arg().db_name_ = db_name);
      OX (alter_routine_stmt->get_routine_arg().routine_info_.set_routine_id(routine_info->get_routine_id()));
      OX (alter_routine_stmt->get_routine_arg().is_need_alter_ = true);
    }
    //Step5: collection error info
    if (OB_SUCC(ret)) {
      obcall::ObCreateRoutineArg &crt_routine_arg = alter_routine_stmt->get_routine_arg();
      ObErrorInfo &error_info = crt_routine_arg.error_info_;
      error_info.collect_error_info(&(crt_routine_arg.routine_info_));
    }
  }
  return ret;
}

int ObAlterRoutineResolver::resolve_clause_list(
  rust::Slice<const peg::Node> clauses, obcall::ObCreateRoutineArg &crt_routine_arg)
{
  int ret = OB_SUCCESS;
  auto &routine = crt_routine_arg.routine_info_;
  for (const auto &clause : clauses) {
    if (OB_FAIL(ret)) {
      break;
    } else if (clause.kind() == peg::NodeKind::InvokeRights) {
      if (peg::routine_invoker_rights(clause)) {
        routine.set_invoker_right();
      } else {
        routine.clear_invoker_right();
      }
    } else if (clause.kind() == peg::NodeKind::ColumnComment) {
      const auto clause_text = clause.text();
      OZ (routine.set_comment(syntax_text(clause_text)));
    } else if (clause.kind() == peg::NodeKind::DataAccess) {
      switch (peg::routine_data_access(clause)) {
      case peg::RoutineDataAccess::NoSql: routine.set_no_sql(); break;
      case peg::RoutineDataAccess::ReadsSqlData: routine.set_reads_sql_data(); break;
      case peg::RoutineDataAccess::ModifiesSqlData: routine.set_modifies_sql_data(); break;
      case peg::RoutineDataAccess::ContainsSql: routine.set_contains_sql(); break;
      }
    }
  }
  return ret;
}



} // namespace sql
} //namespace oceanbase
