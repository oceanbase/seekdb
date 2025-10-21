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

#ifndef _OCEANBASE_SQL_OB_EXPR_ORACLE_TO_CHAR_H_
#define _OCEANBASE_SQL_OB_EXPR_ORACLE_TO_CHAR_H_
#include "lib/ob_name_def.h"
#include "lib/allocator/ob_allocator.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace common {
struct ObTime;
}
namespace sql
{

class ObExprToCharCommon : public ObStringExprOperator
{
public:
  using ObStringExprOperator::ObStringExprOperator;

  static int number_to_char(common::ObObj &result,
                            const common::ObObj *objs_array,
                            int64_t param_num,
                            common::ObExprCtx &expr_ctx);
  static int datetime_to_char(common::ObObj &result,
                              const common::ObObj *objs_array,
                              int64_t param_num,
                              common::ObExprCtx &expr_ctx);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }

  static int eval_to_char(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int eval_oracle_to_char(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  // for static engine batch
  static int eval_oracle_to_char_batch(
      const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size);
  static int eval_to_char_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  template <typename LeftVec, typename ResVec>
  static int inner_eval_to_char_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  DECLARE_SET_LOCAL_SESSION_VARS;

protected:



  // functions for static typing engine, it's hard to reuse the code of old engine,
  // we copy the old functions adapt it to new engine.
  static int is_valid_to_char_number(const ObExpr &expr);

  static int datetime_to_char(const ObExpr &expr,
                              ObEvalCtx &ctx,
                              common::ObIAllocator &alloc,
                              const char *&input_ptr,
                              uint32_t input_len,
                              const common::ObString &fmt,
                              const common::ObString &nlsparam,
                              common::ObString &res);

  static int datetime_to_char(const ObExpr &expr,
                              ObEvalCtx &ctx,
                              common::ObIAllocator &alloc,
                              const common::ObDatum &input,
                              const common::ObString &fmt,
                              const common::ObString &nlsparam,
                              common::ObString &res);

  static int number_to_char(const ObExpr &expr, ObEvalCtx &ctx,
                            common::ObIAllocator &alloc,
                            const char *&input_ptr,
                            uint32_t input_len,
                            common::ObString &fmt_str,
                            const common::ObString &nlsparam,
                            common::ObString &res);
  static int number_to_char(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            common::ObIAllocator &alloc,
                            const common::ObDatum &input,
                            common::ObString &fmt,
                            const common::ObString &nlsparam,
                            common::ObString &res);

  static int process_number_sci_value(const ObExpr &expr,
                                      common::ObIAllocator &alloc,
                                      const common::ObDatum &input,
                                      const int scale,
                                      common::ObString &res);


  static int convert_to_ob_time(ObEvalCtx &ctx,
                                const common::ObDatum &input,
                                const common::ObObjType input_type,
                                const ObTimeZoneInfo *tz_info,
                                common::ObTime &ob_time);
  static int convert_timelike_to_str(const ObExpr &expr,
                                         ObEvalCtx &ctx, ObIAllocator &alloc,
                                         const ObDatum &input,
                                         const ObObjType input_type,
                                         ObString &res);

  static int set_expr_ascii_result(const ObExpr &expr, ObEvalCtx &ctx, const char *& in_ptr,
                                      uint32_t &in_len,
                                      const ObString &res, const bool is_ascii,
                                      const common::ObCollationType src_coll_type);
};

class ObExprOracleToChar : public ObExprToCharCommon
{
public:
  explicit ObExprOracleToChar(common::ObIAllocator &alloc);
  virtual ~ObExprOracleToChar();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *type_array,
                                int64_t params_count,
                                common::ObExprTypeCtx &type_ctx) const;
  static int eval_oracle_to_char(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
};

class ObExprToChar : public ObExprToCharCommon
{
public:
  explicit ObExprToChar(common::ObIAllocator &alloc);
  virtual ~ObExprToChar();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *type_array,
                                int64_t params_count,
                                common::ObExprTypeCtx &type_ctx) const;
};

}
}

#endif // OB_EXPR_ORACLE_TO_CHAR_H_
