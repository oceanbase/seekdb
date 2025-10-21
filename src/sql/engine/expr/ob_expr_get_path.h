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

#ifndef OB_EXPR_GET_PATH_H
#define OB_EXPR_GET_PATH_H

#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{

struct ObDataAccessPathExtraInfo : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  ObDataAccessPathExtraInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
    : ObIExprExtraInfo(alloc, type)
  {}
  virtual ~ObDataAccessPathExtraInfo() {}
  virtual int deep_copy(common::ObIAllocator &allocator,
                        const ObExprOperatorType type,
                        ObIExprExtraInfo *&copied_info) const override;
  TO_STRING_KV(K(type_), K(data_access_path_));
  ObString data_access_path_;
};


class ObExprGetPath: public ObFuncExprOperator
{
public:
  explicit ObExprGetPath(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_GET_PATH, N_GET_PATH, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
  virtual ~ObExprGetPath() {}
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprTypeCtx &type_ctx) const
  {
    UNUSED(type1);
    UNUSED(type2);
    UNUSED(type_ctx);
    type.set_varchar();
    type.set_collation_type(CS_TYPE_BINARY);
    return common::OB_SUCCESS;
  }
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override {
    return common::OB_NOT_SUPPORTED;
  }
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprGetPath);
};

}
}
#endif // OB_EXPR_GET_PATH_H


