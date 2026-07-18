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

#ifndef _OB_SQL_EXPR_LEAST_H_
#define _OB_SQL_EXPR_LEAST_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprLeastGreatest : public ObMinMaxExprOperator
{
public:
  explicit ObExprLeastGreatest(common::ObIAllocator &alloc, ObExprOperatorType type,
                                    const char *name, int32_t param_num);
  virtual ~ObExprLeastGreatest() {}

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  int calc_result_typeN_mysql(ObExprResType &type,
                        ObExprResType *types_stack,
                        int64_t param_num,
                        common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int cast_param(const ObExpr &src_expr, ObEvalCtx &ctx,
                        const ObDatumMeta &dst_meta,
                        const ObCastMode &cm, ObIAllocator &allocator,
                        ObDatum &res_datum);
  static int cast_result(const ObExpr &src_expr, const ObExpr &dst_expr, ObEvalCtx &ctx,
                         const ObCastMode &cm,
                         ObDatum &expr_datum);
  static int calc_mysql(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum, bool least);
  // left < right: return true, else return false.
  static inline bool cmp_integer(const ObDatum &l_datum, const bool l_is_int,
                                 const ObDatum &r_datum, const bool r_is_int)
  {
    bool ret_bool = true;
    if (l_is_int && r_is_int) {
      ret_bool = l_datum.get_int() < r_datum.get_int();
    } else if (!l_is_int && !r_is_int) {
      ret_bool = l_datum.get_uint() < r_datum.get_uint();
    } else if (l_is_int && !r_is_int) {
      ret_bool = l_datum.get_int() < r_datum.get_uint();
    } else {
      ret_bool = l_datum.get_uint() < r_datum.get_int();
    }
    return ret_bool;
  }
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprLeastGreatest);
};

class ObExprLeast : public ObExprLeastGreatest
{
public:
  explicit  ObExprLeast(common::ObIAllocator &alloc);
  virtual ~ObExprLeast() {}

  static int calc_least(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprLeast);
};


}
}
#endif /* _OB_SQL_EXPR_LEAST_H_ */
