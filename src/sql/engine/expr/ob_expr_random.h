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

#ifndef OCEANBASE_SQL_OB_EXPR_FUNC_RANDOM_H_
#define OCEANBASE_SQL_OB_EXPR_FUNC_RANDOM_H_

#include <random>
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprRandom: public ObFuncExprOperator
{
  OB_UNIS_VERSION(1);
	class ObExprRandomCtx: public ObExprOperatorCtx
	{
	public:
		ObExprRandomCtx();
		virtual ~ObExprRandomCtx();
		void set_seed(uint64_t seed);
		void get_next_random(int64_t &res);
	private:
    std::mt19937_64 gen_;
	};
public:
	explicit ObExprRandom(common::ObIAllocator &alloc);
	virtual ~ObExprRandom();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
	inline void set_seed_const(bool is_seed_const);

  // engine 3.0
  virtual bool need_rt_ctx() const override { return true; }
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
  static int calc_random_expr_const_seed(const ObExpr &expr, ObEvalCtx &ctx,
                                         ObDatum &res_datum);
  static int calc_random_expr_nonconst_seed(const ObExpr &expr, ObEvalCtx &ctx,
                                            ObDatum &res_datum);
public:
  virtual int assign(const ObExprOperator &other) override;
private:
	bool is_seed_const_;
	// disallow copy
	DISALLOW_COPY_AND_ASSIGN(ObExprRandom);
};

inline void ObExprRandom::set_seed_const(bool is_seed_const)
{
	is_seed_const_ = is_seed_const;
}
} /* namespace sql */
} /* namespace oceanbase */

#endif /* OCEANBASE_SQL_OB_EXPR_FUNC_RANDOM_H_ */
