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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_HEX_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_HEX_

#include "sql/engine/expr/ob_expr_operator.h"
#include "share/object/ob_obj_cast.h"
namespace oceanbase
{
namespace sql
{
class ObExprHex : public ObStringExprOperator
{
public:
  explicit  ObExprHex(common::ObIAllocator &alloc);
  virtual ~ObExprHex();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &text,
                                common::ObExprTypeCtx &type_ctx) const;

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_hex(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // helper func
  static int get_uint64(const common::ObObj &obj, common::ObCastCtx &cast_ctx, uint64_t &out);
  static int number_uint64(const common::number::ObNumber &num_val, uint64_t &out);
  static int decimalint_uint64(const ObDatumMeta &in_meta, const ObDatum *datum, uint64_t &out);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprHex);
};

}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_HEX_ */
