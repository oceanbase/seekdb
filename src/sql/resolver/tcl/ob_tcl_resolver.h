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

#ifndef OCEANBASE_SQL_TCL_RESOLVER_
#define OCEANBASE_SQL_TCL_RESOLVER_

#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{

class ObTCLResolver : public ObStmtResolver
{
public:
  explicit ObTCLResolver(ObResolverParams &params);
  virtual ~ObTCLResolver();

  virtual int resolve_special_expr(ObRawExpr *&expr, ObStmtScope scope);
  virtual int resolve_sub_query_info(const common::ObIArray<ObSubQueryInfo> &subquery_info,
                                     const ObStmtScope upper_scope);
  virtual int resolve_columns(ObRawExpr *&expr, common::ObArray<ObQualifiedName> &columns);
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObTCLResolver);
};

}
}
#endif /* OCEANBASE_SQL_TCL_RESOLVER_ */
//// end of header file
