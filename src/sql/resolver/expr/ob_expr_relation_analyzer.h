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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_EXPR_OB_EXPR_RELATION_ANALYZER_H_
#define OCEANBASE_SRC_SQL_RESOLVER_EXPR_OB_EXPR_RELATION_ANALYZER_H_

#include "lib/container/ob_se_array.h"
namespace oceanbase
{
namespace sql
{
class ObRawExpr;
class ObExprRelationAnalyzer
{
public:
  explicit ObExprRelationAnalyzer();
  int pull_expr_relation_id(ObRawExpr *expr);
private:
  int visit_expr(ObRawExpr &expr);
};
}  // namespace sql
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_SQL_RESOLVER_EXPR_OB_EXPR_RELATION_ANALYZER_H_ */
