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

#ifndef OCEANBASE_SQL_OB_EXPR_MAP
#define OCEANBASE_SQL_OB_EXPR_MAP

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/geo/ob_geo_utils.h"
#include "lib/udt/ob_array_utils.h"


namespace oceanbase
{
namespace sql
{
class ObExprMap : public ObFuncExprOperator
{
public:
  explicit ObExprMap(common::ObIAllocator &alloc);
  explicit ObExprMap(common::ObIAllocator &alloc, ObExprOperatorType type, 
                                const char *name, int32_t param_num, int32_t dimension);
  virtual ~ObExprMap();
  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types,
                                int64_t param_num, 
                                common::ObExprTypeCtx& type_ctx)
                                const override;
  static int eval_map(const ObExpr &expr,
                        ObEvalCtx &ctx,
                        ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  static int deduce_element_type(ObExecContext *exec_ctx, ObExprResType* types_stack,
                                 int64_t param_num, uint16_t &key_subid, uint16_t &value_subid);
  static int construct_key_array(ObEvalCtx &ctx, const ObExpr &expr,
                                 const ObObjType elem_type, ObIArrayType *&full_key_arr,
                                 uint32_t *&idx_arr, uint32_t &idx_count);

  DISALLOW_COPY_AND_ASSIGN(ObExprMap);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_MAP
