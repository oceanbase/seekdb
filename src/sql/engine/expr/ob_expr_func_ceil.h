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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FUNC_CEIL_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FUNC_CEIL_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprCeilFloor : public ObFuncExprOperator
{
  static const int16_t MAX_LIMIT_WITH_SCALE = 17;
  static const int16_t MAX_LIMIT_WITHOUT_SCALE = 18;
public:
  ObExprCeilFloor(common::ObIAllocator &alloc,
                  ObExprOperatorType type,
                  const char *name,
                  int32_t param_num,
                  int32_t dimension = NOT_ROW_DIMENSION);

  virtual ~ObExprCeilFloor();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const override;

  static int ceil_floor_decint(
      const bool is_floor, const ObDatum *arg_datum,
      const ObDatumMeta &in_meta, const ObDatumMeta &out_meta,
      ObDatum &res_datum);
      
  template <typename T, bool IS_FLOOR, bool NEG>
  static T ceil_floor_trival(T in, const T in_scale_power, const T in_scale_power_minus_one);

  template <typename LeftVec, typename ResVec>
  static int inner_calc_ceil_floor_vector(const ObExpr &expr,
                                         int intput_type,
                                         ObEvalCtx &ctx,
                                         const ObBitVector &skip,
                                         const EvalBound &bound);

  static int calc_ceil_floor_vector(const ObExpr &expr,
                           ObEvalCtx &ctx,
                           const ObBitVector &skip,
                           const EvalBound &bound);

  template <typename LeftVec, typename ResVec, bool IS_FLOOR>
  static int ceil_floor_decint_vector(const ObDatumMeta &in_meta,
                                              const ObDatumMeta &out_meta,
                                              LeftVec *left_vec,
                                              ResVec *res_vec,
                                              const int64_t &idx);

  static int inner_calc_ceil_floor_fixed_vector(const ObExpr &expr,
                                                 ObEvalCtx &ctx,
                                                 const ObBitVector &skip,
                                                 const EvalBound &bound);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCeilFloor);
};

class ObExprFuncCeil : public ObExprCeilFloor
{
public:
  explicit  ObExprFuncCeil(common::ObIAllocator &alloc);
  virtual ~ObExprFuncCeil();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprFuncCeil);
};

class ObExprFuncCeiling : public ObExprCeilFloor
{
public:
  explicit  ObExprFuncCeiling(common::ObIAllocator &alloc);
  virtual ~ObExprFuncCeiling();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprFuncCeiling);
};

class ObExprFuncFloor : public ObExprCeilFloor
{
public:
  explicit  ObExprFuncFloor(common::ObIAllocator &alloc);
  virtual ~ObExprFuncFloor();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprFuncFloor);
};
} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FUNC_CEIL_
