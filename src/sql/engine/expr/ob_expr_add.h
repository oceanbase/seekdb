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

#ifndef _OB_EXPR_ADD_H_
#define _OB_EXPR_ADD_H_

#include <float.h>
#include <math.h>
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObRawExpr;
class ObExprCGCtx;

class ObExprAdd: public ObArithExprOperator
{
public:
  ObExprAdd();
  explicit  ObExprAdd(common::ObIAllocator &alloc, ObExprOperatorType type = T_OP_ADD);
  virtual ~ObExprAdd() {};
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc(common::ObObj &res,
                  const common::ObObj &ojb1,
                  const common::ObObj &obj2,
                  common::ObIAllocator *allocator,
                  common::ObScale scale);
  // add for aggregate function.

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

public:
  //very very effective implementation
  //if false is returned, the addition of multiplication will be stored in res
  template<typename T1, typename T2, typename T3>
  OB_INLINE static bool is_add_out_of_range(T1 val1, T2 val2, T3 &res)
  {
    return __builtin_add_overflow(val1, val2, &res);
  }
  OB_INLINE static bool is_int_int_out_of_range(int64_t val1, int64_t val2, int64_t res)
  {
    // top digit:
    // 0 + 0 = 0 : safe.
    // 0 + 0 = 1 : overflow.
    // 0 + 1     : safe.
    // 1 + 0     : safe.
    // 1 + 1 = 0 : underflow.
    // 1 + 1 = 1 : safe.
    return (val1 >> SHIFT_OFFSET) != (res >> SHIFT_OFFSET) &&
           (val2 >> SHIFT_OFFSET) != (res >> SHIFT_OFFSET);
  }
  OB_INLINE static bool is_int_uint_out_of_range(int64_t val1, uint64_t val2, uint64_t res)
  {
    // top digit:
    // 0 + 0     : safe.
    // 0 + 1 = 0 : overflow.
    // 0 + 1 = 1 : safe.
    // 1 + 0 = 0 : safe.
    // 1 + 0 = 1 : underflow.
    // 1 + 1     : safe.
    return (static_cast<uint64_t>(val1) >> SHIFT_OFFSET) == (res >> SHIFT_OFFSET) &&
           (val2 >> SHIFT_OFFSET) != (res >> SHIFT_OFFSET);
  }
  OB_INLINE static bool is_uint_uint_out_of_range(uint64_t val1, uint64_t val2, uint64_t res)
  {
    // top digit:
    // 0 + 0     : safe.
    // 0 + 1 = 0 : overflow.
    // 0 + 1 = 1 : safe.
    // 1 + 0 = 0 : overflow.
    // 1 + 0 = 1 : safe.
    // 1 + 1     : overflow.
    return (val1 >> SHIFT_OFFSET) + (val2 >> SHIFT_OFFSET) > (res >> SHIFT_OFFSET);
  }
  static int add_datetime(common::ObObj &res,
                          const common::ObObj &left,
                          const common::ObObj &right,
                          common::ObIAllocator *allocator,
                          common::ObScale scale);
private:
  static int add_int(common::ObObj &res,
                     const common::ObObj &left,
                     const common::ObObj &right,
                     common::ObIAllocator *allocator,
                     common::ObScale scale);
  static int add_uint(common::ObObj &res,
                      const common::ObObj &left,
                      const common::ObObj &right,
                      common::ObIAllocator *allocator,
                      common::ObScale scale);
  static int add_double(common::ObObj &res,
                        const common::ObObj &left,
                        const common::ObObj &right,
                        common::ObIAllocator *allocator,
                        common::ObScale scale);
  static int add_double_no_overflow(common::ObObj &res,
                                    const common::ObObj &left,
                                    const common::ObObj &right,
                                    common::ObIAllocator *allocator,
                                    common::ObScale scale);
  static int add_number(common::ObObj &res,
                        const common::ObObj &left,
                        const common::ObObj &right,
                        common::ObIAllocator *allocator,
                        common::ObScale scale);
  //disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprAdd);

public:

  static int add_null(EVAL_FUNC_ARG_DECL);

  static int add_int_int(EVAL_FUNC_ARG_DECL);
  static int add_int_int_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_int_int_vector(VECTOR_EVAL_FUNC_ARG_DECL);

  static int add_int_uint(EVAL_FUNC_ARG_DECL);
  static int add_int_uint_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_int_uint_vector(VECTOR_EVAL_FUNC_ARG_DECL);

  static int add_uint_uint(EVAL_FUNC_ARG_DECL);
  static int add_uint_uint_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_uint_uint_vector(VECTOR_EVAL_FUNC_ARG_DECL);

  static int add_uint_int(EVAL_FUNC_ARG_DECL);
  static int add_uint_int_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_uint_int_vector(VECTOR_EVAL_FUNC_ARG_DECL);

  static int add_float_float(EVAL_FUNC_ARG_DECL);
  static int add_float_float_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_float_float_vector(VECTOR_EVAL_FUNC_ARG_DECL);

  static int add_double_double(EVAL_FUNC_ARG_DECL);
  static int add_double_double_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_double_double_vector(VECTOR_EVAL_FUNC_ARG_DECL);

  static int add_number_number(EVAL_FUNC_ARG_DECL);
  static int add_number_number_batch(BATCH_EVAL_FUNC_ARG_DECL);

  static int add_number_number_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_number_datetime_common(EVAL_FUNC_ARG_DECL, bool number_left);
  OB_INLINE static int add_number_datetime(EVAL_FUNC_ARG_DECL)
  {
    return add_number_datetime_common(EVAL_FUNC_ARG_LIST, true);
  }
  static int add_number_datetime_batch(BATCH_EVAL_FUNC_ARG_DECL);

  OB_INLINE static int add_datetime_number(EVAL_FUNC_ARG_DECL)
  {
    return add_number_datetime_common(EVAL_FUNC_ARG_LIST, false);
  }
  static int add_datetime_number_batch(BATCH_EVAL_FUNC_ARG_DECL);

  static int add_datetime_datetime(EVAL_FUNC_ARG_DECL);
  static int add_datetime_datetime_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint32(EVAL_FUNC_ARG_DECL);
  static int add_decimalint32_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint32_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint64(EVAL_FUNC_ARG_DECL);
  static int add_decimalint64_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint64_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint128(EVAL_FUNC_ARG_DECL);
  static int add_decimalint128_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint128_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint256(EVAL_FUNC_ARG_DECL);
  static int add_decimalint256_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint256_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint512(EVAL_FUNC_ARG_DECL);
  static int add_decimalint512_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint512_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint512_with_check(EVAL_FUNC_ARG_DECL);
  static int add_decimalint512_with_check_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint512_with_check_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint32_oracle(EVAL_FUNC_ARG_DECL);
  static int add_decimalint32_oracle_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint32_oracle_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint64_oracle(EVAL_FUNC_ARG_DECL);
  static int add_decimalint64_oracle_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint64_oracle_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_decimalint128_oracle(EVAL_FUNC_ARG_DECL);
  static int add_decimalint128_oracle_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_decimalint128_oracle_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int8_t(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int8_t_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int8_t_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int16_t(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int16_t_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int16_t_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int32_t(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int32_t_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int32_t_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int64_t(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int64_t_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_int64_t_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_float(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_float_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_float_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_double(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_double_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_double_vector(VECTOR_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_uint64_t(EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_uint64_t_batch(BATCH_EVAL_FUNC_ARG_DECL);
  static int add_collection_collection_uint64_t_vector(VECTOR_EVAL_FUNC_ARG_DECL);

private:
  static ObArithFunc add_funcs_[common::ObMaxTC];
  static ObArithFunc agg_add_funcs_[common::ObMaxTC];
  static const int64_t SHIFT_OFFSET = 63;
};

// Add expr for aggregation, different with ObExprAdd:
//  No overflow check for float/double type.
class ObExprAggAdd : public ObExprAdd
{
public:
  explicit ObExprAggAdd(common::ObIAllocator &alloc)
      : ObExprAdd(alloc, T_OP_AGG_ADD)
  {
  }
};

}
}
#endif  /* _OB_EXPR_ADD_H_ */
