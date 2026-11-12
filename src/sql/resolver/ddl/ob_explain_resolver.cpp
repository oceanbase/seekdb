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
#include "sql/resolver/ddl/ob_explain_resolver.h"
#include "sql/resolver/ob_resolver.h"
#include "rust/sql_parser.h"

using namespace oceanbase;
using namespace sql;
using namespace common;
int ObExplainResolver::resolve(const peg::Node &query)
{
  int ret = OB_SUCCESS;
  ObExplainStmt *statement = create_stmt<ObExplainStmt>();
  const peg::Node &child = peg::explain_statement(query);
  ObStmt *child_statement = nullptr;
  ObExplainDisplayOpt display;
  display.with_tree_line_ = true;
  if (OB_ISNULL(statement)) {
    ret = OB_SQL_RESOLVER_NO_MEMORY;
  } else if (child.kind() != peg::NodeKind::Select && child.kind() != peg::NodeKind::Insert
             && child.kind() != peg::NodeKind::Update && child.kind() != peg::NodeKind::Delete) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "stmt type for EXPLAIN");
  } else {
    ObResolver resolver(params_);
    OZ(resolver.resolve(child, child_statement));
    if (OB_SUCC(ret)) {
      statement->set_explain_query_stmt(static_cast<ObDMLStmt *>(child_statement));
      switch (peg::explain_format(query)) {
        case peg::ExplainFormat::Default: break;
        case peg::ExplainFormat::Outline: statement->set_explain_format(EXPLAIN_OUTLINE); break;
        case peg::ExplainFormat::Extended: statement->set_explain_format(EXPLAIN_EXTENDED); break;
        case peg::ExplainFormat::Partitions: statement->set_explain_format(EXPLAIN_PARTITIONS); break;
        case peg::ExplainFormat::Traditional: statement->set_explain_format(EXPLAIN_TRADITIONAL); break;
        case peg::ExplainFormat::Basic: statement->set_explain_format(EXPLAIN_BASIC); break;
        case peg::ExplainFormat::ExtendedNoAddr: statement->set_explain_format(EXPLAIN_EXTENDED_NOADDR); break;
        case peg::ExplainFormat::PlanRegress: statement->set_explain_format(EXPLAIN_PLANREGRESS); break;
        case peg::ExplainFormat::Json:
          statement->set_explain_format(EXPLAIN_FORMAT_JSON);
          display.with_tree_line_ = false;
          break;
      }
      if (peg::explain_display(query) != peg::ExplainDisplay::Default) {
        display.with_tree_line_ = true;
        display.with_color_ = peg::explain_display(query) == peg::ExplainDisplay::PrettyColor;
      }
      statement->set_display_opt(display);
      if (peg::explain_has_into_table(query)) {
        const auto &node = peg::explain_into_table(query);
        ObString name;
        const auto node_text = node.text();
        OZ(ob_write_string(*allocator_, syntax_text(node_text), name));
        OX(statement->set_into_table(name));
      }
      if (peg::explain_has_statement_id(query)) {
        const auto &node = peg::explain_statement_id(query);
        ObString name;
        const auto literal = peg::expression_literal(node);
        if (literal.binary_notation != peg::BinaryNotation::None) {
          const auto bytes = peg::decode_binary_literal(literal.text, literal.binary_notation);
          OZ(ob_write_string(*allocator_,
              ObString(bytes.size(), reinterpret_cast<const char *>(bytes.data())), name));
        } else {
          OZ(ob_write_string(*allocator_, syntax_text(literal.text), name));
        }
        OX(statement->set_statement_id(name));
      }
    }
  }
  return ret;
}
