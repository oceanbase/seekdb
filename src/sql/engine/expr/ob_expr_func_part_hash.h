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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FUNC_HASH_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FUNC_HASH_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObTaskExecutorCtx;
class ObExprFuncPartHashBase : public ObFuncExprOperator
{
public:
  ObExprFuncPartHashBase(common::ObIAllocator &alloc, ObExprOperatorType type,
          const char *name, int32_t param_num, int32_t dimension,
          bool is_internal_for_mysql = false,
          bool is_internal_for_oracle = false);
  template<typename T>
  static int calc_value_for_mysql(const T &input, T &output, const common::ObObjType input_type);
};

class ObTaskExecutorCtx;
class ObExprFuncPartHash : public ObExprFuncPartHashBase
{
public:
  explicit  ObExprFuncPartHash(common::ObIAllocator &alloc);
  virtual ~ObExprFuncPartHash();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc_value(common::ObExprCtx &expr_ctx,
                        const common::ObObj *objs_stack,
                        int64_t param_num,
                        common::ObObj &result);
  static int calc_hash_value_with_seed(const common::ObObj &obj,
                                        int64_t seed,
                                        uint64_t &res);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_part_hash(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  static bool is_oracle_supported_type(const common::ObObjType type);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprFuncPartHash);

};

}  // namespace sql
}  // namespace oceanbase
#endif //OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FUNC_HASH_
