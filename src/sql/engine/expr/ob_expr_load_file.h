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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_LOAD_FILE_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_LOAD_FILE_

#include "sql/engine/expr/ob_expr_operator.h"

#ifndef N_LOAD_FILE
#define N_LOAD_FILE "load_file"
#endif

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
  static int eval_load_file(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  static int build_file_path(const common::ObString &location_url,
                             const common::ObString &file_name,
                             common::ObSqlString &file_path);
  static int read_whole_file(const char *file_path,
                             const ObExpr &expr,
                             ObEvalCtx &ctx,
                             ObDatum &expr_datum);
  DISALLOW_COPY_AND_ASSIGN(ObExprLoadFile);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_LOAD_FILE_ */
