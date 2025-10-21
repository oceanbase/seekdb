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

#ifndef OCEANBASE_SQL_OB_EXPR_MAP_KEYS_
#define OCEANBASE_SQL_OB_EXPR_MAP_KEYS_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/geo/ob_geo_utils.h"
#include "lib/udt/ob_array_utils.h"

namespace oceanbase
{
namespace sql
{

class ObExprMapComponents : public ObFuncExprOperator
{
public:
  explicit ObExprMapComponents(common::ObIAllocator &alloc, ObExprOperatorType type, 
                                const char *name, int32_t param_num, int32_t dimension);
  virtual ~ObExprMapComponents();
  static int calc_map_components_result_type(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx,
                                bool is_key = true);
  static int eval_map_components(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res, bool is_key = true);
  static int eval_map_components_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                          const ObBitVector &skip, const EvalBound &bound, bool is_key = true);
  static  int get_map_components_arr(ObIAllocator &tmp_allocator,
                                ObEvalCtx &ctx,
                                ObString &map_blob, 
                                ObIArrayType *&arr_res, 
                                uint16_t &res_subschema_id,
                                uint16_t &subschema_id,
                                bool is_key = true);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprMapComponents);
};


class ObExprMapKeys : public ObExprMapComponents
{
public:
  explicit ObExprMapKeys(common::ObIAllocator &alloc);
  virtual ~ObExprMapKeys();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx)
                                const override;
  static int eval_map_keys(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_map_keys_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                          const ObBitVector &skip, const EvalBound &bound);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprMapKeys);
};

class ObExprMapValues : public ObExprMapComponents
{
public:
  explicit ObExprMapValues(common::ObIAllocator &alloc);
  virtual ~ObExprMapValues();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx)
                                const override;
  static int eval_map_values(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_map_values_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                          const ObBitVector &skip, const EvalBound &bound);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprMapValues);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_MAP_KEYS_
