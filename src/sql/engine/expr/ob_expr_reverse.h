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

#ifndef OCEANBASE_SQL_EXPR_FUNC_REVERSE_
#define OCEANBASE_SQL_EXPR_FUNC_REVERSE_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprReverse : public ObStringExprOperator
{
public:
  explicit  ObExprReverse(common::ObIAllocator &alloc);
  virtual ~ObExprReverse();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                               ObExpr &rt_expr) const;
  static int do_reverse(const common::ObString &input_str,
                 const common::ObCollationType &cs_type,
                 common::ObIAllocator *allocator,
                 common::ObString &res_str);
  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprReverse);
};

inline int ObExprReverse::calc_result_type1(ObExprResType &type,
                                            ObExprResType &type1,
                                            common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (lib::is_mysql_mode() && ob_is_collection_sql_type(type1.get_type())) {
    type.set_collection(type1.get_subschema_id());
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObCollectionSQLType]).get_length());
  } else {
    type1.set_calc_type(common::ObVarcharType);
    type1.set_calc_collation_type(type1.get_collation_type());
    if (lib::is_mysql_mode()) {
      if (ObTextType == type1.get_type()
          || ObMediumTextType == type1.get_type()
          || ObLongTextType == type1.get_type()) {
        type1.set_calc_type(type1.get_type());
        type.set_type(ObLongTextType);
        const int32_t mbmaxlen = 4;
        const int32_t default_text_length =
              ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType].get_length() / mbmaxlen;
        type.set_length(default_text_length);
      } else {
        type.set_varchar();
        if (ObTinyTextType == type1.get_type()) {
          type.set_length(OB_MAX_TINYTEXT_LENGTH - 1);
        } else {
          type.set_length(type1.get_length());
        }
      }
      ret = aggregate_charsets_for_string_result(type, &type1, 1, type_ctx);
    } else {
      if (ob_is_character_type(type1.get_type(), type1.get_collation_type())
          || ob_is_varbinary_or_binary(type1.get_type(), type1.get_collation_type())
          || ObNullType == type1.get_type()) {
        type.set_type(type1.get_type());
        type.set_collation_type(type1.get_collation_type());
        type.set_collation_level(type1.get_collation_level());
        type.set_length(type1.get_length());
        type.set_length_semantics(type1.get_length_semantics());
      } else {
        ret = OB_ERR_INVALID_TYPE_FOR_OP;
        LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP,
                      ob_obj_type_str(ObCharType),
                      ob_obj_type_str(type1.get_type()));
      }
    }
  }
  return ret;
}

}
}

#endif /* OCEANBASE_SQL_EXPR_FUNC_REVERSE_ */
