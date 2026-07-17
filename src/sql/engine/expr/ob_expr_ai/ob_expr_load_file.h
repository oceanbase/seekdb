/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef OCEANBASE_SQL_ENGINE_EXPR_AI_OB_EXPR_LOAD_FILE_H_
#define OCEANBASE_SQL_ENGINE_EXPR_AI_OB_EXPR_LOAD_FILE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprLoadFile : public ObFuncExprOperator
{
public:
  explicit ObExprLoadFile(common::ObIAllocator &alloc);
  virtual ~ObExprLoadFile() = default;

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &location_type,
                                ObExprResType &file_type,
                                common::ObExprTypeCtx &type_ctx) const override;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);

private:
  static bool is_safe_relative_file_name(const common::ObString &file_name);
  DISALLOW_COPY_AND_ASSIGN(ObExprLoadFile);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_AI_OB_EXPR_LOAD_FILE_H_
