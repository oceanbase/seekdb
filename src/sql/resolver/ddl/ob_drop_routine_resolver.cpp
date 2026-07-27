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
int ObDropProcedureResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  const ParseNode *name_node = NULL;
  ObString db_name;
  ObString sp_name;
  ObDropRoutineStmt *proc_stmt = NULL;
  if (OB_UNLIKELY(parse_tree.type_ != T_SP_DROP)
      || OB_ISNULL(parse_tree.children_)
      || OB_UNLIKELY(parse_tree.num_child_ != 1)
      || OB_ISNULL(name_node = parse_tree.children_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse tree is invalid", "type", get_type_name(parse_tree.type_),
             K_(parse_tree.children), K_(parse_tree.num_child), K(name_node));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else if (OB_ISNULL(schema_checker_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema checker is null");
  } else if (OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, *name_node, db_name, sp_name))) {
    LOG_WARN("resolve sp name failed", K(ret));
  } else if (OB_ISNULL(proc_stmt = create_stmt<ObDropRoutineStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create drop procedure stmt failed");
  }
  else {
    obcall::ObDropRoutineArg &routine_arg = proc_stmt->get_routine_arg();
    
    
    routine_arg.db_name_ = db_name;
    routine_arg.routine_name_ = sp_name;
    routine_arg.routine_type_ = share::schema::ROUTINE_PROCEDURE_TYPE;
    routine_arg.if_exist_ = parse_tree.value_;
  }
  return ret;
}

int ObDropFunctionResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  const ParseNode *name_node = NULL;
  ObString db_name;
  ObString sp_name;
  ObDropRoutineStmt *routine_stmt = NULL;
  if (parse_tree.type_ != T_SF_DROP
      || OB_ISNULL(parse_tree.children_)
      || OB_UNLIKELY(parse_tree.num_child_ != 1)
      || OB_ISNULL(name_node = parse_tree.children_[0])) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse tree is invalid", "type", get_type_name(parse_tree.type_),
             K_(parse_tree.children), K_(parse_tree.num_child), K(name_node));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is null");
  } else {
    if (OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, *name_node, db_name, sp_name))) {
      // MySQL allows DROP FUNCTION to reach the executor without a selected
      // database so IF EXISTS can return the compatible 1305 warning.
      if (OB_ERR_NO_DB_SELECTED == ret) {
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("resolve stored function name failed", K(ret));
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
        routine_arg.if_exist_ = parse_tree.value_;
      }
    }
  }

  return ret;
}
}
}

