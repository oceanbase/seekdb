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

#ifndef _OB_SQL_EXPR_GREATEST_H_
#define _OB_SQL_EXPR_GREATEST_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_least.h"

namespace oceanbase
{
namespace sql
{
class ObExprGreatest : public ObExprLeastGreatest
{
public:
  explicit  ObExprGreatest(common::ObIAllocator &alloc);
  virtual ~ObExprGreatest() {}
  static int calc_greatest(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprGreatest);
};


}
}
#endif /* _OB_SQL_EXPR_GREATEST_H_ */
