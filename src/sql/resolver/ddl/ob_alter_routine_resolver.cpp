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
#include "pl/ob_pl_package.h"
#include "pl/parser/parse_stmt_item_type.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

int ObAlterRoutineResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;

  CK (OB_NOT_NULL(session_info_));
  CK (OB_NOT_NULL(schema_checker_));
  CK (OB_NOT_NULL(allocator_));
  CK (OB_LIKELY((T_SP_ALTER == parse_tree.type_) || (T_SF_ALTER == parse_tree.type_)));
  CK (OB_LIKELY(2 == parse_tree.num_child_));
  CK (OB_NOT_NULL(parse_tree.children_));
  CK (OB_NOT_NULL(parse_tree.children_[0]));

  if (OB_SUCC(ret)) {
    ObAlterRoutineStmt *alter_routine_stmt = NULL;
    const share::schema::ObRoutineInfo *routine_info = NULL;
    ParseNode *name_node = parse_tree.children_[0];
    ObString db_name;
    ObString sp_name;
    //Step1: resolve routine name and check priv
    CK (OB_NOT_NULL(name_node));
    OZ (ObResolverUtils::resolve_sp_name(*session_info_, *name_node, db_name, sp_name));
    //Step2: create alter stmt
    if (OB_SUCC(ret) && OB_ISNULL(alter_routine_stmt = create_stmt<ObAlterRoutineStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to alloc memory for ObAlterRoutineStmt", K(ret));
    }
    //Step3: got standalone routine info 
    if (OB_FAIL(ret)) {
    } else if (T_SP_ALTER == parse_tree.type_) {
      OZ (schema_checker_->get_standalone_procedure_info(
        db_name, sp_name, routine_info));
    } else {
      OZ (schema_checker_->get_standalone_function_info(
       db_name, sp_name, routine_info));
    }
    if (OB_SUCC(ret) && OB_ISNULL(routine_info)) {
      ret = OB_ERR_SP_DOES_NOT_EXIST;
      LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST,
                     T_SP_ALTER == parse_tree.type_ ? "PROCEDURE" : "FUNCTION",
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
      if (OB_NOT_NULL(parse_tree.children_[1])) {
        OZ (resolve_impl(alter_routine_stmt->get_routine_arg(), *routine_info, *(parse_tree.children_[1])));
      } else {
        OX (alter_routine_stmt->get_routine_arg().routine_info_ = *routine_info);
      }
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
  const ParseNode *node, obcall::ObCreateRoutineArg &crt_routine_arg)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(node)) {
    CK (T_SP_CLAUSE_LIST == node->type_);
    for (int64_t i = 0; OB_SUCC(ret) && i < node->num_child_; ++i) {
      const ObStmtNodeTree *child = node->children_[i];
      if (OB_NOT_NULL(child)) {
        if (T_SP_INVOKE == child->type_) {
          if (SP_INVOKER == child->value_) {
            crt_routine_arg.routine_info_.set_invoker_right();
          } else if (SP_DEFINER == child->value_) {
            crt_routine_arg.routine_info_.clear_invoker_right();
          }
        } else if (T_COMMENT == child->type_) {
          ObString routine_comment;
          OX (routine_comment = ObString(child->str_len_, child->str_value_));
          OZ (crt_routine_arg.routine_info_.set_comment(routine_comment));
        } else if (T_SP_DATA_ACCESS == child->type_) {
          if (SP_NO_SQL == child->value_) {
            crt_routine_arg.routine_info_.set_no_sql();
          } else if (SP_READS_SQL_DATA == child->value_) {
            crt_routine_arg.routine_info_.set_reads_sql_data();
          } else if (SP_MODIFIES_SQL_DATA == child->value_) {
            crt_routine_arg.routine_info_.set_modifies_sql_data();
          } else if (SP_CONTAINS_SQL == child->value_) {
            crt_routine_arg.routine_info_.set_contains_sql();
          }
        } else {
          // do nothing
          /* Currently, ob only support SQL SECURITY and LANGUAGE SQL opt clause,
             other clauses have no real meaning, they are advisory only.
             MYSQL server does not use them to constrain what kinds of statements
             a routine is permitted to execute. */ 
        }
      }
    }
  }
  return ret;
}

int ObAlterRoutineResolver::resolve_impl(
  obcall::ObCreateRoutineArg &crt_routine_arg,
  const share::schema::ObRoutineInfo &routine_info, const ParseNode &alter_clause_node)
{
  int ret = OB_SUCCESS;
  if (T_SP_EDITIONABLE_CLAUSE == alter_clause_node.type_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported yet!", K(ret));
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "alter editionable");
  } else if (T_SP_CLAUSE_LIST == alter_clause_node.type_) {
    OX (crt_routine_arg.routine_info_ = routine_info);
    OZ (resolve_clause_list(&alter_clause_node, crt_routine_arg));
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unknow alter clause node type", K(ret), K(alter_clause_node.type_));
  }
  return ret;
}

} // namespace sql
} //namespace oceanbase
