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

#ifndef _OB_EXPR_TO_TYPE_H_
#define _OB_EXPR_TO_TYPE_H_

#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprToType: public ObFuncExprOperator
{
  OB_UNIS_VERSION_V(1);
public:
  ObExprToType();
  explicit ObExprToType(common::ObIAllocator &alloc);
  virtual ~ObExprToType() {};

  virtual int calc_result_type1(ObExprResType &type, ObExprResType &type1, common::ObExprTypeCtx &type_ctx) const;

  virtual int assign(const ObExprOperator &other);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const;
public:
  OB_INLINE void set_expect_type(common::ObObjType expect_type) { expect_type_ = expect_type; }
  OB_INLINE void set_cast_mode(common::ObCastMode cast_mode) { cast_mode_ = cast_mode; }
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  int calc_result_type_for_literal(ObExprResType &type, ObExprResType &type1, common::ObExprTypeCtx &type_ctx) const;
  int calc_result_type_for_column(ObExprResType &type, ObExprResType &type1, common::ObExprTypeCtx &type_ctx) const;
private:
  // data members
  common::ObObjType expect_type_;
  common::ObCastMode cast_mode_;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprToType);
};

}
}
#endif  /* _OB_EXPR_TO_TYPE_H_ */
