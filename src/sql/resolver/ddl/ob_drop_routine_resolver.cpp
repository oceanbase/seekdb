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
#include "ob_drop_routine_resolver.h"
#include "ob_drop_routine_stmt.h"

namespace oceanbase
{
namespace sql
{
int ObDropProcedureResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  const peg::Node &name_node = peg::routine_name(parse_tree);
  ObString db_name;
  ObString sp_name;
  ObDropRoutineStmt *proc_stmt = NULL;
  if (parse_tree.kind() != peg::NodeKind::DropProcedure) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, peg::name_parts(name_node), *allocator_, db_name, sp_name))) {
  } else if (OB_ISNULL(proc_stmt = create_stmt<ObDropRoutineStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create drop procedure stmt failed");
  }
  else {
    obcall::ObDropRoutineArg &routine_arg = proc_stmt->get_routine_arg();
    
    
    routine_arg.db_name_ = db_name;
    routine_arg.routine_name_ = sp_name;
    routine_arg.routine_type_ = share::schema::ROUTINE_PROCEDURE_TYPE;
    routine_arg.if_exist_ = peg::routine_if_exists(parse_tree);
  }
  return ret;
}

int ObDropFunctionResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  const peg::Node &name_node = peg::routine_name(parse_tree);
  ObString db_name;
  ObString sp_name;
  ObDropRoutineStmt *routine_stmt = NULL;
  if (parse_tree.kind() != peg::NodeKind::DropFunction) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
  } else {
    if (OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, peg::name_parts(name_node), *allocator_, db_name, sp_name))) {
      // MySQL allows DROP FUNCTION to reach the executor without a selected
      // database so IF EXISTS can return the compatible 1305 warning.
      if (OB_ERR_NO_DB_SELECTED == ret) {
        ret = OB_SUCCESS;
      } else {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(routine_stmt = create_stmt<ObDropRoutineStmt>())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("create drop function stmt failed");
      } else {
        obcall::ObDropRoutineArg &routine_arg = routine_stmt->get_routine_arg();
        routine_arg.db_name_ = db_name;
        routine_arg.routine_name_ = sp_name;
        routine_arg.routine_type_ = share::schema::ROUTINE_FUNCTION_TYPE;
        routine_arg.if_exist_ = peg::routine_if_exists(parse_tree);
      }
    }
  }

  return ret;
}
}
}

