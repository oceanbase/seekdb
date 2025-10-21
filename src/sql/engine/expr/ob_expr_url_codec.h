
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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_CODEC_URL_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_CODEC_URL_

#include "lib/oblog/ob_log.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprURLCODEC : public ObFuncExprOperator
{
public:
  explicit ObExprURLCODEC(common::ObIAllocator &alloc, ObExprOperatorType type, const char *name) :
    ObFuncExprOperator(alloc, type, name, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
  {}
  virtual ~ObExprURLCODEC(){};

  virtual int calc_result_type1(ObExprResType &type, ObExprResType &type_1,
                                common::ObExprTypeCtx &type_ctx) const override;

  static int eval_url_codec(EVAL_FUNC_ARG_DECL, bool is_encode);
  static int eval_url_codec_batch(BATCH_EVAL_FUNC_ARG_DECL, bool is_encode);
  static int eval_url_codec_vector(VECTOR_EVAL_FUNC_ARG_DECL, bool is_encode);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprURLCODEC);
};

class ObExprURLEncode : public ObExprURLCODEC
{
public:
  explicit ObExprURLEncode(common::ObIAllocator &alloc);
  virtual ~ObExprURLEncode(){};

  int cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const;

  static int eval_url_encode(EVAL_FUNC_ARG_DECL);
  static int eval_url_encode_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int eval_url_encode_vector(VECTOR_EVAL_FUNC_ARG_DECL);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprURLEncode);
};

class ObExprURLDecode : public ObExprURLCODEC
{
public:
  explicit ObExprURLDecode(common::ObIAllocator &alloc);
  virtual ~ObExprURLDecode(){};

  int cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const;

  static int eval_url_decode(EVAL_FUNC_ARG_DECL);
  static int eval_url_decode_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int eval_url_decode_vector(VECTOR_EVAL_FUNC_ARG_DECL);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprURLDecode);
};

}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_CODEC_URL_ */
