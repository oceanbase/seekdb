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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_RESULT_TYPE_UTIL_
#define OCEANBASE_SQL_ENGINE_EXPR_RESULT_TYPE_UTIL_

#include "lib/timezone/ob_timezone_info.h"
#include "lib/container/ob_bit_set.h"
#include "common/object/ob_obj_type.h"
#include "common/expression/ob_expr_string_buf.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_res_type_map.h"

namespace oceanbase
{
namespace sql
{

class ObArithResultTypeMap;
class ObExprResultTypeUtil
{
public:

  /* mysql calculates the expression as follows: first, determine a target type based on the column type,
   * then during the comparison phase, convert the values to this type, and then perform the comparison.
   * Therefore, the get_relational_cmp_type function is used to calculate the target type
   **/

  static int get_relational_cmp_type(common::ObObjType &type,
                                     const common::ObObjType &type1,
                                     const common::ObObjType &type2);

  static int get_relational_equal_type(common::ObObjType &type,
                                       const common::ObObjType &type1,
                                       const common::ObObjType &type2);

  static int get_relational_result_type(common::ObObjType &type,
                                        const common::ObObjType &type1,
                                        const common::ObObjType &type2);


  static int get_merge_result_type(common::ObObjType &type,
                                   const common::ObObjType &type1,
                                   const common::ObObjType &type2);


  static int get_abs_result_type(common::ObObjType &type,
                                 const common::ObObjType &type1);

  static int get_neg_result_type(common::ObObjType &type,
                                 const common::ObObjType &type1);

  static int get_round_result_type(common::ObObjType &type,
                                   const common::ObObjType &type1);

  static int get_div_result_type(common::ObObjType &result_type,
                                 common::ObObjType &result_ob1_type,
                                 common::ObObjType &result_ob2_type,
                                 const common::ObObjType type1,
                                 const common::ObObjType type2);

  static int get_div_result_type(ObExprResType &res_type,
                                 const ObExprResType &res_type1,
                                 const ObExprResType &res_type2);

  static int get_int_div_result_type(common::ObObjType &result_type,
                                     common::ObObjType &result_ob1_type,
                                     common::ObObjType &result_ob2_type,
                                     const common::ObObjType type1,
                                     const common::ObObjType type2);

  static int get_int_div_result_type(ObExprResType &res_type,
                                     const ObExprResType &res_type1,
                                     const ObExprResType &res_type2);

  static int get_int_div_calc_type(common::ObObjType &calc_type,
                                   common::ObObjType &calc_ob1_type,
                                   common::ObObjType &calc_ob2_type,
                                   const common::ObObjType type1,
                                   const common::ObObjType type2);

  static int get_mod_result_type(common::ObObjType &result_type,
                                 common::ObObjType &result_ob1_type,
                                 common::ObObjType &result_ob2_type,
                                 const common::ObObjType type1,
                                 const common::ObObjType type2);

  static int get_mod_result_type(ObExprResType &res_type,
                                 const ObExprResType &res_type1,
                                 const ObExprResType &res_type2);

  static int get_remainder_result_type(common::ObObjType &result_type,
                                 common::ObObjType &result_ob1_type,
                                 common::ObObjType &result_ob2_type,
                                 const common::ObObjType type1,
                                 const common::ObObjType type2);

  static int get_arith_result_type(common::ObObjType &result_type,
                                   common::ObObjType &result_ob1_type,
                                   common::ObObjType &result_ob2_type,
                                   const common::ObObjType type1,
                                   const common::ObObjType type2);


  static int get_mul_result_type(common::ObObjType &result_type,
                                 common::ObObjType &result_ob1_type,
                                 common::ObObjType &result_ob2_type,
                                 const common::ObObjType type1,
                                 const common::ObObjType type2);

  static int get_mul_result_type(ObExprResType &res_type,
                                 const ObExprResType &res_type1,
                                 const ObExprResType &res_type2);

  static int get_add_result_type(common::ObObjType &result_type,
                                 common::ObObjType &result_ob1_type,
                                 common::ObObjType &result_ob2_type,
                                 const common::ObObjType type1,
                                 const common::ObObjType type2);

  static int get_minus_result_type(common::ObObjType &result_type,
                                   common::ObObjType &result_ob1_type,
                                   common::ObObjType &result_ob2_type,
                                   const common::ObObjType type1,
                                   const common::ObObjType type2);

  static int get_add_result_type(ObExprResType &res_type,
                                 const ObExprResType &res_type1,
                                 const ObExprResType &res_type2);

  static int get_minus_result_type(ObExprResType &res_type,
                                   const ObExprResType &res_type1,
                                   const ObExprResType &res_type2);


  static int get_arith_calc_type(common::ObObjType &calc_type,
                                 common::ObObjType &calc_ob1_type,
                                 common::ObObjType &calc_ob2_type,
                                 const common::ObObjType type1,
                                 const common::ObObjType type2,
                                 const ObArithResultTypeMap::OP oper);
  static int deduce_max_string_length_oracle(const common::ObDataTypeCastParams &dtc_params,
                                             const ObExprResType &orig_type,
                                             const ObExprResType &target_type,
                                             common::ObLength &length,
                                             const int16_t calc_ls = common::LS_INVALIED);
  OB_INLINE static int get_add_calc_type(common::ObObjType &calc_type,
                                         common::ObObjType &calc_ob1_type,
                                         common::ObObjType &calc_ob2_type,
                                         const common::ObObjType type1,
                                         const common::ObObjType type2)
  {
    return get_arith_calc_type(calc_type, calc_ob1_type, calc_ob2_type, type1, type2,
                               ObArithResultTypeMap::OP::ADD);
  }
  OB_INLINE static int get_minus_calc_type(common::ObObjType &calc_type,
                                           common::ObObjType &calc_ob1_type,
                                           common::ObObjType &calc_ob2_type,
                                           const common::ObObjType type1,
                                           const common::ObObjType type2)
  {
    return get_arith_calc_type(calc_type, calc_ob1_type, calc_ob2_type, type1, type2,
                               ObArithResultTypeMap::OP::SUB);
  }
  OB_INLINE static int get_mul_calc_type(common::ObObjType &calc_type,
                                           common::ObObjType &calc_ob1_type,
                                           common::ObObjType &calc_ob2_type,
                                           const common::ObObjType type1,
                                           const common::ObObjType type2)
  {
    return get_arith_calc_type(calc_type, calc_ob1_type, calc_ob2_type, type1, type2,
                               ObArithResultTypeMap::OP::MUL);
  }
  OB_INLINE static int get_div_calc_type(common::ObObjType &calc_type,
                                           common::ObObjType &calc_ob1_type,
                                           common::ObObjType &calc_ob2_type,
                                           const common::ObObjType type1,
                                           const common::ObObjType type2)
  {
    return get_div_result_type(calc_type, calc_ob1_type, calc_ob2_type, type1, type2);
  }
  OB_INLINE static int get_mod_calc_type(common::ObObjType &calc_type,
                                           common::ObObjType &calc_ob1_type,
                                           common::ObObjType &calc_ob2_type,
                                           const common::ObObjType type1,
                                           const common::ObObjType type2)
  {
    return get_arith_calc_type(calc_type, calc_ob1_type, calc_ob2_type, type1, type2,
                               ObArithResultTypeMap::OP::MOD);
  }
  static int get_array_calc_type(ObExecContext *exec_ctx,
                                 const ObExprResType &type1,
                                 const ObExprResType &type2,
                                 ObExprResType &calc_type);
  static int get_array_calc_type(ObExecContext *exec_ctx,
                                 const ObDataType &coll_elem1_type,
                                 const ObDataType &coll_elem2_type,
                                 uint32_t depth,
                                 ObExprResType &calc_type,
                                 ObObjMeta &element_meta);
  static int get_deduce_element_type(ObExprResType &input_type, ObDataType &elem_type);
  static int assign_type_array(const ObIArray<ObRawExprResType> &src, ObIArray<ObExprResType> &dest);
  static int get_collection_calc_type(ObExecContext *exec_ctx,
                                      const ObExprResType &type1,
                                      const ObExprResType &type2,
                                      ObExprResType &calc_type);
};


}
}
#endif  /* OCEANBASE_SQL_ENGINE_EXPR_RESULT_TYPE_UTIL_ */
