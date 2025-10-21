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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_QUOTE_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_QUOTE_H_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprQuote: public ObStringExprOperator
{
public:
  ObExprQuote();
  explicit  ObExprQuote(common::ObIAllocator &alloc);
  virtual ~ObExprQuote();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
  static int calc_quote_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                 ObDatum &res_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  static const int16_t APPEND_LEN = 2;
  static const int16_t LEN_OF_NULL = 4;
  // quote string
  static int calc(common::ObString &res_str, common::ObString str,
                  common::ObCollationType coll_type, common::ObIAllocator *allocator);
  static int string_write_buf(const common::ObString &str, char *buf, const int64_t buf_len, int64_t &pos);
  DISALLOW_COPY_AND_ASSIGN(ObExprQuote);

};
}
}

#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_QUOTE_H_ */
