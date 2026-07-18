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

#ifndef OCEANBASE_SQL_OB_EXPR_EXTRACT_H_
#define OCEANBASE_SQL_OB_EXPR_EXTRACT_H_

#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlanCtx;
class ObExprExtract : public ObFuncExprOperator
{
public:
  explicit  ObExprExtract(common::ObIAllocator &alloc);
  virtual ~ObExprExtract();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &date_unit,
                                ObExprResType &date,
                                common::ObExprTypeCtx &type_ctx) const;

  static int calc(ObObjType date_type,
      const ObDatum &date,
      const ObDateUnitType extract_field,
      const ObScale scale,
      const ObCastMode cast_mode,
      const ObTimeZoneInfo *tz_info,
      const int64_t cur_ts_value,
      const ObDateSqlMode date_sql_mode,
      bool has_lob_header,
      bool &is_null,
      int64_t &result);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_extract_mysql(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  // for static engine batch
  static int calc_extract_mysql_batch(
      const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size);
  static int calc_extract_mysql_vector(
      const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);

private:
  int set_result_type_oracle(common::ObExprTypeCtx &type_ctx,
                             const ObExprResType &date_unit, 
                             ObExprResType &res_type) const;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprExtract);

};

} //sql
} //oceanbase
#endif //OCEANBASE_SQL_OB_EXPR_EXTRACT_H_
