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

#ifndef OCEANBASE_SQL_OB_EXPR_JSON_SCHEMA_VALID_H_
#define OCEANBASE_SQL_OB_EXPR_JSON_SCHEMA_VALID_H_

#include "lib/json_type/ob_json_tree.h"
#include "lib/json_type/ob_json_bin.h"
#include "lib/json_type/ob_json_schema.h"
#include "sql/engine/expr/ob_expr_operator.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

struct ObExprJsonSchemaValidInfo : public ObIExprExtraInfo
{
  OB_UNIS_VERSION(1);
public:
  ObExprJsonSchemaValidInfo(common::ObIAllocator &alloc, ObExprOperatorType type)
      : ObIExprExtraInfo(alloc, type)
  {
  }

  virtual int deep_copy(common::ObIAllocator &allocator,
                        const ObExprOperatorType type,
                        ObIExprExtraInfo *&copied_info) const override;
  int init_json_schema_extra_info(ObIAllocator &alloc, ObExprCGCtx &op_cg_ctx, const ObRawExpr* raw_expr, bool& got_data);
  ObString json_schema_;
};

class ObExprJsonSchemaValid : public ObFuncExprOperator
{
  OB_UNIS_VERSION(1);
public:
  explicit ObExprJsonSchemaValid(common::ObIAllocator &alloc);
  virtual ~ObExprJsonSchemaValid();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx)
                                const override;
  static int eval_json_schema_valid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                    ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprJsonSchemaValid);
  const static uint8_t OB_JSON_SCHEMA_EXPR_ARG_NUM = 2;
private:
  ObString json_schema_;
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_JSON_SCHEMA_VALID_H_
