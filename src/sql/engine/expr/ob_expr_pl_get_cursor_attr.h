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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_GET_CURSOR_ATTR_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_GET_CURSOR_ATTR_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_i_expr_extra_info.h"
#include "pl/ob_pl_type.h"

namespace oceanbase
{
namespace sql
{
class ObExprPLGetCursorAttr : public ObFuncExprOperator
{
  OB_UNIS_VERSION(1);
public:
  explicit ObExprPLGetCursorAttr(common::ObIAllocator &alloc);
  virtual ~ObExprPLGetCursorAttr();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_pl_get_cursor_attr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int assign(const ObExprOperator &other);
  void set_pl_get_cursor_attr_info(const pl::ObPLGetCursorAttrInfo &cursor_info)
  {
    pl_cursor_info_ = cursor_info;
  }
  const pl::ObPLGetCursorAttrInfo& get_pl_get_cursor_attr_info() const
  {
    return pl_cursor_info_;
  }
public:
  struct ExtraInfo : public ObIExprExtraInfo
  {
    OB_UNIS_VERSION(1);
  public:
    ExtraInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
      : ObIExprExtraInfo(alloc, type) {}
    virtual ~ExtraInfo() {}
    static int init_pl_cursor_info(ObIAllocator *allocator,
                                   const ObExprOperatorType type,
                                   const pl::ObPLGetCursorAttrInfo &cursor_info,
                                   ObExpr &rt_expr);
    virtual int deep_copy(common::ObIAllocator &allocator,
                          const ObExprOperatorType type,
                          ObIExprExtraInfo *&copied_info) const override;
    TO_STRING_KV(K_(pl_cursor_info));

    pl::ObPLGetCursorAttrInfo pl_cursor_info_;
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprPLGetCursorAttr);
private:
  pl::ObPLGetCursorAttrInfo pl_cursor_info_;
};

}
}

#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_PL_GET_CURSOR_ATTR_H_ */
