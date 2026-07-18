/**
 * OceanBase seekdb - Document AI: LOAD_FILE(location_name, file_name) -> BLOB.
 *
 * Scalar sys function. Resolves a LOCATION by name via the schema guard, reads
 * the local file:// file, and returns its bytes as a BLOB.
 *
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
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
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const override;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprLoadFile);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_EXPR_LOAD_FILE_H_
