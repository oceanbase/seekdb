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
#include "sql/resolver/ob_stmt_resolver.h"

#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{

uint64_t ObStmtResolver::generate_table_id()
{
  if (NULL != params_.query_ctx_) {
    return params_.query_ctx_->available_tb_id_--;
  } else {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "query ctx pointer is null");
    return OB_INVALID_ID;
  }
}

int ObStmtResolver::resolve_table_relation_node(const ParseNode *node,
                                                ObString &table_name,
                                                ObString &db_name,
                                                bool is_org/*false*/)
{
  int ret = OB_SUCCESS;
  bool is_db_explicit = false;
  if (OB_FAIL(resolve_table_relation_node_v2(node,
                                             table_name,
                                             db_name,
                                             is_db_explicit,
                                             is_org))) {
    LOG_WARN("failed to resolve table name", K(ret));
  } else {
    // do nothing
  }
  return ret;
}
// description: parse association table
//
// @param [in] node         The node related to the associated table
// @param [out] table_name  The associated table name filled into arg
// @param [out] db_name     The associated database name filled into arg

// @return oceanbase error code defined in lib/ob_errno.def
int ObStmtResolver::resolve_table_relation_node_v2(const ParseNode *node,
                                                   ObString &table_name,
                                                   ObString &db_name,
                                                   bool &is_db_explicit,
                                                   bool is_org /*false*/)
{
  int ret = OB_SUCCESS;
  is_db_explicit = false;
  ParseNode *db_node = node->children_[0];
  ParseNode *relation_node = node->children_[1];
  int32_t table_len = static_cast<int32_t>(relation_node->str_len_);
  table_name.assign_ptr(const_cast<char*>(relation_node->str_value_), table_len);
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  ObCollationType cs_type = CS_TYPE_INVALID;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else if (OB_FAIL(session_info_->get_name_case_mode(mode))) {
    SERVER_LOG(WARN, "fail to get name case mode", K(mode), K(ret));
  } else if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
    LOG_WARN("fail to get collation_connection", K(ret));
  } else {
    bool perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
    int tmp_ret = ObSQLUtils::check_and_convert_table_name(cs_type, perserve_lettercase, table_name);
    // Because the index table has a prefix, so when checking if table_name is too long for the first time, we need to continue to obtain db information to determine if it is an index table
    if (OB_SUCCESS == tmp_ret || OB_ERR_TOO_LONG_IDENT == tmp_ret
        || ((session_info_->get_ddl_info().is_ddl() || session_info_->get_ddl_info().is_dummy_ddl_for_inner_visibility()) &&
            OB_WRONG_TABLE_NAME == tmp_ret)) {
      if (NULL == db_node) {
        if (is_org || params_.is_resolve_fake_cte_table_) {
          db_name = ObString::make_empty_string();
        } else if (session_info_->get_database_name().empty()) {
          ret = OB_ERR_NO_DB_SELECTED;
          LOG_WARN("No database selected");
        } else {
          db_name = session_info_->get_database_name();
        }
      } else {
        is_db_explicit = true;
        int32_t db_len = static_cast<int32_t>(db_node->str_len_);
        db_name.assign_ptr(const_cast<char*>(db_node->str_value_), db_len);
        if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(cs_type, perserve_lettercase, db_name))) {
          LOG_WARN("fail to check and convert database name", K(db_name), K(ret));
        } else {
          CK (OB_NOT_NULL(schema_checker_->get_schema_guard()));
          OZ (ObSQLUtils::cvt_db_name_to_org(*schema_checker_->get_schema_guard(),
                                             session_info_,
                                             db_name,
                                             allocator_));
        }
      }
      if (OB_SUCCESS == ret && (OB_ERR_TOO_LONG_IDENT == tmp_ret || OB_WRONG_TABLE_NAME == tmp_ret)) {
         // Directly querying the index table, the table name length restriction is relaxed due to the index prefix
         stmt::StmtType stmt_type = (NULL == get_basic_stmt()) ? stmt::T_NONE : get_basic_stmt()->get_stmt_type();
         bool is_index_table = false;
         
         const bool is_hidden = session_info_->is_table_name_hidden();
         const bool is_built_in_index = true;
         if (OB_FAIL(schema_checker_->check_table_exists(db_name, table_name, true, is_hidden, is_index_table))) {
           LOG_WARN("fail to check and convert table name", K(db_name), K(table_name), K(ret));
         } else if (!is_index_table && // check again
             OB_FAIL(schema_checker_->check_table_exists(db_name, table_name, true, is_hidden, is_index_table, is_built_in_index))) {
           LOG_WARN("fail to check table exist again", K(ret), K(db_name), K(table_name));
         } else if (OB_FAIL(ObSQLUtils::check_and_convert_table_name(cs_type, perserve_lettercase, table_name, stmt_type, is_index_table))) {
           LOG_WARN("fail to check and convert table name", K(table_name), K(stmt_type), K(is_index_table), K(ret));
         }
      } else if (OB_ERR_TOO_LONG_IDENT == tmp_ret) {
        // For compatibility with MySQL, prioritize returning the error code from the first table name check
        ret = tmp_ret;
        LOG_WARN("fail to check and convert table name", K(table_name), K(ret));
      } else {  } // do  nothing
    } else {
      ret = tmp_ret;
      LOG_WARN("fail to check and convert table name", K(table_name), K(ret));
    }
  }
  return ret;
}

int ObStmtResolver::resolve_ref_factor(const ParseNode *node,
                                       ObSQLSessionInfo *session_info, 
                                       ObString &table_name, 
                                       ObString &db_name)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode mode = OB_NAME_CASE_INVALID;
  if (OB_ISNULL(node) || OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("argument is NULL", K(node), K(session_info), K(ret));
  } else {
    ParseNode *db_node = node->children_[0];
    ParseNode *relation_node = node->children_[1];
    int32_t table_len = static_cast<int32_t>(relation_node->str_len_);
    table_name.assign_ptr(const_cast<char*>(relation_node->str_value_), table_len);
    ObCollationType cs_type = CS_TYPE_INVALID;
    if (OB_FAIL(session_info->get_name_case_mode(mode))) {
      SERVER_LOG(WARN, "fail to get name case mode", K(mode), K(ret));
    } else if (OB_FAIL(session_info->get_collation_connection(cs_type))) {
      LOG_WARN("fail to get collation_connection", K(ret));
    } else {
      bool perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
      if (OB_FAIL(ObSQLUtils::check_and_convert_table_name(cs_type, perserve_lettercase, table_name))) {
        LOG_WARN("fail to check and convert relation name", K(table_name), K(ret));
      } else {
        if (NULL == db_node) {
          if (session_info->get_database_name().empty()) {
            ret = OB_ERR_NO_DB_SELECTED;
            LOG_WARN("No database selected");
          } else {
            db_name = session_info->get_database_name();
          }
        } else {
          int32_t db_len = static_cast<int32_t>(db_node->str_len_);
          db_name.assign_ptr(const_cast<char*>(db_node->str_value_), db_len);
          if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(cs_type, perserve_lettercase, db_name))) {
            LOG_WARN("fail to check and convert database name", K(db_name), K(ret));
          }
        }
      }
    }

  }
  return ret;
}

int ObStmtResolver::resolve_database_factor(const ParseNode *node,
                                            uint64_t &database_id,
                                            ObString &db_name)
{
  int ret = OB_SUCCESS;
  database_id = OB_INVALID_ID;
  if (OB_ISNULL(node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("node is NULL", K(ret));
  } else if (OB_UNLIKELY(T_IDENT != node->type_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("node type is not T_IDENT", K(ret), K(node->type_));
  } else if (FALSE_IT(db_name.assign_ptr(const_cast<char*>(node->str_value_),
                                  static_cast<int32_t>(node->str_len_)))) {
    // won't be here
  } else if (OB_FAIL(schema_checker_->get_database_id(db_name, database_id))) {
    LOG_USER_ERROR(OB_ERR_BAD_DATABASE, db_name.length(), db_name.ptr());
  }
  return ret;
}

int ObStmtResolver::normalize_table_or_database_names(ObString &name)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode;
  if (name.empty() || OB_ISNULL(session_info_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid name is empty", K(name), K(ret));
  } else if (OB_FAIL(session_info_->get_name_case_mode(case_mode))) {
    LOG_WARN("fail to get name case mode", K(ret));
  } else if (OB_LOWERCASE_AND_INSENSITIVE == case_mode) {
    ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
  }
  
  return ret;
}

int ObStmtResolver::get_column_schema(const uint64_t table_id,
    const ObString &column_name,
    const share::schema::ObColumnSchemaV2 *&column_schema,
    const bool get_hidden /* = false */,
    bool is_link /* = false */)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "not init", K(ret), KP(schema_checker_), KP(session_info_));
  } else {
    const bool hidden = get_hidden || session_info_->is_inner();

    // Generated columns added by function-based indexes are hidden but may
    // still be selected through this path.
    if (OB_FAIL(schema_checker_->get_column_schema( table_id, column_name, column_schema, true, is_link))) {
      LOG_WARN("fail to get column schema", K(table_id), K(column_name), K(ret));
    } else if (!hidden && column_schema->is_hidden() && !column_schema->is_generated_column()) {
      ret = OB_ERR_BAD_FIELD_ERROR;
      LOG_INFO("do not get hidden column", K(table_id), K(column_name), K(ret));
    }
  }
  return ret;
}

int ObStmtResolver::get_column_schema(const uint64_t table_id,
    const uint64_t column_id,
    const share::schema::ObColumnSchemaV2 *&column_schema,
    const bool get_hidden /* = false */,
    bool is_link /* = false */)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(schema_checker_) || OB_ISNULL(session_info_)) {
    ret = OB_NOT_INIT;
    SQL_RESV_LOG(WARN, "not init", K(ret), KP(schema_checker_), KP(session_info_));
  } else {
    const bool hidden = get_hidden || session_info_->is_inner();
    if (OB_FAIL(schema_checker_->get_column_schema( table_id, column_id, column_schema, hidden, is_link))) {
      SQL_RESV_LOG(WARN, "get_column_schema failed", K(ret), K(table_id), K(column_id), K(hidden), K(is_link));
    }
  }
  return ret;
}

int ObStmtResolver::check_table_name_equal(const ObString &name1, const ObString &name2, bool &equal)
{
  int ret = OB_SUCCESS;
  ObNameCaseMode case_mode = OB_NAME_CASE_INVALID;
  equal = false;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected session info", K(ret));
  } else if (OB_FAIL(session_info_->get_name_case_mode(case_mode))) {
    LOG_WARN("fail to get name case mode", K(ret));
  } else if (OB_NAME_CASE_INVALID == case_mode) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected name case mode", K(ret));
  } else {
    equal = ObCharset::case_mode_equal(case_mode, name1, name2);
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
