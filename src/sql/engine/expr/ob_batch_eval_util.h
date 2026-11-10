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

#ifndef OCEANBASE_EXPR_OB_BATCH_EVAL_UTIL_H_
#define OCEANBASE_EXPR_OB_BATCH_EVAL_UTIL_H_

#include <type_traits>

#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/expr/ob_expr_util.h"

namespace oceanbase
{
namespace sql
{

using common::OB_SUCCESS;
using common::number::ObNumber;

// Batch evaluate operand of binary operator
int binary_operand_batch_eval(const ObExpr &expr,
                              ObEvalCtx &ctx,
                              const ObBitVector &skip,
                              const int64_t size,
                              const bool null_short_circuit);

template <typename ArithOp>
struct ObDoArithBatchEval
{
  template <typename... Args>
  inline int operator()(const ObExpr &expr,
                        ObEvalCtx &ctx,
                        const ObBitVector &skip,
                        const int64_t size,
                        const DatumVector &res,
                        const DatumVector &left,
                        const DatumVector &right,
                        Args &...args) const
  {
    int ret = OB_SUCCESS;
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    // Datum operators may allocate a variable-length result from the current batch slot.
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
    const int64_t step_size = sizeof(uint16_t) * CHAR_BIT;
    bool got_null = false;
    for (int64_t i = 0; i < size && OB_SUCC(ret);) {
      const int64_t bit_vec_off = i / step_size;
      const uint16_t skip_v = skip.reinterpret_data<uint16_t>()[bit_vec_off];
      uint16_t &eval_v = eval_flags.reinterpret_data<uint16_t>()[bit_vec_off];
      if (i + step_size < size && (0 == (skip_v | eval_v))) {
        for (int64_t j = 0; OB_SUCC(ret) && j < step_size; i++, j++) {
          batch_info_guard.set_batch_idx(i);
          ret = ArithOp::datum_op(*res.at(i), *left.at(i), *right.at(i), args...);
          got_null |= res.at(i)->is_null();
        }
        if (OB_SUCC(ret)) {
          eval_v = 0xFFFF;
        }
      } else if (i + step_size < size && (0xFFFF == (skip_v | eval_v))) {
        i += step_size;
      } else {
        const int64_t new_size = std::min(size, i + step_size);
        for (; i < new_size && OB_SUCC(ret); i++) {
          if (!(skip.at(i) || eval_flags.at(i))) {
            batch_info_guard.set_batch_idx(i);
            ret = ArithOp::datum_op(*res.at(i), *left.at(i), *right.at(i), args...);
            eval_flags.bit_or_assign(i, OB_SUCCESS == ret);
            got_null |= res.at(i)->is_null();
          }
        }
      }
    }
    if (OB_SUCC(ret) && got_null) {
      expr.get_eval_info(ctx).notnull_ = false;
    }
    return ret;
  }
};


template <typename ArithOp, typename... Args>
inline int call_functor_with_arg_iter(const ObExpr &expr,
                                     ObEvalCtx &ctx,
                                     const ObBitVector &skip,
                                     const int64_t size,
                                     Args &...args)
{
  int ret = OB_SUCCESS;
  const ObExpr &left = *expr.args_[0];
  const ObExpr &right = *expr.args_[1];
  if (!left.is_batch_result() && !right.is_batch_result()) {
    ret = common::OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "one argument must be batch result in arith batch evaluate",
            K(ret), K(expr), K(left), K(right));
  } else {
    DatumVector res = expr.locate_expr_datumvector(ctx);
    // A binary expression with at least one batch operand always produces a
    // batch.  Keep the old driver's unconditional result indexing explicit.
    res.set_batch(true);
    const DatumVector left_datums = left.locate_expr_datumvector(ctx);
    const DatumVector right_datums = right.locate_expr_datumvector(ctx);
    ret = ObDoArithBatchEval<ArithOp>()(
        expr, ctx, skip, size, res, left_datums, right_datums, args...);
  }
  return ret;
};


// define arith evaluate batch function.
// see example in ObExprAdd
template <typename ArithOp, typename... Args>
int def_batch_arith_op(const ObExpr &expr,
                       ObEvalCtx &ctx,
                       const ObBitVector &skip,
                       const int64_t size,
                       Args &...args)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(binary_operand_batch_eval(expr, ctx, skip, size, false))) {
  } else {
    ret = call_functor_with_arg_iter<ArithOp>(expr, ctx, skip, size, args...);
  }
  return ret;
}

template <typename Res, typename Left, typename Righ>
struct ObArithOpRawType
{
  typedef Res RES_RAW_TYPE;
  typedef Left L_RAW_TYPE;
  typedef Righ R_RAW_TYPE;
};

struct ObArithOpBase : public ObArithOpRawType<char, char, char>
{
  constexpr static bool is_raw_op_supported() { return false; }

  template <typename... Args>
  static void raw_op(RES_RAW_TYPE &, const L_RAW_TYPE &, const R_RAW_TYPE, Args &...args) {}
  static int raw_check(const RES_RAW_TYPE &, const L_RAW_TYPE &, const R_RAW_TYPE &)
  {
    return common::OB_ERR_UNEXPECTED;
  }
};

template <typename Res, typename Left, typename Right>
struct ObArithTypedBase : public ObArithOpRawType<Res, Left, Right>
{
  constexpr static bool is_raw_op_supported()
  {
    return false;
  }

  template <typename... Args>
  static void raw_op(Res &, const Left &, const Right, Args &...args)
  {}
  static int raw_check(const Res &, const Left &, const Right &)
  {
    return common::OB_ERR_UNEXPECTED;
  }
};

// Wrap arith operate with null check.
template <typename DatumFunctor>
struct ObWrapArithOpNullCheck: public ObArithOpBase
{
  template <typename... Args>
  static int datum_op(Datum &res, const Datum &l, const Datum &r, Args &...args)
  {
    int ret = OB_SUCCESS;
    if (l.is_null() || r.is_null()) {
      res.set_null();
    } else {
      ret = DatumFunctor()(res, l, r, args...);
    }
    return ret;
  }
};

template <typename DatumFunctor, typename... Args>
int def_batch_arith_op_by_datum_func(BATCH_EVAL_FUNC_ARG_DECL, Args &...args)
{
  return def_batch_arith_op<ObWrapArithOpNullCheck<DatumFunctor>, Args...>(
      BATCH_EVAL_FUNC_ARG_LIST, args...);
}

// Wrap arith datum operate from raw operate.
// Signed BIGINT arithmetic uses the carrier's scalar accessors. The prepared
// expression type selects signed i64; neither a payload pointer nor its stride
// belongs to this operation. The same functor can be instantiated for the
// legacy pointer carrier and an inline carrier without changing Expr's ABI.
template <typename Base>
struct ObSignedIntArithOpWrap : public Base
{
  static_assert(std::is_same<typename Base::RES_RAW_TYPE, int64_t>::value
      && std::is_same<typename Base::L_RAW_TYPE, int64_t>::value
      && std::is_same<typename Base::R_RAW_TYPE, int64_t>::value,
      "Signed BIGINT arithmetic requires signed i64 operands and result");
  constexpr static bool is_raw_op_supported() { return false; }

  template <typename DatumType, typename... Args>
  int operator()(DatumType &res, const DatumType &l, const DatumType &r, Args &...args) const
  {
    const int64_t left = l.get_int();
    const int64_t right = r.get_int();
    int64_t value = 0;
    Base::raw_op(value, left, right, args...);
    // Keep the old write-before-check contract; the enclosing evaluator owns
    // error publication and the arithmetic policy owns overflow diagnostics.
    res.set_int(value);
    return Base::raw_check(value, left, right);
  }

  template <typename DatumType, typename... Args>
  static int datum_op(DatumType &res, const DatumType &l, const DatumType &r, Args &...args)
  {
    int ret = OB_SUCCESS;
    if (l.is_null() || r.is_null()) {
      res.set_null();
    } else {
      ret = ObSignedIntArithOpWrap()(res, l, r, args...);
    }
    return ret;
  }
};

// The prepared arithmetic policy selects each integral representation. Keep
// unsigned-result arithmetic independent of the carrier's payload address.
template <typename Base>
struct ObUnsignedIntArithOpWrap : public Base
{
  using Left = typename Base::L_RAW_TYPE;
  using Right = typename Base::R_RAW_TYPE;
  static_assert(std::is_same<typename Base::RES_RAW_TYPE, uint64_t>::value
      && (std::is_same<Left, int64_t>::value || std::is_same<Left, uint64_t>::value)
      && (std::is_same<Right, int64_t>::value || std::is_same<Right, uint64_t>::value),
      "Unsigned BIGINT arithmetic requires integral i64/u64 operands");
  constexpr static bool is_raw_op_supported() { return false; }

  template <typename Integer, typename DatumType>
  static Integer read(const DatumType &value)
  {
    if constexpr (std::is_same<Integer, int64_t>::value) {
      return value.get_int();
    } else {
      return value.get_uint();
    }
  }

  template <typename DatumType, typename... Args>
  int operator()(DatumType &res, const DatumType &l, const DatumType &r, Args &...args) const
  {
    const Left left = read<Left>(l);
    const Right right = read<Right>(r);
    uint64_t value = 0;
    Base::raw_op(value, left, right, args...);
    // Preserve the existing write-before-check contract and policy diagnostics.
    res.set_uint(value);
    return Base::raw_check(value, left, right);
  }

  template <typename DatumType, typename... Args>
  static int datum_op(DatumType &res, const DatumType &l, const DatumType &r, Args &...args)
  {
    int ret = common::OB_SUCCESS;
    if (l.is_null() || r.is_null()) {
      res.set_null();
    } else {
      ret = ObUnsignedIntArithOpWrap()(res, l, r, args...);
    }
    return ret;
  }
};

// Floating arithmetic is inline. Decimal arithmetic explicitly presents the
// prepared coefficient to the existing wide-integer library for this call.
template <typename Base>
struct ObArithOpWrap : public Base
{
  constexpr static bool is_raw_op_supported() { return false; }
  template <typename Floating>
  static Floating read_floating(const Datum &value)
  {
    if constexpr (std::is_same<Floating, float>::value) { return value.get_float(); }
    else { return value.get_double(); }
  }
  template <typename... Args>
  int operator()(Datum &res, const Datum &l, const Datum &r,
                 const ObExpr &expr, ObEvalCtx &ctx, Args &...args) const
  {
    using Result = typename Base::RES_RAW_TYPE;
    using Left = typename Base::L_RAW_TYPE;
    using Right = typename Base::R_RAW_TYPE;
    if constexpr (std::is_floating_point<Result>::value) {
      const Left left = read_floating<Left>(l);
      const Right right = read_floating<Right>(r);
      Result value = 0;
      Base::raw_op(value, left, right, args...);
      if constexpr (std::is_same<Result, float>::value) { res.set_float(value); }
      else { res.set_double(value); }
      return Base::raw_check(value, left, right);
    } else {
      int ret = OB_SUCCESS;
      Left left;
      Right right;
      if (OB_FAIL(ObExprUtil::get_decimal_int(l, left))) {
      } else if (OB_FAIL(ObExprUtil::get_decimal_int(r, right))) {
      } else {
        Result value;
        Base::raw_op(value, left, right, args...);
        if (OB_FAIL(ObExprUtil::set_decimal_int(expr, ctx, res,
            reinterpret_cast<const common::ObDecimalInt *>(&value), sizeof(value)))) {
        } else {
          ret = Base::raw_check(value, left, right);
        }
      }
      return ret;
    }
  }

  template <typename... Args>
  static int datum_op(Datum &res, const Datum &l, const Datum &r,
                      const ObExpr &expr, ObEvalCtx &ctx, Args &...args)
  {
    int ret = OB_SUCCESS;
    if (l.is_null() || r.is_null()) {
      res.set_null();
    } else {
      ret = ObArithOpWrap()(res, l, r, expr, ctx, args...);
    }
    return ret;
  }
};

template <typename Base>
struct ObNestedArithOpWrap : public Base
{
  int operator()(Datum &res, const Datum &l, const Datum &r, const ObExpr &expr, ObEvalCtx &ctx) const
  {
    int ret = OB_SUCCESS;
    ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
    common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
    ObString left = l.get_string();
    ObString right = r.get_string();
    const uint16_t left_meta_id = expr.args_[0]->obj_meta_.get_subschema_id();
    const uint16_t right_meta_id = expr.args_[1]->obj_meta_.get_subschema_id();
    const uint16_t res_meta_id = expr.obj_meta_.get_subschema_id();
    ObIArrayType *left_obj = NULL;
    ObIArrayType *right_obj = NULL;
    ObIArrayType *res_obj = NULL;
    ObString res_str;
    if (OB_FAIL(Base::construct_params(tmp_allocator, ctx, left_meta_id, right_meta_id, res_meta_id,
                                          left, right, left_obj, right_obj, res_obj))) {
    } else if (OB_FAIL(Base()(*res_obj, *left_obj, *right_obj))) {
    } else if (OB_FAIL(Base::get_res(ctx, res_obj, expr, res_str))) {
    } else {
      res.set_string(res_str);
    }

    return ret;
  }
};

struct ObNestedArithOpBaseFunc
{
  static int construct_param(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t meta_id,
                             ObString &str_data, ObIArrayType *&param_obj);
  static int construct_params(ObIAllocator &alloc, ObEvalCtx &ctx, const uint16_t left_meta_id,
                              const uint16_t right_meta_id, const uint16_t res_meta_id, ObString &left, ObString right,
                              ObIArrayType *&left_obj, ObIArrayType *&right_obj, ObIArrayType *&res_obj);
  static int get_res(ObEvalCtx &ctx, ObIArrayType *res_obj, const ObExpr &expr, ObString &res_str);
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_EXPR_OB_BATCH_EVAL_UTIL_H_
