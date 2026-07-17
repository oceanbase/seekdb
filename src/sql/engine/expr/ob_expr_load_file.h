/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_LOAD_FILE_H_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_LOAD_FILE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprLoadFile : public ObStringExprOperator
{
public:
  explicit ObExprLoadFile(common::ObIAllocator &alloc);
  virtual ~ObExprLoadFile();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprLoadFile);
};

}
}

#endif
