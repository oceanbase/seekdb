// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ddl/outline_resolver.h"

#include "share/ob_version.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/ddl/ob_alter_outline_stmt.h"
#include "sql/resolver/ddl/ob_create_outline_stmt.h"
#include "sql/resolver/ddl/ob_drop_outline_stmt.h"
#include "sql/resolver/ob_resolver.h"

namespace oceanbase::sql {
using namespace common;

int OutlineResolver::resolve_name(const peg::Node &relation, ObString &database, ObString &name)
{
  int ret = OB_SUCCESS;
  const auto spelling = peg::relation_name(relation).text();
  OZ(ob_write_string(*allocator_, syntax_text(spelling), name));
  if (OB_SUCC(ret) && peg::relation_has_database(relation)) {
    const auto qualifier = peg::relation_database(relation).text();
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    ObCollationType collation = CS_TYPE_INVALID;
    OZ(ob_write_string(*allocator_, syntax_text(qualifier), database));
    OZ(session_info_->get_name_case_mode(mode));
    OZ(session_info_->get_collation_connection(collation));
    OZ(ObSQLUtils::check_and_convert_db_name(collation, mode != OB_LOWERCASE_AND_INSENSITIVE,
                                            database));
    OZ(ObSQLUtils::cvt_db_name_to_org(*schema_checker_->get_schema_guard(), session_info_,
                                    database, allocator_));
  } else if (OB_SUCC(ret)) {
    const ObString selected = session_info_->get_database_name().empty()
        ? ObString(OB_MOCK_DEFAULT_DATABASE_NAME) : session_info_->get_database_name();
    OZ(ob_write_string(*allocator_, selected, database));
  }
  return ret;
}

int OutlineResolver::store_source(const peg::Node &node, ObString &text)
{
  int ret = OB_SUCCESS;
  const auto span = node.span();
  const ObString expanded = params_.syntax_->source(node);
  const ObString trimmed = expanded.trim();
  const ObString source(trimmed.length(), params_.syntax_->original_source().ptr()
      + span.start + (trimmed.ptr() - expanded.ptr()));
  OZ(ob_write_string(*allocator_, source, text));
  OZ(ObSQLUtils::convert_sql_text_to_schema_for_storing(*allocator_, session_info_->get_dtc_params(), text));
  return ret;
}

int OutlineResolver::resolve_statement(const peg::Node &node, ObStmt *&statement,
                                       ObString &sql, ObString &target)
{
  int ret = OB_SUCCESS;
  const auto &query = peg::outline_statement(node);
  const auto saved_scope = params_.resolver_scope_stmt_type_;
  const auto saved_type = session_info_->get_stmt_type();
  auto *exec_ctx = session_info_->get_cur_exec_ctx();
  auto *sql_ctx = exec_ctx == nullptr ? nullptr : exec_ctx->get_sql_ctx();
  const auto saved_sql_type = sql_ctx == nullptr ? stmt::T_NONE : sql_ctx->stmt_type_;
  ObResolver resolver(params_);
  OZ(resolver.resolve(query, statement));
  params_.resolver_scope_stmt_type_ = saved_scope;
  session_info_->set_stmt_type(saved_type);
  if (sql_ctx != nullptr) {
    sql_ctx->stmt_type_ = saved_sql_type;
  }
  OZ(store_source(query, sql));
  if (OB_SUCC(ret) && peg::outline_definition(node).has_target) {
    OZ(store_source(peg::outline_target(node), target));
  }
  return ret;
}

int OutlineResolver::resolve(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  ObString database;
  ObString name;
  const auto definition = peg::outline_definition(node);
  if (OB_ISNULL(allocator_) || OB_ISNULL(session_info_) || OB_ISNULL(schema_checker_)
      || OB_ISNULL(schema_checker_->get_schema_guard()) || OB_ISNULL(params_.syntax_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(resolve_name(definition.relation, database, name))) {
  } else if (node.kind() == peg::NodeKind::DropOutline) {
    auto *statement = create_stmt<ObDropOutlineStmt>();
    if (OB_ISNULL(statement)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      statement->set_database_name(database);
      statement->set_outline_name(name);
      statement->set_is_format(definition.format);
    }
  } else if (node.kind() == peg::NodeKind::AlterOutline) {
    auto *statement = create_stmt<ObAlterOutlineStmt>();
    if (OB_ISNULL(statement)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      statement->set_database_name(database);
      statement->set_outline_name(name);
      statement->set_format_outline(definition.format);
      OZ(resolve_statement(node, statement->get_outline_stmt(),
          definition.format ? statement->get_format_outline_sql() : statement->get_outline_sql(),
          statement->get_target_sql()));
    }
  } else {
    auto *statement = create_stmt<ObCreateOutlineStmt>();
    if (OB_ISNULL(statement)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      statement->set_database_name(database);
      statement->set_outline_name(name);
      statement->set_format_outline(definition.format);
      statement->set_owner(session_info_->get_user_name());
      statement->set_owner_id(session_info_->get_user_id());
      if (definition.replace) {
        statement->set_replace();
      }
      ObString version;
      OZ(ob_write_string(*allocator_, ObString(build_version()), version));
      OX(statement->set_server_version(version));
      if (OB_SUCC(ret) && definition.source == peg::OutlineSource::Statement) {
        OZ(resolve_statement(node, statement->get_outline_stmt(),
            definition.format ? statement->get_format_outline_sql() : statement->get_outline_sql(),
            statement->get_target_sql()));
      } else if (OB_SUCC(ret) && !definition.valid_hint) {
        ret = OB_INVALID_OUTLINE;
        LOG_USER_ERROR(OB_INVALID_OUTLINE, "Hint is not correct, please check");
      } else if (OB_SUCC(ret)) {
        const auto id = peg::outline_sql_id(node);
        OZ(ob_write_string(*allocator_, syntax_text(id),
            definition.format ? statement->get_format_sql_id() : statement->get_sql_id()));
        OZ(store_source(peg::outline_hint(node), statement->get_hint()));
      }
    }
  }
  return ret;
}

}
