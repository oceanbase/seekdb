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

#ifndef OCEANBASE_OB_SQL_EXPR_ARG_CASE_H_
#define OCEANBASE_OB_SQL_EXPR_ARG_CASE_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"

namespace oceanbase
{
namespace sql
{

typedef int (*ob_get_cmp_type_func) (common::ObObjType &type,
                                     const common::ObObjType &type1,
                                     const common::ObObjType &type2,
                                     const common::ObObjType &type3);

class ObExprArgCase : public ObExprOperator
{
public:
  explicit  ObExprArgCase(common::ObIAllocator &alloc);
  virtual ~ObExprArgCase();

  virtual int assign(const ObExprOperator &other);

  virtual int deserialize(const char *buf, const int64_t data_len, int64_t &pos);

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc_with_cast(common::ObObj &result,
                  const common::ObObj *objs_stack,
                  int64_t param_num,
                  common::ObCompareCtx &cmp_ctx,
                  common::ObCastCtx &cast_ctx,
                  const ObExprResType &res_type,
                  const ob_get_cmp_type_func get_cmp_type_func);
  void set_need_cast(bool need_cast) {need_cast_ = need_cast;}

  inline static int get_cmp_type(common::ObObjType &type,
                                 const common::ObObjType &type1,
                                 const common::ObObjType &type2,
                                 const common::ObObjType &type3)
  {
    UNUSED(type3);
    return ObExprResultTypeUtil::get_relational_cmp_type(type,type1,type2);
  }

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const;
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprArgCase);
private:
  bool need_cast_;
};
}
}
#endif // OCEANBASE_OB_SQL_EXPR_ARG_CASE_H_
