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

#ifndef _OB_EXPLAIN_RESOLVER_H
#define _OB_EXPLAIN_RESOLVER_H
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/dml/ob_dml_resolver.h"
#include "sql/resolver/ddl/ob_explain_stmt.h"

namespace oceanbase
{
namespace sql
{
  class ObExplainResolver : public ObDMLResolver
  {
  public:
    ObExplainResolver(ObResolverParams &params)
      : ObDMLResolver(params)
    {}
    virtual ~ObExplainResolver() {}
    virtual int resolve(const ParseNode &parse_tree);
    ObExplainStmt *get_explain_stmt();
    int resolve_columns(ObRawExpr *&expr, common::ObArray<ObQualifiedName> &columns)
    {
      UNUSED(expr);
      UNUSED(columns);
      return common::OB_SUCCESS;
    }
    virtual int resolve_order_item(const ParseNode &sort_node, OrderItem &order_item)
    {
      UNUSED(sort_node);
      UNUSED(order_item);
      return common::OB_SUCCESS;
    }
  };

}
}
#endif
