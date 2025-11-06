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
#include "ob_drop_func_stmt.h"

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
    obrpc::ObDropRoutineArg &routine_arg = proc_stmt->get_routine_arg();
    routine_arg.tenant_id_ = session_info_->get_effective_tenant_id();
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
  bool if_exist = false;
  bool pl_y = true;
  bool need_try_pl_function = true;
  // Because the if exists syntax structure in mysql.y and pl_mysql.y is different, mysql.y has already been widely used,
  // Can't change to be consistent, temporarily can only write as two sets.
  if (parse_tree.type_ == T_DROP_FUNC) {
    if (OB_ISNULL(parse_tree.children_)
        || OB_UNLIKELY(parse_tree.num_child_ != 2)
        || OB_ISNULL(name_node = parse_tree.children_[1])) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("parse tree is invalid", "type", get_type_name(parse_tree.type_),
               K_(parse_tree.children), K_(parse_tree.num_child), K(name_node));
    } else {
      if_exist = (NULL != parse_tree.children_[0]);
      sp_name = ObString(parse_tree.children_[1]->str_len_, parse_tree.children_[1]->str_value_);
      pl_y = false;
    }
  } else if (parse_tree.type_ == T_SF_DROP) {
    if (OB_ISNULL(parse_tree.children_)
        || OB_UNLIKELY(parse_tree.num_child_ != 1)
        || OB_ISNULL(name_node = parse_tree.children_[0])) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("parse tree is invalid", "type", get_type_name(parse_tree.type_),
               K_(parse_tree.children), K_(parse_tree.num_child), K(name_node));
    } else {
      if_exist = parse_tree.value_;
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("parse tree is invalid", "type", get_type_name(parse_tree.type_),
             K_(parse_tree.children), K_(parse_tree.num_child), K(name_node));
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(session_info_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("session info is null");
    } else if (pl_y && OB_FAIL(ObResolverUtils::resolve_sp_name(*session_info_, *name_node, db_name, sp_name))) {
      ret = OB_ERR_NO_DB_SELECTED == ret && lib::is_mysql_mode() ? OB_SUCCESS : ret;
      LOG_WARN("resolve sp name failed", K(ret));
    }
    // drop ddl function and drop pl function share syntax, need to check if it is ddl function first
    if (OB_FAIL(ret)) {
    } else if (pl_y
               && OB_LIKELY(2 == name_node->num_child_)
               && OB_ISNULL(name_node->children_[0])
               && lib::is_mysql_mode()) {
      bool exist = false;
      const share::schema::ObUDF *udf_info = nullptr;
      ObString lower_name;
      OZ (ob_write_string(*allocator_, sp_name, lower_name));
      OX (ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, lower_name));
      OZ (schema_checker_->get_udf_info(
        session_info_->get_effective_tenant_id(), lower_name, udf_info, exist));
      if (OB_FAIL(ret)) {
      } else if (exist) {
        ObDropFuncStmt *drop_func_stmt = NULL;
        CK (OB_NOT_NULL(drop_func_stmt = create_stmt<ObDropFuncStmt>()));
        if (OB_SUCC(ret)) {
          obrpc::ObDropUserDefinedFunctionArg &drop_func_arg = drop_func_stmt->get_drop_func_arg();
          drop_func_arg.tenant_id_ = session_info_->get_effective_tenant_id();
          drop_func_arg.name_ = lower_name;
          drop_func_arg.if_exist_ =  if_exist;
          need_try_pl_function = false;
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (!need_try_pl_function) {
      // do nothing ...
    } else if (OB_ISNULL(routine_stmt = create_stmt<ObDropRoutineStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("create drop function stmt failed");
    } else {
      obrpc::ObDropRoutineArg &routine_arg = routine_stmt->get_routine_arg();
      routine_arg.tenant_id_ = session_info_->get_effective_tenant_id();
      routine_arg.db_name_ = pl_y ? db_name : session_info_->get_database_name();
      routine_arg.routine_name_ = sp_name;
      routine_arg.routine_type_ = share::schema::ROUTINE_FUNCTION_TYPE;
      routine_arg.if_exist_ = if_exist;
    }
  }

  return ret;
}
}
}



