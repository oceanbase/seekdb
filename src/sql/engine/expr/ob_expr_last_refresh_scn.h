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

#ifndef OCEANBASE_SQL_OB_EXPR_LAST_REFRESH_SCN_H_
#define OCEANBASE_SQL_OB_EXPR_LAST_REFRESH_SCN_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "share/scn.h"

namespace oceanbase
{
namespace sql
{
class ObExprLastRefreshScn : public ObFuncExprOperator
{
public:
  explicit  ObExprLastRefreshScn(common::ObIAllocator &alloc);
  virtual ~ObExprLastRefreshScn();

  struct LastRefreshScnExtraInfo : public ObIExprExtraInfo
  {
    OB_UNIS_VERSION(1);
  public:
    LastRefreshScnExtraInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
        : ObIExprExtraInfo(alloc, type),
          mview_id_(share::OB_INVALID_SCN_VAL)
    {
    }
    virtual ~LastRefreshScnExtraInfo() { }
    virtual int deep_copy(common::ObIAllocator &allocator,
                        const ObExprOperatorType type,
                        ObIExprExtraInfo *&copied_info) const override;
    uint64_t mview_id_;
  };

  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_last_refresh_scn(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int get_last_refresh_scn_sql(const share::SCN &scn,
                                      const ObIArray<uint64_t> &mview_ids,
                                      ObSqlString &sql);

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprLastRefreshScn);
};

} //sql
} //oceanbase
#endif //OCEANBASE_SQL_OB_EXPR_CURRENT_SCN_H_
