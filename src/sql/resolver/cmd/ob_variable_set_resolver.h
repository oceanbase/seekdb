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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_VARIALBLE_SET_RESOLVER_
#define OCEANBASE_SQL_RESOLVER_CMD_OB_VARIALBLE_SET_RESOLVER_

#include "sql/resolver/ob_stmt_resolver.h"

namespace oceanbase
{
namespace sql
{
class ObVariableSetResolver : public ObStmtResolver
{
public:
  explicit ObVariableSetResolver(ObResolverParams &params);
  virtual ~ObVariableSetResolver();

  virtual int resolve(const ParseNode &parse_tree);
  int resolve_value_expr(ParseNode &val_node, ObRawExpr *&value_expr);
  int resolve_subquery_info(const ObIArray<ObSubQueryInfo> &subquery_info, ObRawExpr *&value_expr);
private:
  int resolve_set_names(const ParseNode &parse_tree);
  DISALLOW_COPY_AND_ASSIGN(ObVariableSetResolver);
};

class ObAlterSessionSetResolver : public ObStmtResolver
{
public:
  explicit ObAlterSessionSetResolver(ObResolverParams &params);
  virtual ~ObAlterSessionSetResolver();

  virtual int resolve(const ParseNode &parse_tree);
private:
  DISALLOW_COPY_AND_ASSIGN(ObAlterSessionSetResolver);
};

}
}
#endif /* OCEANBASE_SQL_RESOLVER_CMD_OB_VARIALBLE_SET_RESOLVER_ */
