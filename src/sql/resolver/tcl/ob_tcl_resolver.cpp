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

#include "sql/resolver/tcl/ob_tcl_resolver.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
namespace sql
{


ObTCLResolver::ObTCLResolver(ObResolverParams &params)
  : ObStmtResolver(params)
{
}

ObTCLResolver::~ObTCLResolver()
{
}

int ObTCLResolver::resolve_special_expr(ObRawExpr *&expr, ObStmtScope scope)
{
  UNUSED(expr);
  UNUSED(scope);
  return OB_SUCCESS;
}
int ObTCLResolver::resolve_sub_query_info(const ObIArray<ObSubQueryInfo> &subquery_info,
                                          const ObStmtScope upper_scope)
{
  UNUSED(subquery_info);
  UNUSED(upper_scope);
  return OB_SUCCESS;
}
int ObTCLResolver::resolve_columns(ObRawExpr *&expr, ObArray<ObQualifiedName> &columns)
{
  UNUSED(expr);
  UNUSED(columns);
  return OB_SUCCESS;
}


}  // namespace sql
}  // namespace oceanbase
