/*
 * Copyright (c) 2026 OceanBase.
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

#include "sql/resolver/prepare/prepared_command_resolver.h"
#include "rust/sql_parser.h"
#include "sql/plan_cache/literal_token.h"
#include "sql/plan_cache/ob_prepare_stmt_struct.h"
#include "sql/resolver/expr/literal_decode_options.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/resolver/prepare/ob_deallocate_stmt.h"
#include "sql/resolver/prepare/ob_execute_stmt.h"
#include "sql/resolver/prepare/ob_prepare_stmt.h"
#include "sql/resolver/statement_factory.h"

namespace oceanbase::sql
{
using namespace common;
namespace
{
int resolve_command_value(const peg::Node &source, ObResolverParams &params, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObString text;
  const bool binary = source.kind() == peg::NodeKind::HexString || source.kind() == peg::NodeKind::BitString;
  rust::Vec<uint8_t> decoded;
  auto source_spelling = source.text();
  if (binary) {
    const auto literal = peg::expression_literal(source);
    decoded = peg::decode_binary_literal(literal.text, literal.binary_notation);
    source_spelling = rust::Slice<const uint8_t>(decoded.data(), decoded.size());
  }
  const ObString source_text(syntax_text(source_spelling));
  if (OB_FAIL(ob_write_string(*params.allocator_, source_text, text))) {
  } else if (source.kind() == peg::NodeKind::UserVariable) {
    // The expression helper normalizes its owned name in place.
    ret = ObRawExprUtils::build_get_user_var(*params.expr_factory_, text, expr,
                                            params.session_info_, params.query_ctx_);
  } else if (source.kind() == peg::NodeKind::String || binary) {
    ObCollationType collation = CS_TYPE_INVALID;
    ObObjParam value;
    ObString prefix;
    ObConstRawExpr *literal_expr = nullptr;
    const LiteralToken literal{binary ? peg::LiteralKind::Binary : peg::LiteralKind::String, 0, text, {}, false, 0};
    if (OB_FAIL(params.session_info_->get_collation_connection(collation))) {
    } else {
      const LiteralDecodeOptions options{{stmt::T_PREPARE, false, false, false, 0},
          collation, params.session_info_->get_timezone_info(), params.session_info_->get_sql_mode(),
          share::COMPAT_MYSQL57, false, false, nullptr, NumericSignMode::Source};
      if (OB_FAIL(ObResolverUtils::resolve_literal(literal, options, *params.allocator_, value, prefix))) {
      } else if (OB_FAIL(params.expr_factory_->create_raw_expr(binary ? T_HEX_STRING : T_VARCHAR, literal_expr))) {
      } else {
        literal_expr->set_value(value);
        literal_expr->set_expr_obj_meta(value.get_param_meta());
        literal_expr->set_literal_prefix(prefix);
        literal_expr->set_accuracy(value.get_accuracy());
        literal_expr->set_result_flag(value.get_result_flag());
        expr = literal_expr;
        ret = expr->formalize(params.session_info_);
      }
    }
  } else {
    ret = OB_ERR_PARSER_SYNTAX;
  }
  return ret;
}

int resolve_prepare(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  const auto node_text = node.text();
  ObPrepareStmt *prepared = nullptr;
  ObString name;
  ObRawExpr *source = nullptr;
  if (OB_FAIL(create_resolved_statement(params, prepared))) {
  } else if (OB_FAIL(ob_write_string(*params.allocator_,
              syntax_text(node_text), name))) {
  } else if (OB_FAIL(resolve_command_value(peg::prepared_source(node), params, source))) {
  } else {
    prepared->set_prepare_name(name);
    prepared->set_prepare_sql(source);
    stmt = prepared;
  }
  return ret;
}

int resolve_execute(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  const auto node_text = node.text();
  ObExecuteStmt *execute = nullptr;
  ObString name;
  ObPsStmtId id = OB_INVALID_ID;
  ObPsSessionInfo *prepared = nullptr;
  if (OB_FAIL(create_resolved_statement(params, execute))) {
  } else if (OB_FAIL(ob_simple_low_to_up(*params.allocator_,
              syntax_text(node_text), name))) {
  } else if (OB_FAIL(params.session_info_->get_prepare_id(name, id))) {
  } else if (OB_FAIL(params.session_info_->get_ps_session_info(id, prepared))) {
  } else if (OB_ISNULL(prepared)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    execute->set_prepare_id(id);
    execute->set_prepare_type(prepared->get_stmt_type());
    for (const auto &argument : peg::execution_arguments(node)) {
      ObRawExpr *expr = nullptr;
      if (OB_FAIL(resolve_command_value(argument, params, expr))) {
        break;
      } else if (OB_FAIL(execute->add_param(expr))) {
        break;
      }
    }
    stmt = execute;
  }
  return ret;
}

int resolve_deallocate(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  const auto node_text = node.text();
  ObDeallocateStmt *deallocate = nullptr;
  ObString name;
  ObPsStmtId id = OB_INVALID_ID;
  if (OB_FAIL(create_resolved_statement(params, deallocate))) {
    if (ret == OB_SQL_RESOLVER_NO_MEMORY) ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ob_simple_low_to_up(*params.allocator_,
              syntax_text(node_text), name))) {
  } else if (OB_FAIL(params.session_info_->get_prepare_id(name, id))) {
  } else {
    deallocate->set_prepare_id(id);
    deallocate->set_prepare_name(name);
    stmt = deallocate;
  }
  return ret;
}
}

int resolve_prepared_command(const peg::Node &node, ObResolverParams &params, ObStmt *&stmt)
{
  stmt = nullptr;
  if (params.is_prepare_protocol_) return OB_ERR_PARSE_SQL;
  if (OB_ISNULL(params.allocator_) || OB_ISNULL(params.stmt_factory_)
      || OB_ISNULL(params.session_info_) || OB_ISNULL(params.query_ctx_)
      || OB_ISNULL(params.expr_factory_) || OB_ISNULL(params.stmt_factory_->get_query_ctx())) {
    return OB_ERR_UNEXPECTED;
  }
  switch (node.kind()) {
    case peg::NodeKind::Prepare: return resolve_prepare(node, params, stmt);
    case peg::NodeKind::Execute: return resolve_execute(node, params, stmt);
    case peg::NodeKind::Deallocate: return resolve_deallocate(node, params, stmt);
    default: return OB_INVALID_ARGUMENT;
  }
}
}
