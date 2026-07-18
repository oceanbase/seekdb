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

#ifndef OCEANBASE_SQL_ENGINE_OB_SQL_EXPR_FUNC_ROUND_
#define OCEANBASE_SQL_ENGINE_OB_SQL_EXPR_FUNC_ROUND_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
#define ROUND_MIN_SCALE -30
#define ROUND_MAX_SCALE 30
class ObExprFuncRound : public ObFuncExprOperator
{
public:
  explicit  ObExprFuncRound(common::ObIAllocator &alloc);
  virtual ~ObExprFuncRound();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }

  static int calc_round_expr_numeric1_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size);

  static int calc_round_expr_numeric2_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size);

  static int calc_round_expr_datetime1_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size);

  template <typename LeftVec, typename ResVec>
  static int inner_calc_round_expr_numeric1_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);

  template <typename LeftVec, typename ResVec>
  static int inner_calc_round_expr_numeric2_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);

  template <typename LeftVec, typename ResVec>
  static int inner_calc_round_expr_datetime1_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);
                             
  static int calc_round_expr_numeric1_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);

  static int calc_round_expr_numeric2_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);

  static int calc_round_expr_datetime1_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);

  static int do_round_decimalint(
      const int16_t in_prec, const int16_t in_scale,
      const int16_t out_prec, const int16_t out_scale, const int64_t round_scale,
      const ObDatum &in_datum, ObDecimalIntBuilder &res_val);

  static int calc_round_decimalint(
      const ObDatumMeta &in_meta, const ObDatumMeta &out_meta, const int64_t round_scale,
      const ObDatum &in_datum, ObDatum &res_datum);


  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // engine 3.0
  int se_deduce_type(ObExprResType &type,
                     ObExprResType *params,
                     int64_t param_num,
                     common::ObExprTypeCtx &type_ctx) const;
  static int set_res_scale_prec(common::ObExprTypeCtx &type_ctx, ObExprResType *params,
                                int64_t param_num, const common::ObObjType &res_type,
                                ObExprResType &type);
  static int set_res_and_calc_type(ObExprResType *params, int64_t param_num,
                                   common::ObObjType &res_type);
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprFuncRound);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_OB_SQL_EXPR_FUNC_ROUND_
