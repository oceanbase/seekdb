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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_BIT_COUNT_H_
#define OCEANBASE_SQL_ENGINE_EXPR_BIT_COUNT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprBitCount: public ObBitwiseExprOperator {
public:
	explicit ObExprBitCount(common::ObIAllocator &alloc);
	virtual ~ObExprBitCount();
  // for static typing engine
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const override;
  static int calc_bitcount_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                ObDatum& res_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
	static const uint8_t char_to_num_bits[256];
	DISALLOW_COPY_AND_ASSIGN(ObExprBitCount);
};
} /* namespace sql */
} /* namespace oceanbase */

#endif /* OCEANBASE_SQL_ENGINE_EXPR_BIT_COUNT_H_ */
