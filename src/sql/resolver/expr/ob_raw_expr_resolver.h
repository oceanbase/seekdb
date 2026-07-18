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

#ifndef _OB_RAW_EXPR_RESOLVER_H
#define _OB_RAW_EXPR_RESOLVER_H
#include "lib/oblog/ob_log.h"
#include "sql/parser/parse_node.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObRawExprResolver
{
public:
  ObRawExprResolver() {}

  virtual ~ObRawExprResolver() {}

  virtual int resolve(const ParseNode *node,
                      ObRawExpr *&expr,
                      common::ObIArray<ObQualifiedName> &columns,
                      common::ObIArray<ObVarInfo> &sys_vars,
                      common::ObIArray<ObSubQueryInfo> &sub_query_info,
                      common::ObIArray<ObAggFunRawExpr*> &aggr_exprs,
                      common::ObIArray<ObWinFunRawExpr*> &win_exprs,
                      common::ObIArray<ObUDFInfo> &udf_exprs,
                      common::ObIArray<ObOpRawExpr*> &op_exprs,
                      common::ObIArray<ObUserVarIdentRawExpr*> &user_var_exprs,
                      common::ObIArray<ObInListInfo> &inlist_infos,
                      common::ObIArray<ObMatchFunRawExpr*> &match_exprs) = 0;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObRawExprResolver);
  // function members
private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_RAW_EXPR_RESOLVER_H */
