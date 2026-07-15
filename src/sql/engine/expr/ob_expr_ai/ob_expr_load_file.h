/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SQL_OB_EXPR_LOAD_FILE_H_
#define OCEANBASE_SQL_OB_EXPR_LOAD_FILE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprLoadFile : public ObFuncExprOperator
{
public:
  explicit ObExprLoadFile(common::ObIAllocator &alloc);
  virtual ~ObExprLoadFile();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &location,
                                ObExprResType &filename,
                                common::ObExprTypeCtx &type_ctx) const override;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);

private:
  static int build_local_path(common::ObIAllocator &allocator,
                              const common::ObString &base_url,
                              const common::ObString &filename,
                              common::ObString &path);
  static int read_file_to_datum(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                const common::ObString &path,
                                ObDatum &res);
  DISALLOW_COPY_AND_ASSIGN(ObExprLoadFile);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_EXPR_LOAD_FILE_H_
