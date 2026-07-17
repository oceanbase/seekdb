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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FIELD_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FIELD_

#include "sql/engine/expr/ob_expr_operator.h"
#include "share/object/ob_obj_cast.h"

namespace oceanbase
{
namespace sql
{
class ObExprField : public ObVectorExprOperator
{
public:

  explicit  ObExprField(common::ObIAllocator &alloc);
  virtual ~ObExprField() {};
  virtual int assign(const ObExprOperator &other);
public:
  //serialize and deserialize
  virtual int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  virtual int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  virtual int64_t get_serialize_size() const;
public:
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *type_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;

  void set_need_cast(bool need_cast) {need_cast_ = need_cast;}

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_field(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprField);
private:
  bool need_cast_;
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_FIELD_
