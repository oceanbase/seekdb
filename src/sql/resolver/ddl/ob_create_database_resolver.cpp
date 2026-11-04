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
#include "sql/resolver/ddl/ob_create_database_resolver.h"

#include "sql/resolver/ddl/ob_create_database_stmt.h"
#include "sql/resolver/ddl/database_options.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObCreateDatabaseResolver::ObCreateDatabaseResolver(ObResolverParams &params)
    : ObDDLResolver(params)
{
}

ObCreateDatabaseResolver::~ObCreateDatabaseResolver()
{
}

int ObCreateDatabaseResolver::resolve(const peg::Node &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)
      || parse_tree.kind() != peg::NodeKind::CreateDatabase) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObCreateDatabaseStmt *create_database_stmt = NULL;
    if (OB_ISNULL(create_database_stmt = create_stmt<ObCreateDatabaseStmt>())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to create create_database_stmt", K(ret));
    } else {
      stmt_ = create_database_stmt;
      
    }
    OX (create_database_stmt->set_if_not_exists(peg::database_if_not_exists(parse_tree)));
    //resolve database name
    if (OB_SUCC(ret)) {
      ObString database_name;
      const auto &name = peg::database_name(parse_tree);
      if (OB_SUCC(ret)) {
        const auto name_text = name.text();
        OZ (ob_write_string(*allocator_, syntax_text(name_text), database_name));
        ObNameCaseMode mode = OB_NAME_CASE_INVALID;
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(session_info_->get_name_case_mode(mode))) {
        } else {
          bool perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
          ObCollationType cs_type = CS_TYPE_INVALID;
          if (OB_FAIL(session_info_->get_collation_connection(cs_type))) {
          } else if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(
                      cs_type, perserve_lettercase, database_name))) {
          } else if (OB_FAIL(create_database_stmt->set_database_name(database_name))) {
          }
        }
      }
    }
    OZ (resolve_options(*create_database_stmt, peg::database_options(parse_tree)));
    if (OB_SUCC(ret)) {
      if (CHARSET_INVALID == create_database_stmt->get_charset_type() &&  CS_TYPE_INVALID == create_database_stmt->get_collation_type()) {
         // For CREATE DATABASE statements, the server character set and collation are used as default
         // values for table definitions if the database character set and collation are not specified.
         // To override this, provide explicit CHARACTER SET and COLLATE table options.
         // http://dev.mysql.com/doc/refman/5.7/en/charset-database.html
         int64_t coll_cs_srv_int64 = -1;
         int64_t coll_srv_int64 = -1;
         if (OB_FAIL(session_info_->get_sys_variable(share::SYS_VAR_CHARACTER_SET_SERVER, coll_cs_srv_int64))) {
         } else if (OB_FAIL(session_info_->get_sys_variable(share::SYS_VAR_COLLATION_SERVER, coll_srv_int64))) {
         } else if (!ObCharset::is_valid_collation(coll_cs_srv_int64) || !ObCharset::is_valid_collation(coll_srv_int64)) {
           ret = OB_ERR_UNEXPECTED;
           SQL_RESV_LOG(WARN, "invalid collation type", K(ret), K(coll_cs_srv_int64), K(coll_srv_int64));
         } else {
           ObCharsetType cs = ObCharset::charset_type_by_coll(static_cast<ObCollationType>(coll_cs_srv_int64));
           ObCollationType coll_type = static_cast<ObCollationType>(coll_srv_int64);
           create_database_stmt->set_charset_type(cs);
           create_database_stmt->set_collation_type(coll_type);
         }
      }
    }
  }
  LOG_INFO("resolve create database finish", K(ret));
  return ret;
}

int ObCreateDatabaseResolver::resolve_options(ObCreateDatabaseStmt &stmt,
                                               rust::Slice<const peg::Node> options)
{
  return resolve_database_options(stmt, options);
}

} //end namespace sql
} //end namespace oceanbase
