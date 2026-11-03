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

#define USING_LOG_PREFIX SQL_RESV
#include <cinttypes>
#include "ob_raw_expr_resolver_impl.h"
#include "sql/engine/expr/cast_type_descriptor.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/expr/numeric_literal.h"
#include "sql/resolver/expr/literal_decode_options.h"
#include "sql/plan_cache/literal_token.h"
#include "sql/resolver/parameterization_result.h"
#include "lib/json/ob_json_print_utils.h"
#include "lib/charset/ob_ctype.h"
#include "sql/pl/ob_pl_resolver.h"
#include "sql/resolver/dml/ob_inlist_resolver.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

namespace {
int resolve_weight_integer(const peg::Node &node, uint64_t &value)
{
  int ret = OB_SUCCESS;
  if (node.kind() != peg::NodeKind::Int && node.kind() != peg::NodeKind::Uint) {
    ret = OB_INVALID_ARGUMENT;
    _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "WEIGHT_STRING requires integer length and level options");
  } else {
    value = peg::integer_literal_value(node);
  }
  return ret;
}

int check_scalar_quantifier(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  if (peg::aggregate_quantifier(node) != peg::AggregateQuantifier::Default) {
    const auto name = peg::call_name(node);
    ret = OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT;
    LOG_USER_ERROR(OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT, int(name.size()), name.data());
  }
  return ret;
}

ObItemType binary_expression_type(peg::BinaryOperator operation)
{
  switch (operation) {
    case peg::BinaryOperator::Add: return T_OP_ADD;
    case peg::BinaryOperator::Subtract: return T_OP_MINUS;
    case peg::BinaryOperator::Multiply: return T_OP_MUL;
    case peg::BinaryOperator::Divide: return T_OP_DIV;
    case peg::BinaryOperator::IntegerDivide: return T_OP_INT_DIV;
    case peg::BinaryOperator::Modulo: return T_OP_MOD;
    case peg::BinaryOperator::BitOr: return T_OP_BIT_OR;
    case peg::BinaryOperator::BitAnd: return T_OP_BIT_AND;
    case peg::BinaryOperator::BitXor: return T_OP_BIT_XOR;
    case peg::BinaryOperator::ShiftLeft: return T_OP_BIT_LEFT_SHIFT;
    case peg::BinaryOperator::ShiftRight: return T_OP_BIT_RIGHT_SHIFT;
    case peg::BinaryOperator::Concat: return T_OP_CNN;
    case peg::BinaryOperator::Equal: return T_OP_EQ;
    case peg::BinaryOperator::NullSafeEqual: return T_OP_NSEQ;
    case peg::BinaryOperator::NotEqual: return T_OP_NE;
    case peg::BinaryOperator::Less: return T_OP_LT;
    case peg::BinaryOperator::LessEqual: return T_OP_LE;
    case peg::BinaryOperator::Greater: return T_OP_GT;
    case peg::BinaryOperator::GreaterEqual: return T_OP_GE;
    case peg::BinaryOperator::LogicalAnd: return T_OP_AND;
    case peg::BinaryOperator::LogicalOr: return T_OP_OR;
    case peg::BinaryOperator::LogicalXor: return T_OP_XOR;
    case peg::BinaryOperator::Like: return T_OP_LIKE;
    case peg::BinaryOperator::NotLike: return T_OP_NOT_LIKE;
    case peg::BinaryOperator::Regexp: return T_OP_REGEXP;
    case peg::BinaryOperator::NotRegexp: return T_OP_NOT_REGEXP;
    default: return T_INVALID;
  }
}

ObItemType callable_expression_type(peg::NodeKind kind)
{
  switch (kind) {
    case peg::NodeKind::Count: return T_FUN_COUNT;
    case peg::NodeKind::Sum: return T_FUN_SUM;
    case peg::NodeKind::Min: return T_FUN_MIN;
    case peg::NodeKind::Max: return T_FUN_MAX;
    case peg::NodeKind::Avg: return T_FUN_AVG;
    case peg::NodeKind::PercentileCont: return T_FUN_GROUP_PERCENTILE_CONT;
    case peg::NodeKind::Grouping: return T_FUN_GROUPING;
    case peg::NodeKind::JsonArrayAgg: return T_FUN_JSON_ARRAYAGG;
    case peg::NodeKind::JsonObjectAgg: return T_FUN_JSON_OBJECTAGG;
    case peg::NodeKind::Stddev: return T_FUN_STDDEV;
    case peg::NodeKind::StddevPop: return T_FUN_STDDEV_POP;
    case peg::NodeKind::StddevSamp: return T_FUN_STDDEV_SAMP;
    case peg::NodeKind::Variance: return T_FUN_VARIANCE;
    case peg::NodeKind::VarPop: return T_FUN_VAR_POP;
    case peg::NodeKind::VarSamp: return T_FUN_VAR_SAMP;
    case peg::NodeKind::BitAnd: return T_FUN_SYS_BIT_AND;
    case peg::NodeKind::BitOr: return T_FUN_SYS_BIT_OR;
    case peg::NodeKind::BitXor: return T_FUN_SYS_BIT_XOR;
    case peg::NodeKind::ApproxCountDistinct: return T_FUN_APPROX_COUNT_DISTINCT;
    case peg::NodeKind::ApproxCountDistinctSynopsis: return T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS;
    case peg::NodeKind::ApproxCountDistinctSynopsisMerge: return T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE;
    case peg::NodeKind::SumOpnsize: return T_FUN_SUM_OPNSIZE;
    case peg::NodeKind::Mvt: return T_FUN_SYS_ST_ASMVT;
    case peg::NodeKind::TopFreHist: return T_FUN_TOP_FRE_HIST;
    case peg::NodeKind::HybridHist: return T_FUN_HYBRID_HIST;
    case peg::NodeKind::GroupConcat: return T_FUN_GROUP_CONCAT;
    case peg::NodeKind::ArrayAgg: return T_FUNC_SYS_ARRAY_AGG;
    case peg::NodeKind::RowNumber: return T_WIN_FUN_ROW_NUMBER;
    case peg::NodeKind::Rank: return T_WIN_FUN_RANK;
    case peg::NodeKind::DenseRank: return T_WIN_FUN_DENSE_RANK;
    case peg::NodeKind::PercentRank: return T_WIN_FUN_PERCENT_RANK;
    case peg::NodeKind::CumeDist: return T_WIN_FUN_CUME_DIST;
    case peg::NodeKind::Ntile: return T_WIN_FUN_NTILE;
    case peg::NodeKind::FirstValue: return T_WIN_FUN_FIRST_VALUE;
    case peg::NodeKind::LastValue: return T_WIN_FUN_LAST_VALUE;
    case peg::NodeKind::NthValue: return T_WIN_FUN_NTH_VALUE;
    case peg::NodeKind::Lead: return T_WIN_FUN_LEAD;
    case peg::NodeKind::Lag: return T_WIN_FUN_LAG;
    case peg::NodeKind::Point: return T_FUN_SYS_POINT;
    case peg::NodeKind::LineString: return T_FUN_SYS_LINESTRING;
    case peg::NodeKind::Polygon: return T_FUN_SYS_POLYGON;
    case peg::NodeKind::MultiPoint: return T_FUN_SYS_MULTIPOINT;
    case peg::NodeKind::MultiLineString: return T_FUN_SYS_MULTILINESTRING;
    case peg::NodeKind::MultiPolygon: return T_FUN_SYS_MULTIPOLYGON;
    case peg::NodeKind::GeometryCollection: return T_FUN_SYS_GEOMCOLLECTION;
    case peg::NodeKind::ArrayLiteral: return T_FUN_SYS_ARRAY;
    case peg::NodeKind::ArrayMembership: return T_FUNC_SYS_ARRAY_CONTAINS;
    case peg::NodeKind::MapLiteral: return T_FUN_SYS_MAP;
    case peg::NodeKind::ArrayMap: return T_FUNC_SYS_ARRAY_MAP;
    case peg::NodeKind::ArrayFilter: return T_FUNC_SYS_ARRAY_FILTER;
    case peg::NodeKind::ArrayFirst: return T_FUNC_SYS_ARRAY_FIRST;
    case peg::NodeKind::ArraySortBy: return T_FUNC_SYS_ARRAY_SORTBY;
    case peg::NodeKind::VectorDistance: return T_FUN_SYS_VECTOR_DISTANCE;
    case peg::NodeKind::VectorSimilarity: return T_FUN_SYS_VECTOR_SIMILARITY;
    case peg::NodeKind::CurrentTimestamp: return T_FUN_SYS_CUR_TIMESTAMP;
    case peg::NodeKind::CurrentTime: return T_FUN_SYS_CUR_TIME;
    case peg::NodeKind::CurrentDate: return T_FUN_SYS_CUR_DATE;
    case peg::NodeKind::UtcTimestamp: return T_FUN_SYS_UTC_TIMESTAMP;
    case peg::NodeKind::UtcTime: return T_FUN_SYS_UTC_TIME;
    case peg::NodeKind::UtcDate: return T_FUN_SYS_UTC_DATE;
    case peg::NodeKind::Sysdate: return T_FUN_SYS_SYSDATE;
    case peg::NodeKind::CurrentUser: return T_FUN_SYS_CURRENT_USER;
    case peg::NodeKind::If: return T_FUN_SYS_IF;
    case peg::NodeKind::IsNull: return T_FUN_SYS_ISNULL;
    case peg::NodeKind::PartitionKeyFunction: return T_FUN_SYS_PART_KEY;
    case peg::NodeKind::PartitionHashFunction: return T_FUN_SYS_PART_HASH;
    case peg::NodeKind::SetCollation: return T_FUN_SYS_SET_COLLATION;
    default: return T_FUN_SYS;
  }
}

ObItemType literal_expression_type(peg::LiteralKind kind)
{
  switch (kind) {
    case peg::LiteralKind::Integer: return T_INT;
    case peg::LiteralKind::Unsigned: return T_UINT64;
    case peg::LiteralKind::Decimal: return T_NUMBER;
    case peg::LiteralKind::Float: return T_DOUBLE;
    case peg::LiteralKind::String:
    case peg::LiteralKind::NationalString: return T_VARCHAR;
    case peg::LiteralKind::Binary: return T_HEX_STRING;
    case peg::LiteralKind::Boolean: return T_BOOL;
    case peg::LiteralKind::Null: return T_NULL;
    case peg::LiteralKind::Date: return T_DATE;
    case peg::LiteralKind::Time: return T_TIME;
    case peg::LiteralKind::Timestamp: return T_TIMESTAMP;
    case peg::LiteralKind::Placeholder: return T_QUESTIONMARK;
    default: return T_INVALID;
  }
}



int infer_json_returning_type(ObIAllocator &allocator, ObString &path,
                             const ObRawExprResType &declared, bool query,
                             ObRawExprResType &target, bool &inferred)
{
  int ret = OB_SUCCESS;
  inferred = false;
  bool has_method = false;
  char *method = nullptr;
  uint64_t length = 0;
  if (OB_FAIL(ObJsonPath::get_path_item_method_str(allocator, path, method, length, has_method))) {
  } else if (has_method) {
    ObJsonPathFuncNode function(&allocator);
    if (OB_FAIL(function.init(method, length))) {
      ret = OB_SUCCESS;
    } else {
      const bool json_query = query && declared.get_type() == ObJsonType;
      const auto string_target = [&](int32_t size) {
        target.set_type(ObVarcharType);
        target.set_collation_type(CS_TYPE_INVALID);
        target.set_length(size);
        inferred = true;
      };
      const auto numeric_target = [&](ObObjType type, ObPrecision precision, ObScale scale) {
        target.set_type(type);
        target.set_collation_type(CS_TYPE_INVALID);
        target.set_precision(precision);
        target.set_scale(scale);
        inferred = true;
      };
      switch (function.get_node_type()) {
        case JPN_BOOLEAN:
        case JPN_BOOL_ONLY:
          if (!json_query) { string_target(20); }
          break;
        case JPN_DATE:
          if (!query) { numeric_target(ObDateTimeType, 0, 0); }
          break;
        case JPN_DOUBLE:
          if (!query) { numeric_target(ObDoubleType, -1, -85); }
          break;
        case JPN_LENGTH:
        case JPN_SIZE:
        case JPN_NUM_ONLY:
        case JPN_NUMBER:
        case JPN_FLOOR:
        case JPN_CEILING:
          if (declared.get_type() == ObNullType || json_query) { string_target(4000); }
          else { numeric_target(ObNumberType, -1, -85); }
          break;
        case JPN_TIMESTAMP:
          if (!query) { numeric_target(ObTimestampNanoType, 0, 6); }
          break;
        case JPN_TYPE:
        case JPN_STR_ONLY:
        case JPN_STRING:
        case JPN_UPPER:
        case JPN_LOWER:
          if (!json_query) { string_target(75); }
          break;
        default:
          break;
      }
    }
  }
  return ret;
}
}  // namespace
ObRawExprResolverImpl::ObRawExprResolverImpl(ObExprResolveContext &ctx)
    : ctx_(ctx),
      is_contains_assignment_(false),
      lambda_parameters_(nullptr)
{
}

int ObRawExprResolverImpl::resolve(const peg::Node &node,
                                   ObRawExpr *&expr,
                                   ObIArray<ObQualifiedName> &columns,
                                   ObIArray<ObVarInfo> &sys_vars,
                                   ObIArray<ObSubQueryInfo> &sub_query_info,
                                   ObIArray<ObAggFunRawExpr*> &aggr_exprs,
                                   ObIArray<ObWinFunRawExpr*> &win_exprs,
                                   ObIArray<ObUDFInfo> &udf_info,
                                   ObIArray<ObOpRawExpr*> &op_exprs,
                                   ObIArray<ObUserVarIdentRawExpr*> &user_var_exprs,
                                   ObIArray<ObInListInfo> &inlist_infos,
                                   ObIArray<ObMatchFunRawExpr*> &match_exprs)
{
  ctx_.columns_ = &columns;
  ctx_.op_exprs_ = &op_exprs;
  ctx_.sys_vars_ = &sys_vars;
  ctx_.sub_query_info_ = &sub_query_info;
  ctx_.aggr_exprs_ = &aggr_exprs;
  ctx_.win_exprs_ = &win_exprs;
  ctx_.udf_info_ = &udf_info;
  ctx_.user_var_exprs_ = &user_var_exprs;
  ctx_.inlist_infos_ = &inlist_infos;
  ctx_.match_exprs_ = &match_exprs;
  int ret = recursive_resolve(node, expr, true);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(expr->extract_info())) {
    }
  }
  return ret;
}

int ObRawExprResolverImpl::get_opposite_string(const common::ObString &orig_string, common::ObString &new_string, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  char *tmp_buf = NULL;
  if (OB_UNLIKELY(orig_string.empty())) {
    ret = OB_INVALID_ARGUMENT;
    SQL_LOG(WARN, "invalid argument", K(ret));
  } else {
    if ('-' == orig_string[0]) {
      new_string.assign_ptr(orig_string.ptr() + 1, orig_string.length() - 1);
    } else {
      if (OB_ISNULL(tmp_buf = (char*)(allocator.alloc(orig_string.length() + 1)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SQL_LOG(WARN, "failed to allocate memory", "size", orig_string.length() + 1, K(ret));
      } else {
        tmp_buf[0] = '-';
        MEMCPY(tmp_buf + 1, orig_string.ptr(), orig_string.length());
        new_string.assign_ptr(tmp_buf, orig_string.length() + 1);
      }
    }
  }
  return ret;
}

int ObRawExprResolverImpl::try_negate_const(ObRawExpr *&expr,
                                            const int64_t neg_cnt,
                                            int64_t &remain_neg_cnt)
{
  int ret = OB_SUCCESS;
  remain_neg_cnt = neg_cnt;
  bool neg_const = false;
  const bool is_odd = neg_cnt % 2 == 1;
  if (neg_cnt > 0 && NULL != expr && expr->is_const_raw_expr()) {
    ObConstRawExpr *const_expr = static_cast<ObConstRawExpr *>(expr);
    const ObString &orig_literal_prefix = const_expr->get_literal_prefix();
    neg_const = true;
    switch (const_expr->get_expr_type()) {
      case T_INT: {
        int64_t val = OB_INVALID_ID;
        if (OB_SUCC(const_expr->get_value().get_int(val))) {
          if (INT64_MIN == val) {
            // -val will overflow if val is INT64_MIN, we must use neg expr to handle this.
            neg_const = false;
          } else if (is_odd) {
            ObObj new_val;
            new_val.set_int(-val);
            const_expr->set_expr_type(T_INT);
            const_expr->set_value(new_val);
            const_expr->set_precision(static_cast<ObPrecision>(
                                      const_expr->get_accuracy().get_precision() + 1));
          }
        }
        break;
      }
      case T_FLOAT: {
        float val = static_cast<float>(OB_INVALID_ID);
        if (is_odd && OB_SUCC(const_expr->get_value().get_float(val))) {
          if (!isnan(val)) { // OB only store nan, don't add negative nan
            ObObj new_val;
            new_val.set_float(-val);
            new_val.set_scale(const_expr->get_accuracy().get_scale());
            const_expr->set_expr_type(T_FLOAT);
            const_expr->set_value(new_val);
            const_expr->set_precision(static_cast<ObPrecision>(const_expr->get_accuracy().get_precision() + 1));
          }
        }
        break;
      }
      case T_DOUBLE: {
        double val = static_cast<double>(OB_INVALID_ID);
        if (is_odd && OB_SUCC(const_expr->get_value().get_double(val))) {
          if (!isnan(val)) { // OB only store nan, don't add negative nan
            ObObj new_val;
            new_val.set_double(-val);
            new_val.set_scale(const_expr->get_accuracy().get_scale());
            const_expr->set_expr_type(T_DOUBLE);
            const_expr->set_value(new_val);
            const_expr->set_precision(static_cast<ObPrecision>(const_expr->get_accuracy().get_precision() + 1));
          }
        }
        break;
      }
      case T_NUMBER: {
        bool negated = false;
        ObObj new_val;
        if (const_expr->get_value().is_decimal_int()) {
          ObDecimalInt *neg_dec = nullptr;
          int32_t out_prec =
            const_expr->get_accuracy().get_precision() + 1;
          int32_t out_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
          if (!is_odd) {// do nothing
          } else if (OB_FAIL(wide::negate(const_expr->get_value().get_decimal_int(),
                                          const_expr->get_value().get_int_bytes(), neg_dec,
                                          out_bytes, ctx_.expr_factory_.get_allocator()))) {
          } else {
            new_val.set_decimal_int(out_bytes, const_expr->get_value().get_scale(), neg_dec);
            negated = true;
          }
        } else {
          number::ObNumber val;
          number::ObNumber negative_val;
          if (is_odd && OB_SUCC(const_expr->get_value().get_number(val))
              && OB_SUCC(val.negate(negative_val, ctx_.expr_factory_.get_allocator()))) {
            new_val.set_number(negative_val);
            new_val.set_scale(const_expr->get_accuracy().get_scale());
            negated = true;
          }
        }
        if (OB_SUCC(ret) && negated) {
          const_expr->set_expr_type(T_NUMBER);
          const_expr->set_value(new_val);
          const_expr->set_precision(
            static_cast<ObPrecision>(const_expr->get_accuracy().get_precision() + 1));
        }
        break;
      }
      default: {
        neg_const = false;
        break;
      }
    }

    if (OB_SUCC(ret)) {
      if (!orig_literal_prefix.empty() && neg_const && is_odd) {
        ObString new_literal_prefix;
        if (OB_FAIL(get_opposite_string(orig_literal_prefix, new_literal_prefix, ctx_.expr_factory_.get_allocator()))) {
        } else {
          const_expr->set_literal_prefix(new_literal_prefix);
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    // ObConstRawExpr::type_ and ObConstRawExpr::value_ type may mismatch, value_ may be number
    // wile type_ is T_INT. This will cause get value error, ignore it.
    ret = OB_SUCCESS;
    neg_const = false;
  }
  if (0 == neg_cnt || neg_const) {
    remain_neg_cnt = 0;
  } else {
    // Reduce negative count to 2 for even number, 3 for odd number. Why not 1 or 2:
    //
    //   c1: bigint unsigned, values is 9223372036854775808
    //   -c1: -9223372036854775808
    //   --c1: report out of range error
    //   ---c1: report out of range error
    //
    remain_neg_cnt = is_odd ? (neg_cnt > 2 ? 3 : 1) : 2;
  }
  return ret;
}

int ObRawExprResolverImpl::recursive_resolve(const peg::Node &node, ObRawExpr *&expr, bool is_root_expr)
{
  return SMART_CALL(do_recursive_resolve(node, expr, is_root_expr));
}

int ObRawExprResolverImpl::do_recursive_resolve(const peg::Node &node,
                                                ObRawExpr *&expr, bool is_root_expr)
{
  int ret = OB_SUCCESS;
  bool overflow = false;
  expr = nullptr;
  OZ (check_stack_overflow(overflow));
  if (OB_SUCC(ret) && overflow) { ret = OB_SIZE_OVERFLOW; }
  const ParameterBinding *binding = ctx_.parameterization_ != nullptr
      ? ctx_.parameterization_->find(&node) : nullptr;
  const NumericLowering *lowering = ctx_.parameterization_ != nullptr
      ? ctx_.parameterization_->numeric_lowering(&node) : nullptr;
  if (OB_FAIL(ret)) {
  } else if (node.kind() == peg::NodeKind::CharsetName) {
    OZ(process_char_charset_node(node, expr));
  } else if (binding != nullptr || peg::is_literal(node)
      || (lowering != nullptr && lowering->operation != peg::ParameterLoweringKind::AddNegatedOperand)) {
    OZ (process_datatype_or_questionmark(node, expr));
  } else {
    switch (node.kind()) {
      case peg::NodeKind::AliasedArgument:
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "argument aliases on built-in functions");
        break;
      case peg::NodeKind::OdbcEscape:
        OZ (process_odbc_escape(node, expr));
        break;
      case peg::NodeKind::Parenthesized:
        OZ (recursive_resolve(peg::parenthesized_expression(node), expr, is_root_expr));
        break;
      case peg::NodeKind::Default: {
        ObConstRawExpr *value = nullptr;
        OZ (ctx_.expr_factory_.create_raw_expr(T_DEFAULT, value));
        if (OB_SUCC(ret)) { ObObj null; null.set_null(); value->set_value(null); expr = value; }
        break;
      }
      case peg::NodeKind::Column:
      case peg::NodeKind::Ident:
      case peg::NodeKind::Star:
        OZ (process_column_ref_node(node, expr));
        break;
      case peg::NodeKind::SystemVariable:
        OZ (process_system_variable_node(node, expr));
        break;
      case peg::NodeKind::GetUserVariable:
        OZ (process_user_var_node(node, expr));
        break;
      case peg::NodeKind::UserAssignment:
        OZ (process_user_assignment(node, expr));
        break;
      case peg::NodeKind::Unary:
        if (peg::unary_operator(node) == peg::UnaryOperator::LogicalNot) {
          OZ (process_not_node(node, expr));
        } else if (peg::unary_operator(node) == peg::UnaryOperator::BitwiseNot) {
          ObRawExpr *operand = nullptr;
          ObOpRawExpr *operation = nullptr;
          OZ (recursive_resolve(peg::unary_operand(node), operand));
          OZ (ctx_.expr_factory_.create_raw_expr(T_OP_BIT_NEG, operation));
          OZ (operation->set_param_expr(operand));
          OX (expr = operation);
        } else { OZ (process_pos_or_neg_node(node, expr)); }
        break;
      case peg::NodeKind::Binary: {
        const ObItemType type = binary_expression_type(peg::binary_operator(node));
        if (type == T_OP_AND || type == T_OP_OR) { OZ (process_logical_node(node, is_root_expr, expr)); }
        else if (type == T_OP_LIKE || type == T_OP_NOT_LIKE) { OZ (process_like_node(node, expr)); }
        else if (type == T_OP_REGEXP || type == T_OP_NOT_REGEXP) { OZ (process_regexp_or_not_regexp_node(node, expr)); }
        else { OZ (process_operator_node(node, expr)); }
        break;
      }
      case peg::NodeKind::Is: OZ (process_is_or_is_not_node(node, expr)); break;
      case peg::NodeKind::Between: OZ (process_between_node(node, expr)); break;
      case peg::NodeKind::In: OZ (process_in_or_not_in_node(node, is_root_expr, expr)); break;
      case peg::NodeKind::Case: OZ (process_case_node(node, expr)); break;
      case peg::NodeKind::ExprList:
      case peg::NodeKind::ValueVector: OZ (process_row(node, expr)); break;
      case peg::NodeKind::Exists:
      case peg::NodeKind::Any:
      case peg::NodeKind::All: OZ (process_any_or_all_node(node, expr)); break;
      case peg::NodeKind::Select:
      case peg::NodeKind::PlSubquery: OZ (process_sub_query_node(node, expr)); break;
      case peg::NodeKind::Count:
      case peg::NodeKind::Sum:
      case peg::NodeKind::Min:
      case peg::NodeKind::Max:
      case peg::NodeKind::Avg:
      case peg::NodeKind::PercentileCont:
      case peg::NodeKind::Grouping:
      case peg::NodeKind::JsonArrayAgg:
      case peg::NodeKind::JsonObjectAgg:
      case peg::NodeKind::Stddev:
      case peg::NodeKind::StddevPop:
      case peg::NodeKind::StddevSamp:
      case peg::NodeKind::Variance:
      case peg::NodeKind::VarPop:
      case peg::NodeKind::VarSamp:
      case peg::NodeKind::BitAnd:
      case peg::NodeKind::BitOr:
      case peg::NodeKind::BitXor:
      case peg::NodeKind::ApproxCountDistinct:
      case peg::NodeKind::ApproxCountDistinctSynopsis:
      case peg::NodeKind::ApproxCountDistinctSynopsisMerge:
      case peg::NodeKind::SumOpnsize:
      case peg::NodeKind::Mvt:
      case peg::NodeKind::TopFreHist:
      case peg::NodeKind::HybridHist:
      case peg::NodeKind::GroupConcat:
      case peg::NodeKind::ArrayAgg:
        OZ (process_agg_node(node, expr));
        break;
      case peg::NodeKind::NamedWindow:
      case peg::NodeKind::WindowFunction: {
        const int64_t orig_win_func_cnt = ctx_.win_exprs_->count();
        if (OB_FAIL(process_window_function_node(node, expr))) {
        }
        for (int64_t i = orig_win_func_cnt; OB_SUCC(ret) && i < ctx_.win_exprs_->count(); ++i) {
          if (OB_FAIL(check_and_canonicalize_window_expr(ctx_.win_exprs_->at(i)))) {
          }
        }
        if (OB_SUCC(ret)) {
          // remove aggr generated by window_function
          ObIArray<ObAggFunRawExpr *> &aggr_exprs = *ctx_.aggr_exprs_;
          ObSEArray<ObAggFunRawExpr *, 2> clone_aggr_exprs;
          ret = clone_aggr_exprs.assign(aggr_exprs);
          aggr_exprs.reset();
          for (int64_t i = 0; OB_SUCC(ret) && i < clone_aggr_exprs.count(); ++i) {
            bool exist = false;
            for (int64_t j = orig_win_func_cnt; j < ctx_.win_exprs_->count(); ++j) {
              if (clone_aggr_exprs.at(i) == ctx_.win_exprs_->at(j)->get_agg_expr()) {
                exist = true;
                break;
              }
            }
            if (!exist) {
              ret = aggr_exprs.push_back(clone_aggr_exprs.at(i));
            }
          }
        }
        break;
      }
      case peg::NodeKind::Point:
      case peg::NodeKind::LineString:
      case peg::NodeKind::Polygon:
      case peg::NodeKind::MultiPoint:
      case peg::NodeKind::MultiLineString:
      case peg::NodeKind::MultiPolygon:
      case peg::NodeKind::GeometryCollection:
      case peg::NodeKind::ArrayLiteral:
      case peg::NodeKind::MapLiteral:
        OZ (process_geo_func_node(node, expr));
        break;
      case peg::NodeKind::ArrayMap:
      case peg::NodeKind::ArrayFilter:
      case peg::NodeKind::ArrayFirst:
      case peg::NodeKind::ArraySortBy: OZ (process_array_map_func_node(node, expr)); break;
      case peg::NodeKind::Lambda: OZ (process_lambda_func_node(node, expr)); break;
      case peg::NodeKind::VectorDistance:
      case peg::NodeKind::VectorSimilarity: OZ (process_vector_func_node(node, expr)); break;
      case peg::NodeKind::CurrentTimestamp:
      case peg::NodeKind::CurrentTime:
      case peg::NodeKind::CurrentDate:
      case peg::NodeKind::UtcTimestamp:
      case peg::NodeKind::UtcTime:
      case peg::NodeKind::UtcDate:
      case peg::NodeKind::Sysdate:
      case peg::NodeKind::CurrentUser:
        OZ (process_timestamp_node(node, expr));
        break;
      case peg::NodeKind::JsonValue: OZ (process_json_value_node(node, expr)); break;
      case peg::NodeKind::JsonQuery: OZ (process_json_query_node(node, expr)); break;
      case peg::NodeKind::Cast: OZ (process_cast(node, expr)); break;
      case peg::NodeKind::ElasticMatch:
      case peg::NodeKind::MatchAgainst: OZ (process_match_against(node, expr)); break;
      case peg::NodeKind::MatchScore: OZ (process_match_score(expr)); break;
      case peg::NodeKind::CharsetName: OZ (process_char_charset_node(node, expr)); break;
      case peg::NodeKind::Collation: OZ (process_collation_node(node, expr)); break;
      case peg::NodeKind::If: OZ (process_if_node(node, expr)); break;
      case peg::NodeKind::IsNull: OZ (process_isnull_node(node, expr)); break;
      case peg::NodeKind::PartitionKeyFunction:
      case peg::NodeKind::PartitionHashFunction:
      case peg::NodeKind::ArrayMembership:
      case peg::NodeKind::SetCollation:
        OZ (process_internal_sys_function_node(node, expr, callable_expression_type(node.kind())));
        break;
      case peg::NodeKind::PlNamedArgument: OZ(process_call_param_node(node, expr)); break;
      case peg::NodeKind::DefaultValue: OZ(process_fun_sys_node(node, expr, is_root_expr)); break;
      case peg::NodeKind::Function:
      case peg::NodeKind::StringConcat:
        ret = peg::call_qualifiers(node).empty() ? process_fun_sys_node(node, expr, is_root_expr)
                                               : process_unresolved_function(node, expr);
        if (ret == OB_ERR_FUNCTION_UNKNOWN && OB_FAIL(process_unresolved_function(node, expr))) {
          const auto name = peg::call_name(node);
          ret = OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT;
          LOG_USER_ERROR(OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT, int(name.size()), name.data());
        }
        break;
      default: ret = OB_ERR_PARSER_SYNTAX; break;
    }
  }
  if (OB_SUCC(ret) && expr != nullptr) { OZ (expr->fast_check_status()); }
  return ret;
}

ObString ObRawExprResolverImpl::source_text(const peg::Node &node) const
{
  const auto span = node.span();
  return span.end == span.start ? ObString()
      : ObString(span.end - span.start, ctx_.syntax_source_.ptr() + span.start);
}

int ObRawExprResolverImpl::process_row(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  const auto elements = peg::row_elements(node);
  ObOpRawExpr *row = nullptr;
  OZ (ctx_.expr_factory_.create_raw_expr(T_OP_ROW, row));
  OZ (row->init_param_exprs(elements.size()));
  for (const auto &element : elements) {
    ObRawExpr *value = nullptr;
    OZ (recursive_resolve(element, value));
    OZ (row->add_param_expr(value));
    if (OB_FAIL(ret)) { break; }
  }
  OX (expr = row);
  return ret;
}

int ObRawExprResolverImpl::publish_qualified_name(ObQualifiedName &name,
                                                  int64_t child_start, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObColumnRefRawExpr *column = nullptr;
  name.format_qualified_name(ctx_.case_mode_);
  name.parents_expr_info_ = ctx_.parents_expr_info_;
  OZ (ctx_.expr_factory_.create_raw_expr(T_REF_COLUMN, column));
  CK (OB_NOT_NULL(column));
  if (OB_SUCC(ret)) {
    name.ref_expr_ = column;
    if (name.is_pl_var()) {
      for (int64_t i = child_start; i < ctx_.columns_->count(); ++i) {
        ctx_.columns_->at(i).is_access_root_ = false;
      }
    }
    OZ (ctx_.columns_->push_back(name));
    OX (expr = column);
  }
  return ret;
}

int ObRawExprResolverImpl::process_unresolved_function(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObQualifiedName name;
  if (OB_FAIL(check_scalar_quantifier(node))) {
  } else if (OB_ISNULL(ctx_.columns_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t child_start = ctx_.columns_->count();
    OZ (resolve_func_node_of_obj_access_idents(node, name));
    OZ (publish_qualified_name(name, child_start, expr));
  }
  return ret;
}

int ObRawExprResolverImpl::check_pl_variable(ObQualifiedName &q_name, bool &is_pl_var)
{
  int ret = OB_SUCCESS;
  is_pl_var = false;
  common::ObArenaAllocator allocator;
  ObRawExprFactory expr_factory(allocator);
  ObRawExpr *var = NULL;
  if (NULL == ctx_.secondary_namespace_) {
    // do nothing ...
  } else {
    SET_LOG_CHECK_MODE();
    CK(OB_NOT_NULL(ctx_.secondary_namespace_->get_external_ns()));
    if (OB_SUCC(ret)) {
      ObArray<ObQualifiedName> fake_columns;
      ObArray<ObRawExpr*> fake_exprs;
      if (OB_FAIL(ObResolverUtils::resolve_external_symbol(allocator,
                                                           expr_factory,
                                                           ctx_.secondary_namespace_->get_external_ns()->get_resolve_ctx().session_info_,
                                                           ctx_.secondary_namespace_->get_external_ns()->get_resolve_ctx().schema_guard_,
                                                           &ctx_.secondary_namespace_->get_external_ns()->get_resolve_ctx().sql_proxy_,
                                                           ctx_.external_param_info_,
                                                           ctx_.secondary_namespace_,
                                                           q_name,
                                                           fake_columns,
                                                           fake_exprs,
                                                           var,
                                                           &ctx_.secondary_namespace_->get_external_ns()->get_resolve_ctx().package_guard_,
                                                           ctx_.param_list_,
                                                           false,/*is_prepare_protocol*/
                                                           true,/*is_check_mode*/
                                                           ctx_.current_scope_ != T_PL_SCOPE /*is_sql_scope*/))) {
        LOG_INFO("failed to resolve external symbol", K(q_name), K(ret));
        ret = OB_SUCCESS;
        ob_reset_tsi_warning_buffer();
      } else if (OB_ISNULL(var)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (T_QUESTIONMARK == var->get_expr_type()) { // PL base variable
        is_pl_var = true;
        q_name.access_idents_.at(q_name.access_idents_.count() - 1).set_pl_var();
      } else if (var->is_obj_access_expr()) {
        ObObjAccessRawExpr* obj_access_expr = static_cast<ObObjAccessRawExpr*>(var);
        OX (is_pl_var = true);
        CK (OB_NOT_NULL(obj_access_expr));
        if (OB_FAIL(ret)) {
        } else if (obj_access_expr->is_property()) {
          q_name.access_idents_.at(q_name.access_idents_.count() - 1).set_type_method();
        } else {
          q_name.access_idents_.at(q_name.access_idents_.count() - 1).set_pl_var();
        }
      } else { /*do nothing*/ }
    }
    CANCLE_LOG_CHECK_MODE();
  }
  return ret;
}

int ObRawExprResolverImpl::check_sys_func(ObQualifiedName &q_name, bool &is_sys_func)
{
  int ret = OB_SUCCESS;
  is_sys_func = 1 == q_name.access_idents_.count()
      && (IS_FUN_SYS_TYPE(ObExprOperatorFactory::get_type_by_name(q_name.access_idents_.at(0).access_name_))
          || 0 == q_name.access_idents_.at(0).access_name_.case_compare("sqlerrm")
          || 0 == q_name.access_idents_.at(0).access_name_.case_compare("sqlcode"))
      // AUTOINC NEXTVAL is generated internally. A user-written NEXTVAL/CURRVAL call
      // must still be resolved as a stored function after SQL Sequence is removed.
      && q_name.access_idents_.at(0).access_name_.case_compare("nextval") != 0
      && q_name.access_idents_.at(0).access_name_.case_compare("currval") != 0;

  if (is_sys_func) {
    q_name.access_idents_.at(0).set_sys_func();
  }
  return ret;
}
int ObRawExprResolverImpl::check_pl_udf(ObQualifiedName &q_name,
                                        const ObSQLSessionInfo *session_info,
                                        ObSchemaChecker *schema_checker,
                                        pl::ObPLBlockNS *secondary_namespace,
                                        pl::ObProcType &proc_type)
{
  int ret = OB_SUCCESS;
  bool is_pl_udf = false;
  proc_type = pl::INVALID_PROC_TYPE;
  if (1 == q_name.access_idents_.count()) {
    if (OB_FAIL(ObResolverUtils::check_routine_exists(session_info,
                                                       schema_checker,
                                                       secondary_namespace,
                                                       ObString(""),
                                                       ObString(""),
                                                       q_name.access_idents_.at(0).access_name_,
                                                       share::schema::ObRoutineType::ROUTINE_FUNCTION_TYPE,
                                                       is_pl_udf,
                                                       proc_type))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

// PL SCOPE : local udf/local variables > sys function > public function
// SQL SCOPE : sys function > local udf/local variables > public function
// actually we only need recongnise sys function
int ObRawExprResolverImpl::check_name_type(
  ObQualifiedName &q_name, ObStmtScope scope, AccessNameType &type)
{
  int ret = OB_SUCCESS;

  SET_LOG_CHECK_MODE();

  type = UNKNOWN;
  bool check_success = false;
  pl::ObProcType proc_type = pl::INVALID_PROC_TYPE;

  if (T_PL_SCOPE == scope) { //PL Scope: Variable > UDF > SysFunction
    if (OB_FAIL(check_pl_variable(q_name, check_success))) {
    } else if (check_success) {
      type = q_name.is_type_method() ? TYPE_METHOD : PL_VAR;
    } else if (OB_FAIL(check_sys_func(q_name, check_success))) {
    } else if (check_success) {
      // Variables > System function > Local pl function, so here only need to check local pl function.
      if (OB_FAIL(check_pl_udf(q_name,
                               ctx_.session_info_,
                               ctx_.schema_checker_,
                               ctx_.secondary_namespace_,
                               proc_type))) {
      } else if (proc_type != pl::INVALID_PROC_TYPE && proc_type != pl::STANDALONE_FUNCTION) {
        q_name.access_idents_.at(0).set_pl_udf();
        type = PL_UDF;
      } else {
        type = SYS_FUNC;
      }
    } else { // not variables, not system function, must be udf, do not check it.
      q_name.access_idents_.at(q_name.access_idents_.count() - 1).set_pl_udf();
      type = PL_UDF;
    }
  } else { //SQL Scope: System function > Variable > All pl functions
    if (OB_FAIL(check_sys_func(q_name, check_success))) {
    } else if (check_success) {
      type = SYS_FUNC;
    } else if (OB_FAIL(check_pl_variable(q_name, check_success))) {
    } else if (check_success) {
      type = q_name.is_type_method() ? TYPE_METHOD : PL_VAR;;
    } else {
      q_name.access_idents_.at(q_name.access_idents_.count() - 1).set_pl_udf();
      type = PL_UDF;
    }
  }

  CANCLE_LOG_CHECK_MODE();
  return ret;
}

int ObRawExprResolverImpl::resolve_func_node_of_obj_access_idents(const peg::Node &node, ObQualifiedName &name)
{
  int ret = OB_SUCCESS;
  for (const auto &qualifier : peg::call_qualifiers(node)) {
    ObString text;
    const auto qualifier_text = qualifier.text();
    OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(qualifier_text), text));
    OZ(name.access_idents_.push_back(ObObjAccessIdent(text, OB_INVALID_INDEX)));
  }
  const auto spelling = peg::call_name(node);
  ObString text;
  OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(spelling), text));
  OZ(name.access_idents_.push_back(ObObjAccessIdent(text, OB_INVALID_INDEX)));
  AccessNameType type = UNKNOWN;
  if (OB_SUCC(ret)) {
    name.access_idents_.at(name.access_idents_.count() - 1).has_brackets_ = true;
    OZ(check_name_type(name, ctx_.current_scope_, type));
    if (type == PL_VAR) {
      type = PL_UDF;
      name.access_idents_.at(name.access_idents_.count() - 1).set_pl_udf();
    }
  }
  if (OB_SUCC(ret)) {
    auto &identifier = name.access_idents_.at(name.access_idents_.count() - 1);
    ObRawExpr *function = nullptr;
    if (type == SYS_FUNC) {
      if (text.case_compare("SQLERRM") == 0) OZ(process_sqlerrm_node(node, function));
      else if (text.case_compare("SQLCODE") == 0) OZ(process_sqlcode_node(node, function));
      else if (text.case_compare("json_equal") == 0) ret = OB_ERR_JSON_EQUAL_OUTSIDE_PREDICATE;
      else OZ(process_fun_sys_node(node, function, false));
      OX(identifier.sys_func_expr_ = function);
    } else if (type == PL_UDF) {
      OZ(resolve_stored_function(node, identifier.udf_info_));
      OX(function = identifier.udf_info_.ref_expr_);
    } else if (type == TYPE_METHOD) {
      const auto arguments = peg::call_arguments(node);
      if (text.case_compare("LIMIT") == 0 || text.case_compare("COUNT") == 0) {
        if (!arguments.empty()) {
          ret = OB_ERR_CALL_WRONG_ARG;
          LOG_USER_ERROR(OB_ERR_CALL_WRONG_ARG, text.length(), text.ptr());
        }
      } else if (arguments.size() != 1) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        ObRawExpr *index = nullptr;
        const int64_t start = ctx_.columns_->count();
        OZ(ctx_.parents_expr_info_.add_member(IS_PL_ACCESS_IDX));
        OZ(SMART_CALL(recursive_resolve(arguments[0], index)));
        OZ(identifier.params_.push_back(std::make_pair(index, int64_t(0))));
        const int cleanup_ret = ctx_.parents_expr_info_.del_member(IS_PL_ACCESS_IDX);
        if (OB_SUCC(ret)) ret = cleanup_ret;
        for (int64_t i = start; i < ctx_.columns_->count(); ++i) ctx_.columns_->at(i).is_access_root_ = false;
      }
    } else ret = OB_ERR_UNEXPECTED;
    for (int64_t i = 0; OB_SUCC(ret) && function != nullptr && i < function->get_param_count(); ++i) {
      OZ(identifier.params_.push_back(std::make_pair(function->get_param_expr(i), int64_t(0))));
    }
  }
  return ret;
}

int ObRawExprResolverImpl::mock_enum_type_info(common::ObIAllocator &allocator, ObString &string, uint64_t idx, ObIArray<common::ObString> &type_info)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && idx > 0 && i < idx - 1; ++i) {
    if(OB_FAIL(type_info.push_back(ObString(0, NULL)))) {
    }
  }
  if(OB_SUCC(ret)) {
    ObString temp_str;
    if (OB_FAIL(ob_write_string(allocator, string, temp_str))) {
    } else if (OB_FAIL(type_info.push_back(temp_str))) {
    }
  }
  return ret;
}

int ObRawExprResolverImpl::mock_set_type_info(common::ObIAllocator &allocator, ObString &string, uint64_t element_val, ObIArray<common::ObString> &type_info)
{
  int ret = OB_SUCCESS;
  void *mem = NULL;
  if (element_val == 0) {
    if (OB_FAIL(type_info.push_back(ObString(0, NULL)))) {
    }
  } else {
    const int64_t effective_count = 64;
    const int64_t element_num = effective_count - static_cast<int64_t>(__builtin_clzll(element_val));
    ObString element_str = ObString();
    for (int64_t i = 0; OB_SUCC(ret) && i < element_num && i < effective_count; ++i) {
      if (i == element_num - 1) { //last set element
        element_str = string;
      } else if (element_val & 1U) { //this bit has set element
        element_str = string.split_on(',');
      } else { //this bit does not have set element
        element_str = ObString(0, NULL);
      }
      ObString temp_str;
      if (OB_FAIL(ob_write_string(allocator, element_str, temp_str))) {
      } else if (OB_FAIL(type_info.push_back(temp_str))) {
      }
      OX (element_val >>= 1);
    }
  }
  return ret;
}
int ObRawExprResolverImpl::resolve_cast_collation(const ObString &charset_name, ObCollationType &collation)
{
  int ret = OB_SUCCESS;
  if (collation != CS_TYPE_INVALID) {
  } else if (!charset_name.empty()) {
    ObString name = charset_name;
    const ObCharsetType charset = ObCharset::charset_type(name.trim());
    if (charset == CHARSET_INVALID) { ret = OB_ERR_UNKNOWN_CHARSET; }
    else { collation = ObCharset::get_default_collation(charset); }
  } else if (ctx_.is_in_system_view_) {
    collation = ObCharset::get_system_collation();
  } else if (OB_ISNULL(ctx_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    OZ (ctx_.session_info_->get_collation_connection(collation));
  }
  return ret;
}

int ObRawExprResolverImpl::build_cast_argument(const ObRawExprResType &target,
                                              const ObString &charset_name, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObConstRawExpr *constant = nullptr;
  ObCollationType collation = target.get_collation_type();
  CastTypeDescriptor descriptor;
  descriptor.set_type(target.get_type());
  if (ob_is_string_tc(target.get_type())) {
    descriptor.set_length(target.get_length());
  } else {
    descriptor.set_precision(target.get_precision());
    descriptor.set_scale(target.get_scale());
  }
  OZ (ctx_.expr_factory_.create_raw_expr(T_INT, constant));
  OZ (resolve_cast_collation(charset_name, collation));
  if (OB_SUCC(ret)) {
    ObObj value;
    descriptor.set_collation(collation);
    value.set_int(descriptor.value());
    constant->set_value(value);
    expr = constant;
  }
  return ret;
}

int ObRawExprResolverImpl::build_implicit_literal(const LiteralToken &literal, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = ctx_.session_info_;
  bool decimal_int = false;
  bool mysql_dates = false;
  ObObjParam value;
  ObString prefix;
  ObConstRawExpr *constant = nullptr;
  const ObItemType type = literal_expression_type(literal.kind);
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session, decimal_int))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(session, false, mysql_dates))) {
  } else {
    const LiteralDecodeOptions options{{session->get_stmt_type(), decimal_int,
        ctx_.secondary_namespace_ != nullptr, ctx_.formalize_const_int_prec_,
        session->get_min_const_integer_precision()}, ctx_.dest_collation_, ctx_.tz_info_,
        session->get_sql_mode(), share::COMPAT_MYSQL57, mysql_dates, false, nullptr,
        NumericSignMode::Source};
    OZ (ObResolverUtils::resolve_literal(literal, options, ctx_.expr_factory_.get_allocator(), value, prefix));
    ObObjParam owned_value = value;
    OZ(ob_write_obj(ctx_.expr_factory_.get_allocator(), value, owned_value));
    OX(value = owned_value);
    ObString owned_prefix;
    OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), prefix, owned_prefix));
    OX(prefix = owned_prefix);
    OZ (ctx_.expr_factory_.create_raw_expr(type, constant));
    if (OB_SUCC(ret)) {
      constant->set_value(value);
      constant->set_expr_obj_meta(value.get_param_meta());
      constant->set_literal_prefix(prefix);
      constant->set_accuracy(value.get_accuracy());
      constant->set_result_flag(value.get_result_flag());
      expr = constant;
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_odbc_escape(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  const auto action = peg::odbc_action(node);
  const auto &operand = peg::odbc_operand(node);
  bool converted = false;
  if (action != peg::OdbcAction::Expression) {
    const auto literal = peg::expression_literal(operand);
    const ParameterBinding *binding = ctx_.parameterization_ != nullptr
        ? ctx_.parameterization_->find(&operand) : nullptr;
    const int64_t index = binding != nullptr ? static_cast<int64_t>(binding->parameter_index)
        : literal.kind == peg::LiteralKind::Placeholder
        ? static_cast<int64_t>(peg::placeholder_index(operand)) : -1;
    ObString text;
    rust::Vec<uint8_t> decoded;
    if (index < 0) {
      converted = true;
      if (literal.kind == peg::LiteralKind::Binary) {
        decoded = peg::decode_binary_literal(literal.text, literal.binary_notation);
        text.assign_ptr(reinterpret_cast<const char *>(decoded.data()), static_cast<int64_t>(decoded.size()));
      } else {
        text.assign_ptr(syntax_text(literal.text).ptr(), static_cast<int64_t>(literal.text.size()));
      }
    } else if (OB_ISNULL(ctx_.param_list_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (ctx_.param_list_->empty()) {
      // The prepare phase has no value to convert.
    } else if (index >= ctx_.param_list_->count()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (ctx_.param_list_->at(index).is_varchar()) {
      converted = true;
      text = ctx_.param_list_->at(index).get_varchar();
    }
    if (OB_SUCC(ret) && converted) {
      const auto kind = action == peg::OdbcAction::Date ? peg::LiteralKind::Date
          : action == peg::OdbcAction::Time ? peg::LiteralKind::Time : peg::LiteralKind::Timestamp;
      const LiteralToken temporal{kind, 0, text, {}, false, 0};
      if (OB_FAIL(build_implicit_literal(temporal, expr))) {
        if (ret == OB_ERR_WRONG_VALUE) {
          ret = OB_SUCCESS;
          converted = false;
        }
      } else if (index >= 0) {
        ObPCConstParamInfo constraint;
        if (OB_ISNULL(ctx_.query_ctx_)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          OZ (constraint.const_idx_.push_back(index));
          OZ (constraint.const_params_.push_back(ctx_.param_list_->at(index)));
          OZ (ctx_.query_ctx_->all_plan_const_param_constraints_.push_back(constraint));
        }
      }
    }
  }
  if (OB_SUCC(ret) && !converted) {
    OZ (recursive_resolve(operand, expr));
  }
  return ret;
}

int ObRawExprResolverImpl::process_datatype_or_questionmark(const peg::Node &node, ObRawExpr *&expr)
{
  if (node.kind() == peg::NodeKind::Parameter && !ctx_.is_variable_allowed_) {
    return OB_ERR_VIEW_SELECT_CONTAIN_QUESTIONMARK;
  }
  int ret = OB_SUCCESS;
  ObObjParam val;
  ObConstRawExpr *c_expr = NULL;
  ObString literal_prefix;
  const auto span = node.span();
  const ParameterBinding *binding = ctx_.parameterization_ != nullptr
      ? ctx_.parameterization_->find(&node) : nullptr;
  const NumericLowering *lowering = ctx_.parameterization_ != nullptr
      ? ctx_.parameterization_->numeric_lowering(&node) : nullptr;
  ObItemType expr_type = binding != nullptr ? T_QUESTIONMARK
      : peg::is_literal(node) ? literal_expression_type(peg::expression_literal(node).kind) : T_INVALID;
  const ObSQLSessionInfo *session_info = ctx_.session_info_;
  bool enable_decimal_int = false;
  share::ObCompatType compat_type = share::COMPAT_MYSQL57;
  bool enable_mysql_compatible_dates = false;
  if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session_info, enable_decimal_int))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(session_info, false,
                       enable_mysql_compatible_dates))) {
  }

  if (OB_FAIL(ret)) { // do nothing
  } else if (binding != nullptr) {
    val.set_unknown(binding->parameter_index);
    val.set_param_meta(val.get_meta());
  } else if (lowering != nullptr && lowering->operation != peg::ParameterLoweringKind::AddNegatedOperand) {
    const auto &value = lowering->value;
    NumericKind kind = NumericKind::Integer;
    switch (value.kind) {
      case peg::LiteralKind::Integer: kind = NumericKind::Integer; expr_type = T_INT; break;
      case peg::LiteralKind::Unsigned: kind = NumericKind::Unsigned; expr_type = T_UINT64; break;
      case peg::LiteralKind::Decimal: kind = NumericKind::Decimal; expr_type = T_NUMBER; break;
      case peg::LiteralKind::Float: kind = NumericKind::Double; expr_type = T_DOUBLE; break;
      default: ret = OB_ERR_UNEXPECTED; break;
    }
    const NumericLiteral literal{kind, value.integer,
        ObString(value.text.size(), reinterpret_cast<const char *>(value.text.data()))};
    const NumericLiteralOptions options{session_info->get_stmt_type(), enable_decimal_int,
        nullptr != ctx_.secondary_namespace_, ctx_.formalize_const_int_prec_,
        session_info->get_min_const_integer_precision()};
    OZ (ObResolverUtils::resolve_numeric_literal(literal, options,
        ctx_.expr_factory_.get_allocator(), val, literal_prefix));
    if (OB_SUCC(ret) && node.kind() == peg::NodeKind::DateUnit && span.start != span.end
        && (literal.kind == NumericKind::Integer || literal.kind == NumericKind::Unsigned)) {
      literal_prefix = literal.text;
    }
  } else {
    const LiteralDecodeOptions options{{session_info->get_stmt_type(), enable_decimal_int,
        ctx_.secondary_namespace_ != nullptr, ctx_.formalize_const_int_prec_,
        session_info->get_min_const_integer_precision()}, ctx_.dest_collation_, ctx_.tz_info_,
        session_info->get_sql_mode(), compat_type, enable_mysql_compatible_dates, false,
        nullptr, NumericSignMode::Source};
    OZ (ObResolverUtils::resolve_expression_literal(node, options,
        ctx_.expr_factory_.get_allocator(), val, literal_prefix));
  }
  if (OB_SUCC(ret) && lowering != nullptr && !literal_prefix.empty()) {
    ObString owned_prefix;
    OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), literal_prefix, owned_prefix));
    OX(literal_prefix = owned_prefix);
  }
  if (OB_SUCC(ret) && node.kind() == peg::NodeKind::DateUnit && span.start != span.end) {
    OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), source_text(node), literal_prefix));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(expr_type, c_expr))) {
  } else if (OB_ISNULL(c_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (node.kind() == peg::NodeKind::DateUnit && span.start != span.end) {
      c_expr->set_is_date_unit();
    }
    c_expr->set_value(val);
    // Reset meta information to an empty string
    c_expr->set_expr_obj_meta(val.get_param_meta());
    c_expr->set_literal_prefix(literal_prefix);
    // for mysql mode distinguish tinyint and literal bool
    if (node.kind() == peg::NodeKind::Bool) {
      c_expr->set_is_literal_bool(true);
    }
    if (!val.is_unknown()) {
      c_expr->set_accuracy(val.get_accuracy());
      c_expr->set_result_flag(val.get_result_flag()); // not_null etc
    } else {
      if (!ctx_.is_extract_param_type_) {
        if (NULL == ctx_.param_list_) {
          LOG_INFO("NOTICE : UNIT TEST Code direction.", K(ret));
        } else if (ctx_.param_list_->empty()) { // reach here means current val is ?, but param_list is empty, means it's prepare stage
          ObObjMeta question_mark_meta;
          // Compatible with MySQL, use the default varbinary type for type deduce in the ps prepare phase
          question_mark_meta.set_varbinary();
          question_mark_meta.set_collation_level(CS_LEVEL_IMPLICIT);
          c_expr->set_meta_type(question_mark_meta);
          c_expr->set_expr_obj_meta(question_mark_meta);
          if (NULL == ctx_.external_param_info_) {
            /*do nothing...*/
          } else if (ctx_.is_for_dynamic_sql_ && OB_NOT_NULL(session_info->get_pl_context())){
            // PL dynamic SQL already prepares question marks in the parse stage.
            bool need_save = true;
            for (int64_t i = 0; OB_SUCC(ret) && i < ctx_.external_param_info_->count(); ++i) {
              CK (OB_NOT_NULL(ctx_.external_param_info_->at(i).element<0>()));
              if (OB_SUCC(ret)
                  && ctx_.external_param_info_->at(i).element<0>()->same_as(*c_expr)) {
                need_save = false;
                break;
              }
            }
            if (OB_SUCC(ret) && need_save) {
              OZ (ctx_.external_param_info_->push_back(ExternalParamInfo(c_expr, c_expr, 1)));
              OX (ctx_.prepare_param_count_++);
            }
          } else {
            // prepare stmt does not need type, only need to count the number of ?
            if (OB_SUCC(ret) && nullptr != ctx_.secondary_namespace_) {
              const pl::ObPLSymbolTable* symbol_table = NULL;
              const pl::ObPLVar* var = NULL;
              CK (OB_NOT_NULL(symbol_table = ctx_.secondary_namespace_->get_symbol_table()));
              if (OB_SUCC(ret)
                  && OB_NOT_NULL(var = symbol_table->get_symbol(val.get_unknown()))
                  && 0 == var->get_name().case_compare(pl::ObPLResolver::ANONYMOUS_ARG)) {
                if (OB_NOT_NULL(var->get_type().get_meta_type())) {
                  CK (OB_NOT_NULL(var->get_type().get_data_type()));
                  OX (c_expr->set_meta_type(*var->get_type().get_meta_type()));
                  OX (c_expr->set_expr_obj_meta(*var->get_type().get_meta_type()));
                  OX (c_expr->set_accuracy(var->get_type().get_data_type()->get_accuracy()));
                } else {
                  ObObjMeta meta;
                  OX (meta.set_type(ObExtendType));
                  OX (c_expr->set_meta_type(meta));
                  OX (c_expr->set_udt_id(var->get_type().get_user_type_id()));
                }
              }
            }
            ObRawExpr *original_expr = c_expr;
            OZ (ObResolverUtils::resolve_external_param_info(*ctx_.external_param_info_,
                                                             *session_info,
                                                             ctx_.expr_factory_,
                                                             ctx_.prepare_param_count_,
                                                             original_expr));
            OX (c_expr = static_cast<ObConstRawExpr*>(original_expr));
          }
        } else if (val.get_unknown() >= ctx_.param_list_->count()) {
          ret = OB_ERR_NOT_ALL_VARIABLE_BIND;
          LOG_WARN("question mark index out of param list count",
                   "index", val.get_unknown(), "param_count", ctx_.param_list_->count());
        } else { // with parameter prepare or execute stmt stage, note: will there be a problem if c_expr's accuracy is not set here
          const ObObjParam &param = ctx_.param_list_->at(val.get_unknown());
          c_expr->set_is_literal_bool(param.is_boolean());
          if (param.is_ext()) {
              c_expr->set_meta_type(param.get_meta());
              c_expr->set_expr_obj_meta(param.get_param_meta());
              c_expr->set_udt_id(param.get_udt_id());
              c_expr->set_obj_param(param);
          } else {
            if (ObNullType == param.get_type()) {
              c_expr->set_meta_type(param.get_param_meta());
            } else {
              c_expr->set_meta_type(param.get_meta());
            }
            c_expr->set_expr_obj_meta(param.get_param_meta());
            c_expr->set_accuracy(param.get_accuracy());
            c_expr->set_result_flag(param.get_result_flag()); // not_null etc
            c_expr->set_obj_param(param);

            sql::ObRawExprResType result_type = c_expr->get_result_type();
            if (result_type.get_length() == -1) {
              if (result_type.is_varchar()) {
                result_type.set_length(OB_MAX_EXTENDED_VARCHAR_LENGTH);
              } else if (result_type.is_char()) {
                result_type.set_length(OB_MAX_EXTENDED_CHAR_LENGTH_BYTE);
              }
            }
            if (-1 == result_type.get_length_semantics() &&
              ObNullType == param.get_type() &&
              ob_is_string_tc(param.get_param_meta().get_type())) {
              result_type.set_length_semantics(session_info->get_actual_length_semantics());
            }
            c_expr->set_result_type(result_type);
            if (ob_is_enumset_inner_tc(c_expr->get_result_type().get_type())) { // only in PL execute, enum or set paramters generate enumset_inner type param value
              ObDatum datum;
              ObEnumSetInnerValue inner_value;
              ObSEArray<common::ObString, 64> type_info_value;
              if (ObNullType != param.get_type()) {
                OZ (datum.from_obj(param));
                OZ (datum.get_enumset_inner(inner_value));
                if (ObEnumInnerType == c_expr->get_result_type().get_type()) {
                  OZ (mock_enum_type_info(ctx_.expr_factory_.get_allocator(), inner_value.string_value_, inner_value.numberic_value_, type_info_value));
                } else {
                  OZ (mock_set_type_info(ctx_.expr_factory_.get_allocator(), inner_value.string_value_, inner_value.numberic_value_, type_info_value));
                }
              } else {
                OZ(type_info_value.push_back(ObString(0, NULL))); //mock null param type_info
              }
              if (OB_SUCC(ret)) {
                ObSysFunRawExpr *out_expr = NULL;
                OZ (ObRawExprUtils::create_inner_type_to_enumset_expr(ctx_.expr_factory_, c_expr, out_expr, ctx_.session_info_, type_info_value));
                OX (c_expr = (ObConstRawExpr*)(out_expr));
              }
            }
          }
          if (OB_SUCC(ret) && nullptr != ctx_.secondary_namespace_) {
            const pl::ObPLSymbolTable* symbol_table = NULL;
            const pl::ObPLVar* var = NULL;
            CK (OB_NOT_NULL(symbol_table = ctx_.secondary_namespace_->get_symbol_table()));
            CK (OB_NOT_NULL(var = symbol_table->get_symbol(val.get_unknown())));
            if (OB_SUCC(ret)) {
              if (var->get_name().prefix_match(pl::ObPLResolver::ANONYMOUS_ARG)) {
                if (!var->is_readonly()) {
                  // set in out flag
                  OX (const_cast<pl::ObPLVar*>(var)->set_name(pl::ObPLResolver::ANONYMOUS_INOUT_ARG));
                }
                OX (const_cast<pl::ObPLVar*>(var)->set_is_referenced(true));
              }
            }
          }
//execute stage does not need to count the number of prepare_param_count_
//          ctx_.prepare_param_count_++;
//          LOG_DEBUG("prepare stmt add new param", K(ctx_.prepare_param_count_));
        }
      } else {
        int64_t param_idx = val.get_unknown();
        if (OB_ISNULL(ctx_.param_list_)) {
          ret = OB_NOT_INIT;
        } else if (param_idx >= ctx_.param_list_->count()) {
          ret = OB_ERR_BIND_VARIABLE_NOT_EXIST;
        } else {
          const ObObjParam &param = ctx_.param_list_->at(param_idx);
          c_expr->set_is_literal_bool(param.is_boolean());
          if (param.is_ext()) {
            // values statement
            if (param.is_ext_sql_array()) {
              ObSqlArrayObj *param_array = reinterpret_cast<ObSqlArrayObj*>(param.get_ext());
              const ObExecContext *exec_ctx = NULL;
              const ObPhysicalPlanCtx *phy_ctx = NULL;
              if (OB_ISNULL(param_array) || OB_ISNULL(ctx_.session_info_) ||
                  OB_ISNULL(exec_ctx = ctx_.session_info_->get_cur_exec_ctx()) ||
                  OB_ISNULL(phy_ctx = ctx_.session_info_->get_cur_exec_ctx()->get_physical_plan_ctx())) {
                ret = OB_ERR_UNEXPECTED;
              } else {
                c_expr->set_meta_type(param_array->element_.get_meta_type());
                c_expr->set_expr_obj_meta(param_array->element_.get_meta_type());
                c_expr->set_accuracy(param_array->element_.get_accuracy());
                c_expr->set_obj_param(param);
                const ObIArray<ObArrayParamGroup> &array_param_groups = phy_ctx->get_array_param_groups();
                for (int64_t i = 0; i < array_param_groups.count(); i++) {
                  const ObArrayParamGroup &group = array_param_groups.at(i);
                  if (param_idx >= group.start_param_idx_ &&
                      param_idx < group.start_param_idx_ + group.column_count_) {
                    c_expr->set_array_param_group_id(i);
                    break;
                  }
                }
              }
            } else {
              ret = OB_NOT_SUPPORTED;
            }
          } else {
            //questionmark won't set meta_type again
            if (param.get_param_meta().get_type() != param.get_type()) {
              LOG_TRACE("question mark not suited", K(param.get_param_meta().get_type()), K(param.get_type()), K(common::lbt()));
            }
            c_expr->set_meta_type(param.get_param_meta());
            c_expr->set_expr_obj_meta(param.get_param_meta());
            c_expr->set_accuracy(param.get_accuracy());
            c_expr->set_result_flag(param.get_result_flag()); // not_null etc
            c_expr->set_obj_param(param);
          }
          if (OB_SUCC(ret) && nullptr != ctx_.secondary_namespace_) {
            const pl::ObPLSymbolTable* symbol_table = NULL;
            const pl::ObPLVar* var = NULL;
            CK (OB_NOT_NULL(symbol_table = ctx_.secondary_namespace_->get_symbol_table()));
            CK (OB_NOT_NULL(var = symbol_table->get_symbol(val.get_unknown())));
            if (OB_SUCC(ret)) {
              if (var->get_name().prefix_match(pl::ObPLResolver::ANONYMOUS_ARG)) {
                if (!var->is_readonly()) {
                  // set in out flag
                  OX (const_cast<pl::ObPLVar*>(var)->set_name(pl::ObPLResolver::ANONYMOUS_INOUT_ARG));
                }
                OX (const_cast<pl::ObPLVar*>(var)->set_is_referenced(true));
              }
            }
          }
        }
      }
      // force sql run das logic if sql stmt has pl question mark expr, and its type is complex type
      if (OB_SUCC(ret) &&
          OB_NOT_NULL(ctx_.stmt_) &&
          OB_NOT_NULL(c_expr) &&
          OB_NOT_NULL(session_info->get_pl_context()) &&
          T_QUESTIONMARK == c_expr->get_expr_type() &&
          c_expr->get_result_type().is_ext() &&
          (pl::PL_RECORD_TYPE == c_expr->get_result_type().get_extend_type() ||
           pl::PL_NESTED_TABLE_TYPE == c_expr->get_result_type().get_extend_type() ||
           pl::PL_ASSOCIATIVE_ARRAY_TYPE == c_expr->get_result_type().get_extend_type() ||
           pl::PL_VARRAY_TYPE == c_expr->get_result_type().get_extend_type() ||
           pl::PL_CURSOR_TYPE == c_expr->get_result_type().get_extend_type() ||
           pl::PL_OPAQUE_TYPE == c_expr->get_result_type().get_extend_type())) {
        ctx_.stmt_->get_query_ctx()->disable_udf_parallel_ |= true;
      }
    }
    expr = c_expr;
  }
  return ret;
}

int ObRawExprResolverImpl::process_system_variable_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObString name;
  const auto spelling = peg::variable_name(node);
  if (!ctx_.is_variable_allowed_) { ret = OB_ERR_VIEW_SELECT_CONTAIN_QUESTIONMARK; }
  OZ (ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(spelling), name));
  const auto scope = peg::variable_scope(node);
  const ObSetVar::SetScopeType set_scope = scope == peg::VariableScope::Global ? ObSetVar::SET_SCOPE_GLOBAL
      : scope == peg::VariableScope::Session ? ObSetVar::SET_SCOPE_SESSION : ObSetVar::SET_SCOPE_NEXT_TRANS;
  OZ (ObRawExprUtils::build_get_sys_var(ctx_.expr_factory_, name, set_scope, expr));
  if (OB_SUCC(ret)) {
    ObVarInfo info; info.name_ = name; info.type_ = SYS_VAR;
    OZ (ctx_.sys_vars_->push_back(info));
  }
  return ret;
}

int ObRawExprResolverImpl::process_char_charset_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObString name;
  const auto node_text = node.text();
  OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(node_text), name));
  if (OB_SUCC(ret) && ObCharset::charset_type(name) == CHARSET_INVALID) {
    ret = OB_ERR_UNKNOWN_CHARSET;
    LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, name.length(), name.ptr());
  }
  ObConstRawExpr *constant = nullptr;
  OZ(ctx_.expr_factory_.create_raw_expr(T_VARCHAR, constant));
  if (OB_SUCC(ret)) {
    ObObj value;
    value.set_varchar(name);
    value.set_collation_type(ObCharset::get_system_collation());
    value.set_collation_level(CS_LEVEL_COERCIBLE);
    constant->set_value(value);
    expr = constant;
  }
  return ret;
}


int ObRawExprResolverImpl::process_vector_func_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  const bool similarity = node.kind() == peg::NodeKind::VectorSimilarity;
  const auto arguments = peg::call_arguments(node);
  const auto metric = peg::vector_metric(node);
  OZ(ctx_.expr_factory_.create_raw_expr(similarity ? T_FUN_SYS_VECTOR_SIMILARITY : T_FUN_SYS_VECTOR_DISTANCE, function));
  OX(function->set_func_name(similarity ? N_VECTOR_SIMILARITY : N_VECTOR_DISTANCE));
  OZ(function->init_param_exprs(arguments.size() + (metric != peg::VectorMetric::Unspecified)));
  for (const auto &argument : arguments) {
    ObRawExpr *value = nullptr;
    OZ(SMART_CALL(recursive_resolve(argument, value)));
    OZ(function->add_param_expr(value));
  }
  if (OB_SUCC(ret) && metric != peg::VectorMetric::Unspecified) {
    int64_t value = 0;
    switch (metric) {
      case peg::VectorMetric::Cosine: value = 0; break;
      case peg::VectorMetric::Dot: value = 1; break;
      case peg::VectorMetric::Euclidean: value = 2; break;
      case peg::VectorMetric::Manhattan: value = 3; break;
      default: break;
    }
    ObRawExpr *argument = nullptr;
    OZ(build_implicit_literal({peg::LiteralKind::Integer, static_cast<uint64_t>(value), {}, {}, false, 0}, argument));
    OZ(function->add_param_expr(argument));
  }
  OX(expr = function);
  return ret;
}

int ObRawExprResolverImpl::set_geo_func_name(ObSysFunRawExpr *func_expr,
                                             const ObItemType func_type)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(func_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    switch (func_type) {
      case T_FUN_SYS_POINT: {
        OX(func_expr->set_func_name(N_POINT));
        break;
      }
      case T_FUN_SYS_LINESTRING: {
        OX(func_expr->set_func_name(N_LINESTRING));
        break;
      }
      case T_FUN_SYS_POLYGON: {
        OX(func_expr->set_func_name(N_POLYGON));
        break;
      }
      case T_FUN_SYS_MULTIPOINT: {
        OX(func_expr->set_func_name(N_MULTIPOINT));
        break;
      }
      case T_FUN_SYS_MULTILINESTRING: {
        OX(func_expr->set_func_name(N_MULTILINESTRING));
        break;
      }
      case T_FUN_SYS_MULTIPOLYGON: {
        OX(func_expr->set_func_name(N_MULTIPOLYGON));
        break;
      }
      case T_FUN_SYS_GEOMCOLLECTION: {
        OX(func_expr->set_func_name(N_GEOMCOLLECTION));
        break;
      }
      case T_FUN_SYS_ARRAY: {
        OX(func_expr->set_func_name(N_ARRAY));
        break;
      }
      case T_FUN_SYS_MAP: {
        OX(func_expr->set_func_name(N_MAP));
        break;
      }
      default: {
        ret = OB_INVALID_ARGUMENT;
        break;
      }
    }
  }

  return ret;
}

int ObRawExprResolverImpl::process_geo_func_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  const auto type = callable_expression_type(node.kind());
  OZ(check_scalar_quantifier(node));
  if (OB_SUCC(ret) && peg::call_arguments(node).empty()
      && (node.kind() == peg::NodeKind::LineString || node.kind() == peg::NodeKind::Polygon
          || node.kind() == peg::NodeKind::MultiPoint || node.kind() == peg::NodeKind::MultiLineString
          || node.kind() == peg::NodeKind::MultiPolygon)) {
    const auto name = peg::call_name(node);
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(name.size()), name.data());
  }
  OZ(ctx_.expr_factory_.create_raw_expr(type, function));
  OZ(set_geo_func_name(function, type));
  OZ(function->init_param_exprs(peg::call_arguments(node).size()));
  for (const auto &argument : peg::call_arguments(node)) {
    ObRawExpr *value = nullptr;
    OZ(SMART_CALL(recursive_resolve(argument, value)));
    OZ(function->add_param_expr(value));
  }
  OX(expr = function);
  return ret;
}


int ObRawExprResolverImpl::build_lambda_parameter(int64_t index, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObVarRawExpr *para_expr = NULL;
  if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_EXEC_VAR, para_expr))) {
  } else if (OB_ISNULL(para_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    para_expr->set_ref_index(index);
    expr = para_expr;
  }
  return ret;
}

int ObRawExprResolverImpl::extract_var_exprs(ObRawExpr *expr, ObIArray<ObVarRawExpr *> &var_exprs)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (expr->is_aggr_expr()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "aggr expr in lambda function");
  } else if (expr->get_expr_type() == T_EXEC_VAR) {
    ObVarRawExpr *var_expr = static_cast<ObVarRawExpr *>(expr);
    if (OB_FAIL(add_var_to_array_no_dup(var_exprs, var_expr))) {
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr->get_param_count(); ++i) {
      if (IS_ARRAY_MAP_OP(expr->get_expr_type())&& i == 0) {
      } else if (OB_FAIL(SMART_CALL(extract_var_exprs(expr->get_param_expr(i), var_exprs)))) {
      }
    }
  }
  return ret;
}

int ObRawExprResolverImpl::check_lambda_params_duplicated(const peg::Node &lambda)
{
  int ret = OB_SUCCESS;
  const auto parameters = peg::lambda_parameters(lambda);
  for (size_t i = 0; OB_SUCC(ret) && i < parameters.size(); ++i) {
    const auto parameter_text = parameters[i].text();
    for (size_t j = 0; OB_SUCC(ret) && j < i; ++j) {
      if (strncmp(syntax_text(parameters[j].text()).ptr(), syntax_text(parameter_text).ptr(), parameter_text.size()) == 0) {
        ret = OB_ERR_PARAM_DUPLICATE;
      }
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_lambda_func_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  ObRawExpr *body = nullptr;
  ObSEArray<ObString, 4> names;
  OZ (check_lambda_params_duplicated(node));
  for (const auto &name : peg::lambda_parameters(node)) {
    const auto name_text = name.text();
    OZ (names.push_back(syntax_text(name_text)));
  }
  if (OB_SUCC(ret)) {
    const auto *outer = lambda_parameters_;
    lambda_parameters_ = &names;
    ret = recursive_resolve(peg::lambda_body(node), body);
    lambda_parameters_ = outer;
  }
  if (OB_SUCC(ret) && !body->is_const_raw_expr() && !body->is_const_or_param_expr()
      && !body->is_column_ref_expr() && !body->is_sys_func_expr() && !body->is_op_expr()
      && !body->is_var_expr() && !body->is_case_op_expr()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "expr in lambda function");
  }
  OZ (ctx_.expr_factory_.create_raw_expr(T_FUNC_SYS_LAMBDA, function));
  OX (function->set_func_name("lambda"));
  OZ (function->set_param_expr(body));
  OX (expr = function);
  return ret;
}

int ObRawExprResolverImpl::process_array_map_func_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  ObRawExpr *lambda = nullptr;
  ObSEArray<ObRawExpr *, 4> arguments;
  ObSEArray<ObVarRawExpr *, 4> variables;
  const auto arrays = peg::call_arguments(node);
  const auto &definition = peg::collection_lambda(node);
  OZ (recursive_resolve(definition, lambda));
  if (OB_SUCC(ret) && arrays.size() != peg::lambda_parameters(definition).size()) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(strlen("lambda function")), "lambda function");
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < lambda->get_param_count(); ++i) {
    OZ (arguments.push_back(lambda->get_param_expr(i)));
    OZ (extract_var_exprs(lambda->get_param_expr(i), variables));
  }
  for (size_t i = 0; OB_SUCC(ret) && i < arrays.size(); ++i) {
    ObRawExpr *array = nullptr;
    OZ (recursive_resolve(arrays[i], array));
    OZ (arguments.push_back(array));
    for (int64_t j = 0; OB_SUCC(ret) && j < variables.count(); ++j) {
      if (variables.at(j)->get_ref_index() == i) { variables.at(j)->set_ref_expr(array); }
    }
  }
  OZ (ctx_.expr_factory_.create_raw_expr(callable_expression_type(node.kind()), function));
  OX (function->set_func_name("array_map"));
  OZ (function->set_param_exprs(arguments));
  OX (expr = function);
  return ret;
}

int ObRawExprResolverImpl::process_left_value_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObUserVarIdentRawExpr *var_expr = NULL;
  ObIArray<ObUserVarIdentRawExpr *> *all_vars =
      (NULL == ctx_.query_ctx_ ? NULL : &ctx_.query_ctx_->all_user_variable_);
  bool query_has_udf = (NULL == ctx_.query_ctx_ ? false : ctx_.query_ctx_->has_udf_);
  if (OB_ISNULL(ctx_.sys_vars_) || OB_ISNULL(ctx_.user_var_exprs_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObString str;
    OZ (ob_write_string(ctx_.expr_factory_.get_allocator(),
        ObString(peg::variable_name(node).size(), peg::variable_name(node).data()), str));
    OX (ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, str));
    ObObj val;
    val.set_varchar(str);
    val.set_collation_type(ObCharset::get_system_collation());
    val.set_collation_level(CS_LEVEL_IMPLICIT);

    if (NULL != all_vars) {
      for (int64_t i = 0; OB_SUCC(ret) && i < all_vars->count(); ++i) {
        if (OB_ISNULL(all_vars->at(i))) {
          ret = OB_ERR_UNEXPECTED;
        } else if (all_vars->at(i)->is_same_variable(val)) {
          var_expr = all_vars->at(i);
          var_expr->set_is_contain_assign(true);
        }
      }
    }
    if (OB_SUCC(ret) && NULL == var_expr) {
      if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_USER_VARIABLE_IDENTIFIER, var_expr))) {
      } else if (NULL != all_vars && OB_FAIL(all_vars->push_back(var_expr))) {
      } else {
        var_expr->set_value(val);
        var_expr->set_is_contain_assign(true);
      }
    }
    if (OB_SUCC(ret)) {
      var_expr->set_query_has_udf(query_has_udf);
      expr = var_expr;
      ObVarInfo var_info;
      var_info.name_.assign_ptr(str.ptr(), static_cast<int32_t>(str.length()));
      var_info.type_ = USER_VAR;
      if (OB_FAIL(ctx_.sys_vars_->push_back(var_info))) {
      } else if (OB_FAIL(add_var_to_array_no_dup(*ctx_.user_var_exprs_, var_expr))) {
      }
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_column_ref_node(const peg::Node &node, ObRawExpr *&expr, bool allow_star)
{
  int ret = OB_SUCCESS;
  int64_t lambda_index = -1;
  if (lambda_parameters_ != nullptr) {
    const ObString spelling = source_text(node);
    for (int64_t i = 0; i < lambda_parameters_->count() && lambda_index < 0; ++i) {
      if (spelling == lambda_parameters_->at(i)) { lambda_index = i; }
    }
  }
  if (lambda_index >= 0) {
    OZ (build_lambda_parameter(lambda_index, expr));
  } else if (ctx_.session_info_->is_for_trigger_package() && ObRawExprUtils::is_new_old_column_ref(node)) {
    ObQualifiedName name;
    OZ (ObRawExprUtils::resolve_trigger_column_name(node, ctx_.tg_timing_event_,
        ctx_.expr_factory_.get_allocator(), name));
    OZ (publish_qualified_name(name, ctx_.columns_->count(), expr));
  } else {
    ObQualifiedName name;
    OZ (ObResolverUtils::resolve_column_ref(node, ctx_.case_mode_, ctx_.expr_factory_.get_allocator(), name));
    if (OB_SUCC(ret) && name.is_star_ && !allow_star) { ret = OB_ERR_PARSER_SYNTAX; }
    if (OB_SUCC(ret) && name.database_name_.empty() && name.tbl_name_.empty() && !name.col_name_.empty()) {
      OZ (name.access_idents_.push_back(ObObjAccessIdent(name.col_name_, OB_INVALID_INDEX)));
    }
    if (OB_SUCC(ret)) {
      ObColumnRefRawExpr *column = nullptr;
      name.parents_expr_info_ = ctx_.parents_expr_info_;
      OZ (ctx_.expr_factory_.create_raw_expr(T_REF_COLUMN, column));
      OX (name.ref_expr_ = column);
      OZ (ctx_.columns_->push_back(name));
      OX (expr = column);
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_user_assignment(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *target = nullptr;
  ObRawExpr *value = nullptr;
  ObOpRawExpr *assignment = nullptr;
  is_contains_assignment_ = true;
  OZ (process_left_value_node(node, target));
  OZ (recursive_resolve(peg::variable_assignment_value(node), value));
  OZ (ctx_.expr_factory_.create_raw_expr(T_OP_ASSIGN, assignment));
  OZ (assignment->set_param_exprs(target, value));
  OX (expr = assignment);
  return ret;
}

int ObRawExprResolverImpl::process_any_or_all_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  const bool exists = node.kind() == peg::NodeKind::Exists;
  const peg::Node &query = exists ? peg::exists_query(node) : peg::quantified_query(node);
  ObRawExpr *operand = nullptr;
  ObOpRawExpr *operation = nullptr;
  if (exists) { OZ (ctx_.parents_expr_info_.add_member(IS_EXISTS)); }
  OZ (recursive_resolve(query, operand));
  const ObItemType type = exists ? T_OP_EXISTS
      : peg::quantified_comparison(node) == peg::ComparisonQuantifier::Any ? T_ANY : T_ALL;
  if (OB_SUCC(ret) && (operand->get_expr_type() == T_REF_QUERY
      || (!exists && operand->get_expr_type() == T_OP_ROW))) {
    if (operand->get_expr_type() == T_REF_QUERY) {
      static_cast<ObQueryRefRawExpr *>(operand)->set_is_set(true);
    }
    OZ (ctx_.expr_factory_.create_raw_expr(type, operation));
    OZ (operation->set_param_expr(operand));
    OX (expr = operation);
  } else if (OB_SUCC(ret) && !exists) {
    expr = operand;
  } else if (OB_SUCC(ret)) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObRawExprResolverImpl::process_user_var_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObString name;
  const auto spelling = peg::variable_name(node);
  if (!ctx_.is_variable_allowed_) { ret = OB_ERR_VIEW_SELECT_CONTAIN_QUESTIONMARK; }
  OZ (ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(spelling), name));
  OZ (ObRawExprUtils::build_get_user_var(ctx_.expr_factory_, name, expr, nullptr,
                                       ctx_.query_ctx_, ctx_.user_var_exprs_));
  if (OB_SUCC(ret)) {
    ObVarInfo info; info.name_ = name; info.type_ = USER_VAR;
    OZ (ctx_.sys_vars_->push_back(info));
  }
  return ret;
}

int ObRawExprResolverImpl::process_not_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *operand = nullptr;
  ObOpRawExpr *negation = nullptr;
  OZ (recursive_resolve(peg::unary_operand(node), operand));
  if (OB_SUCC(ret) && operand->get_expr_type() == T_OP_EXISTS) {
    operand->set_expr_type(T_OP_NOT_EXISTS); expr = operand;
  } else if (OB_SUCC(ret) && operand->get_expr_type() == T_OP_NOT_EXISTS) {
    operand->set_expr_type(T_OP_EXISTS); expr = operand;
  } else {
    OZ (ctx_.expr_factory_.create_raw_expr(T_OP_NOT, negation));
    OZ (negation->set_param_expr(operand));
    OZ (ObRawExprUtils::try_add_bool_expr(negation, ctx_.expr_factory_));
    OX (expr = negation);
  }
  return ret;
}

int ObRawExprResolverImpl::process_pos_or_neg_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  int64_t remaining = 0;
  const rust::Slice<const uint8_t> source{
      reinterpret_cast<const uint8_t *>(ctx_.syntax_source_.ptr()),
      static_cast<size_t>(ctx_.syntax_source_.length())};
  const auto *effects = ctx_.parameterization_ != nullptr ? ctx_.parameterization_->effects() : nullptr;
  const auto resolution = effects != nullptr
      ? peg::resolve_parameterized_unary(node, source, *effects)
      : peg::resolve_plain_unary(node, source);
  if (resolution.literal) {
    OZ(build_implicit_literal({resolution.kind, resolution.integer,
        ObString(resolution.text.size(), reinterpret_cast<const char *>(resolution.text.data())),
        {}, false, 0}, expr));
  } else {
    OZ(recursive_resolve(resolution.operand, expr));
  }
  const int64_t negations = resolution.negations;
  OZ (try_negate_const(expr, negations, remaining));
  for (int64_t i = 0; OB_SUCC(ret) && i < remaining; ++i) {
    ObOpRawExpr *negation = nullptr;
    OZ (ctx_.expr_factory_.create_raw_expr(T_OP_NEG, negation));
    OZ (negation->set_param_expr(expr));
    OX (expr = negation);
  }
  return ret;
}

int ObRawExprResolverImpl::process_logical_node(const peg::Node &node, bool is_root_expr, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSEArray<InListGroup, 4> groups;
  OZ(ObInListResolver::group_inlists(ctx_, is_root_expr, node, groups));
  if (OB_SUCC(ret) && groups.count() == 1) {
    const auto &group = groups.at(0);
    if (group.rows_.segments_.empty()) OZ(recursive_resolve(*group.node_, expr, is_root_expr));
    else OZ(process_in_or_not_in_node(*group.node_, is_root_expr, expr, &group.rows_));
  } else if (OB_SUCC(ret)) {
    ObOpRawExpr *logical = nullptr;
    OZ(ctx_.expr_factory_.create_raw_expr(binary_expression_type(peg::binary_operator(node)), logical));
    OZ(logical->init_param_exprs(groups.count()));
    for (int64_t i = 0; OB_SUCC(ret) && i < groups.count(); ++i) {
      ObRawExpr *operand = nullptr;
      const auto &group = groups.at(i);
      if (group.rows_.segments_.empty()) OZ(recursive_resolve(*group.node_, operand, is_root_expr));
      else OZ(process_in_or_not_in_node(*group.node_, is_root_expr, operand, &group.rows_));
      OZ(logical->add_param_expr(operand));
    }
    OZ(ObRawExprUtils::try_add_bool_expr(logical, ctx_.expr_factory_));
    OX(expr = logical);
  }
  return ret;
}

int ObRawExprResolverImpl::process_operator_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *left = nullptr;
  ObRawExpr *right = nullptr;
  ObOpRawExpr *operation = nullptr;
  bool happened = false;
  const auto *lowering = ctx_.parameterization_ != nullptr
      ? ctx_.parameterization_->numeric_lowering(&node) : nullptr;
  const ObItemType type = lowering != nullptr && lowering->operation == peg::ParameterLoweringKind::AddNegatedOperand
      ? T_OP_ADD : binary_expression_type(peg::binary_operator(node));
  const bool formalize = type == T_OP_DIV || type == T_OP_MINUS || type == T_OP_ADD;
  ctx_.formalize_const_int_prec_ = formalize;
  OZ (recursive_resolve(peg::binary_left(node), left));
  ctx_.formalize_const_int_prec_ = formalize;
  OZ (recursive_resolve(peg::binary_right(node), right));
  ctx_.formalize_const_int_prec_ = false;
  OZ (ctx_.expr_factory_.create_raw_expr(type, operation));
  OZ (operation->set_param_exprs(left, right));
  OX (expr = operation);
  OZ (convert_any_or_all_expr(expr, happened));
  if (OB_SUCC(ret) && type == T_OP_XOR) { OZ (ObRawExprUtils::try_add_bool_expr(operation, ctx_.expr_factory_)); }
  return ret;
}

int ObRawExprResolverImpl::convert_any_or_all_expr(ObRawExpr *&expr,
                                                   bool &happened)
{
  int ret = OB_SUCCESS;
  ObRawExpr *sub_expr1 = NULL;
  ObRawExpr *sub_expr2 = NULL;
  ObRawExpr *sub_expr2_child = NULL;
  ObOpRawExpr *op_expr = NULL;
  happened = false;
  if (OB_ISNULL(expr) || OB_ISNULL(ctx_.op_exprs_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_OP_EQ > expr->get_expr_type()
             || T_OP_NE < expr->get_expr_type()) {
    /*do nothing*/
  } else if (OB_UNLIKELY(2 != expr->get_param_count()) ||
             OB_ISNULL(sub_expr1 = expr->get_param_expr(0)) ||
             OB_ISNULL(sub_expr2 = expr->get_param_expr(1))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_ANY != sub_expr2->get_expr_type() &&
             T_ALL != sub_expr2->get_expr_type()) {
    /*do nothing*/
  } else if (OB_UNLIKELY(1 != sub_expr2->get_param_count()) ||
             OB_ISNULL(sub_expr2_child = sub_expr2->get_param_expr(0))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (T_OP_ROW != sub_expr2_child->get_expr_type()) {
    /*do nothing*/
  } else if (T_OP_EQ == expr->get_expr_type() &&
             T_ANY == sub_expr2->get_expr_type() &&
             OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_IN, op_expr))) {
  } else if (T_OP_NE == expr->get_expr_type() &&
             T_ALL == sub_expr2->get_expr_type() &&
             OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_NOT_IN, op_expr))) {
  } else if (OB_NOT_NULL(op_expr)) {
    if (OB_FAIL(op_expr->set_param_exprs(sub_expr1, sub_expr2_child))) {
    } else if (T_OP_ROW != sub_expr1->get_expr_type() &&
               T_OP_ROW != sub_expr2_child->get_expr_type() &&
               OB_FAIL(ctx_.op_exprs_->push_back(op_expr))) {
    } else {
      happened = true;
      expr = op_expr;
    }
  } else if (T_ANY == sub_expr2->get_expr_type() &&
             (T_OP_EQ < expr->get_expr_type() && expr->get_expr_type() <= T_OP_NE) &&
             OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_OR, op_expr))) {
  } else if (T_ALL == sub_expr2->get_expr_type() &&
             (T_OP_EQ <= expr->get_expr_type() && expr->get_expr_type() < T_OP_NE) &&
             OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_AND, op_expr))) {
  } else if (OB_ISNULL(op_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(op_expr->init_param_exprs(sub_expr2_child->get_param_count()))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < sub_expr2_child->get_param_count(); i++) {
      ObOpRawExpr *tmp_expr = NULL;
      if (OB_ISNULL(sub_expr2_child->get_param_expr(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if ((T_OP_EQ != expr->get_expr_type() && T_OP_NE != expr->get_expr_type()) &&
                 (T_OP_ROW == sub_expr2_child->get_param_expr(i)->get_expr_type() ||
                 T_OP_ROW == sub_expr1->get_expr_type())) {
        // Vector list comparison is only supported for equality and inequality operators.
        ret = OB_ERR_OPERATOR_CANNOT_BE_USED_WITH_LIST;
      } else if (T_OP_EQ == expr->get_expr_type() &&
                 OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_EQ, tmp_expr))) {
      } else if (T_OP_NE == expr->get_expr_type() &&
                 OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_NE, tmp_expr))) {
      } else if (T_OP_LT == expr->get_expr_type() &&
                 OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_LT, tmp_expr))) {
      } else if (T_OP_LE == expr->get_expr_type() &&
                 OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_LE, tmp_expr))) {
      } else if (T_OP_GT == expr->get_expr_type() &&
                 OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_GT, tmp_expr))) {
      } else if (T_OP_GE == expr->get_expr_type() &&
                 OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_GE, tmp_expr))) {
      } else if (OB_ISNULL(tmp_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(tmp_expr->set_param_exprs(sub_expr1,
                                                   sub_expr2_child->get_param_expr(i)))) {
      } else if (T_OP_ROW != sub_expr1->get_expr_type() &&
                 T_OP_ROW != sub_expr2_child->get_param_expr(i)->get_expr_type() &&
                 OB_FAIL(ctx_.op_exprs_->push_back(tmp_expr))) {
      } else if (sub_expr2_child->get_param_count() == 1) {//any/all has only one row, there is no need to create and/or
        op_expr = tmp_expr;
      } else if (OB_FAIL(op_expr->add_param_expr(tmp_expr))) {
      } else {/*do nothing*/}
    }
    if (OB_SUCC(ret)) {
      happened = true;
      expr = op_expr;
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_is_or_is_not_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *subject = nullptr;
  ObRawExpr *test = nullptr;
  ObOpRawExpr *operation = nullptr;
  OZ (recursive_resolve(peg::predicate_subject(node), subject));
  OZ (recursive_resolve(peg::is_test(node), test));
  OZ (ctx_.expr_factory_.create_raw_expr(peg::predicate_negated(node) ? T_OP_IS_NOT : T_OP_IS, operation));
  OZ (operation->set_param_exprs(subject, test));
  OX (expr = operation);
  return ret;
}

int ObRawExprResolverImpl::process_regexp_or_not_regexp_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObRawExpr *left = nullptr;
  ObRawExpr *right = nullptr;
  ObOpRawExpr *operation = nullptr;
  OZ (recursive_resolve(peg::binary_left(node), left));
  OZ (recursive_resolve(peg::binary_right(node), right));
  OZ (ctx_.expr_factory_.create_raw_expr(T_OP_REGEXP, operation));
  OZ (operation->set_param_exprs(left, right));
  OX (expr = operation);
  if (OB_SUCC(ret) && peg::binary_operator(node) == peg::BinaryOperator::NotRegexp) {
    ObOpRawExpr *negation = nullptr;
    OZ (ctx_.expr_factory_.create_raw_expr(T_OP_NOT, negation));
    OZ (negation->set_param_expr(expr));
    OX (expr = negation);
  }
  return ret;
}

int ObRawExprResolverImpl::process_between_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  const int64_t BTW_PARAM_NUM = 3;
  // Why BTW_PARAM_NUM + 1 ?
  // The content of the 4th raw expr is same to that of the 1st raw expr.
  // But the ptr addresses need to be different because our optimizer relys on it.
  ObRawExpr *btw_params[BTW_PARAM_NUM + 1];

  const peg::Node *operands[] = {&peg::predicate_subject(node), &peg::between_lower(node), &peg::between_upper(node)};
  const ObItemType type = peg::predicate_negated(node) ? T_OP_NOT_BTW : T_OP_BTW;
  {
    bool can_transform_in_mysql_mode = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < BTW_PARAM_NUM; ++i ) {
      if (OB_FAIL(recursive_resolve(*operands[i], btw_params[i]))) {
      } else if (OB_ISNULL(btw_params[i])) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(ctx_.session_info_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(btw_params[1]->extract_info())) {
      } else if (OB_FAIL(btw_params[2]->extract_info())) {
      } else if (btw_params[1]->has_flag(CNT_PL_UDF)
                 || btw_params[2]->has_flag(CNT_PL_UDF)) {
        // skip rewrite, relevant issue :
      } else if (btw_params[1]->is_const_expr()
                 && btw_params[2]->is_const_expr()) {
        // We will transform if the second and the third param are both const value or const expr
        // and the result meta types of them are same.
        // Why need const? Because 'resolve_columns' is called later than 'expr_resolver.resolve'.
        if (OB_FAIL(btw_params[1]->deduce_type(ctx_.session_info_))) {
        } else if (OB_FAIL(btw_params[2]->deduce_type(ctx_.session_info_))) {
        } else if (btw_params[1]->get_result_meta() == btw_params[2]->get_result_meta()) {
          can_transform_in_mysql_mode = true;
        }
      }
    }
    // The content of the 4th raw expr is same to that of the 1st raw expr.
    // But the ptr addresses need to be different because our optimizer relys on it.
    if (OB_SUCC(ret)) {
      if (!can_transform_in_mysql_mode) {
        // do nothing
      } else {
        if (OB_FAIL(recursive_resolve(peg::predicate_subject(node), btw_params[BTW_PARAM_NUM]))) {
        } else if (OB_ISNULL(btw_params[BTW_PARAM_NUM])) {
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (can_transform_in_mysql_mode) {
        // convert between expr to >= and <=
        if (OB_FAIL(transform_between_expr(btw_params, expr, T_OP_NOT_BTW == type))) {
        }
      } else {
        ObOpRawExpr *btw_expr = NULL;
        if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(type, btw_expr))) {
        } else if (OB_FAIL(btw_expr->init_param_exprs(BTW_PARAM_NUM))) {
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < BTW_PARAM_NUM; ++i) {
            if (OB_FAIL(btw_expr->add_param_expr(btw_params[i]))) {
            }
          }
        }
        expr = btw_expr;
      }
    }
  }

  return ret;
}

// Transform BETWEEN to >= and <= when supported by current result metadata.
// expr1 NOT BETWEEN expr2 AND expr3 ==>  NOT (expr1 >= expr2 AND expr1 <= expr3)
// expr1 BETWEEN expr2 AND expr3 ==> expr1 >= expr2 AND expr1 <= expr3
int ObRawExprResolverImpl::transform_between_expr(
    ObRawExpr **btw_params, ObRawExpr *&expr, const bool is_not_btw)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *ge_expr = NULL;
  ObOpRawExpr *le_expr = NULL;
  ObOpRawExpr *and_expr = NULL;
  if (OB_ISNULL(ctx_.session_info_) || OB_ISNULL(ctx_.op_exprs_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_GE, ge_expr))) {
  } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_LE, le_expr))) {
  } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_AND, and_expr))) {
  } else if (OB_ISNULL(ge_expr) || OB_ISNULL(le_expr) || OB_ISNULL(and_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(add_params_to_op_expr(btw_params[0], btw_params[1], ge_expr))) {
  } else if (OB_FAIL(add_params_to_op_expr(btw_params[3], btw_params[2], le_expr))) {
  } else if (OB_FAIL(and_expr->set_param_exprs(ge_expr, le_expr))) {
  }
  if (OB_SUCC(ret)) {
    if (is_not_btw) {
      // expr1 NOT BETWEEN expr2 AND expr3 ==>  NOT (expr1 >= expr2 AND expr1 <= expr3)
      ObOpRawExpr *not_expr = NULL;
      if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_NOT, not_expr))) {
      } else if (OB_ISNULL(not_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(not_expr->set_param_expr(and_expr))) {
      } else {
        expr = not_expr; // T_OP_NOT_BTW
      }
    } else {
      expr = and_expr; // T_OP_BTW
    }
  }

  return ret;
}

int ObRawExprResolverImpl::add_params_to_op_expr(
    ObRawExpr *op_param_1, ObRawExpr *op_param_2, ObOpRawExpr *op_expr)
{
  int ret = OB_SUCCESS;
  bool happened = false;
  ObRawExpr *expr = NULL;

  if (OB_ISNULL(op_param_1) || OB_ISNULL(op_param_2) || OB_ISNULL(op_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(expr = op_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(op_expr->set_param_exprs(op_param_1, op_param_2))) {
  } else if (OB_FAIL(convert_any_or_all_expr(expr, happened))) {
  }

  return ret;
}

int ObRawExprResolverImpl::process_like_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *t_expr = NULL;
  const ObItemType type = binary_expression_type(peg::binary_operator(node));
  if (peg::like_has_escape(node)) {
    const peg::Node &escape_node = peg::like_escape(node);
    if (escape_node.kind() == peg::NodeKind::Parameter) {
      const int64_t parameter_index = peg::placeholder_index(escape_node);
      // If it has gone through the fast parser, then escape will be replaced with ?, need to get the real value from the param list
      if (OB_ISNULL(ctx_.param_list_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (ctx_.param_list_->empty() && nullptr != ctx_.secondary_namespace_) {
        const pl::ObPLSymbolTable* symbol_table = NULL;
        const pl::ObPLVar* var = NULL;
        CK (OB_NOT_NULL(symbol_table = ctx_.secondary_namespace_->get_symbol_table()));
        CK (OB_NOT_NULL(var = symbol_table->get_symbol(parameter_index)));
        if (0 == var->get_name().case_compare(pl::ObPLResolver::ANONYMOUS_ARG) &&
            NULL != var->get_pl_data_type().get_data_type() &&
            ObNullType == var->get_pl_data_type().get_data_type()->get_obj_type()) {
          ret = OB_ERR_INVALID_ESCAPE_CHAR_LENGTH;
        }
      } else if (parameter_index < 0 || parameter_index >= ctx_.param_list_->count()) {
        if (OB_NOT_NULL(ctx_.session_info_) && ctx_.session_info_->is_varparams_sql_prepare()) {
          // skip check question mark about escape node in prepare statement
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("index is out of range", K(parameter_index), K(ctx_.param_list_->count()));
        }
        // NULL ESCAPE is accepted.
      } else if (ctx_.param_list_->at(parameter_index).is_null()) {
        // escape null is legal in mysql
      } else {
        // Note: currently when like node has escape, it's childs will not be parameterized and
        // therefore this line is meaningless. However when you wanna to open this, this line may
        // be somewhat useful.
        //escape = ctx_.param_list_->at(parameter_index).get_string()[0];
      }
    }
  } else {
    // do nothing
  }

  if (OB_FAIL(ret)){
  } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(type, t_expr))) {
  } else if (OB_ISNULL(t_expr)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObRawExpr *subject = nullptr, *pattern = nullptr;
    OZ(SMART_CALL(recursive_resolve(peg::binary_left(node), subject)));
    OZ(SMART_CALL(recursive_resolve(peg::binary_right(node), pattern)));
    OZ(t_expr->set_param_exprs(subject, pattern));
    if (peg::like_has_escape(node)) {
      ObRawExpr *escape = nullptr;
      OZ(SMART_CALL(recursive_resolve(peg::like_escape(node), escape)));
      OZ(t_expr->extend_param_exprs(3));
      OZ(t_expr->add_param_expr(escape));
    }
    // If there are two child_, it means the escape parameter for like or not like was not explicitly given, here we set the default value '\\'
    if (OB_SUCC(ret) && !peg::like_has_escape(node)) {
      ObRawExpr *escape_expr = NULL;
      ObString escape_text = ObString::make_string("\\");

      // when sql_mode = 'NO_BACKSLASH_ESCAPES', Remove the default value '\'.
      // otherwise, it is not possible to determine whether ESCAPE is explicitly specified.
      bool is_no_backslash_escapes = false;
      IS_NO_BACKSLASH_ESCAPES(ctx_.session_info_->get_sql_mode(), is_no_backslash_escapes);
      if (is_no_backslash_escapes) {
        escape_text = ObString::make_string("");
      }
      /*
      bugfix:https://work.aone.alibaba-inc.com/issue/36691548
      in NO_BACKSLASH_ESCAPES mode, 'like BINARY xxx' stmt should also set the escapes as null, instead of '\'
      */
      bool no_escapes = false;
      const auto &pattern_syntax = peg::binary_right(node);
      if (pattern_syntax.kind() == peg::NodeKind::Cast) {
        const auto target = peg::type_syntax(peg::cast_target(pattern_syntax));
        no_escapes = is_no_backslash_escapes && target.name == peg::TypeName::VarBinary;
      }
      if (OB_FAIL(build_implicit_literal({peg::LiteralKind::String, 0, escape_text, {}, false, 0}, escape_expr))) {
      } else if (OB_FAIL(t_expr->extend_param_exprs(3))) {
      } else if (OB_FAIL(t_expr->add_param_expr(escape_expr))) {
      } else if (no_escapes) {
        // The LIKE implementation must contain an escape expression, so rewrite
        // LIKE without escape.
        // c1 like '%x\x%' --> c1 like replace('%x\x%', '\','\\') escape '\' -> c1 like '%x\\x%' escape '\'
        ObRawExpr *replace_expr1 = NULL;
        ObRawExpr *replace_expr2 = NULL;
        ObSysFunRawExpr *replace_expr = NULL;
        if (3 != t_expr->get_param_count()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("like expr with escape must contain 3 arguments");
        } else {
          if (OB_FAIL(build_implicit_literal({peg::LiteralKind::String, 0, escape_text, {}, false, 0}, replace_expr1))) {
          } else if (OB_FAIL(build_implicit_literal({peg::LiteralKind::String, 0, ObString::make_string("\\\\"), {}, false, 0}, replace_expr2))) {
          } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_FUN_SYS, replace_expr))) {
          } else if (OB_ISNULL(replace_expr)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(replace_expr->set_param_exprs(t_expr->get_param_expr(1),
                                                           replace_expr1,
                                                           replace_expr2))) {
          } else {
            replace_expr->set_func_name(N_REPLACE);
            if (OB_FAIL(t_expr->replace_param_expr(1, replace_expr))) {
            }
          }
        }
      } else {
        //do nothing
      }
    }
    // convert `A not like B' to `NOT A like B'
    if (OB_SUCC(ret)) {
      ObOpRawExpr *not_expr = NULL;
      if (T_OP_NOT_LIKE != type) {
        t_expr->set_expr_type(type);
      } else {
        t_expr->set_expr_type(T_OP_LIKE);
        if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_OP_NOT, not_expr))) {
        } else if (OB_ISNULL(not_expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(not_expr->set_param_expr(t_expr))) {
        } else {
          t_expr = not_expr;
        }
      }
      expr = t_expr;
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_in_or_not_in_node(const peg::Node &node,
                                                     const bool is_root_expr,
                                                     ObRawExpr *&expr,
                                                     const InListRows *merged_rows)
{
  int ret = OB_SUCCESS;
  ObOpRawExpr *in_expr = NULL;
  ObRawExpr *sub_expr1 = NULL;
  ObRawExpr *sub_expr2 = NULL;
  const ObItemType type = peg::predicate_negated(node) ? T_OP_NOT_IN : T_OP_IN;
  if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_INVALID, in_expr))) {
  } else if (OB_FAIL(SMART_CALL(recursive_resolve(peg::predicate_subject(node), sub_expr1)))) {
  } else if (OB_FAIL(resolve_right_branch_of_in_op(node, type, sub_expr1,
                                                   is_root_expr, sub_expr2, merged_rows))) {
  } else if (OB_ISNULL(sub_expr1) || OB_ISNULL(sub_expr2)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObItemType param_type2 = sub_expr2->get_expr_type();
    if (T_REF_QUERY == param_type2) {
      ObQueryRefRawExpr *sub_ref = static_cast<ObQueryRefRawExpr*>(sub_expr2);
      sub_ref->set_is_set(true);
      //rewrite expr in sub-query as expr = ANY(sub-query)
      //rewrite expr not in sub-query as expr != ALL(sub-query)
      ObOpRawExpr *u_expr = NULL;
      if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_INVALID, u_expr))) {
      } else if (OB_ISNULL(in_expr) || OB_ISNULL(u_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObItemType u_expr_type = (T_OP_IN == type) ? T_ANY : T_ALL;
        ObItemType in_expr_type = (T_OP_IN == type) ? T_OP_EQ : T_OP_NE;
        u_expr->set_expr_type(u_expr_type);
        if (OB_FAIL(u_expr->set_param_expr(sub_expr2))) {
        } else if (OB_FAIL(in_expr->set_param_exprs(sub_expr1, u_expr))) {
        } else {
          in_expr->set_expr_type(in_expr_type);
          expr = in_expr;
        }
      }
    } else if (T_OP_ROW == param_type2) {
      ObOpRawExpr *row_expr = static_cast<ObOpRawExpr *>(sub_expr2);
      if (OB_ISNULL(row_expr)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (1 == row_expr->get_param_count()) {
        ObRawExpr *param = row_expr->get_param_expr(0);
        if (OB_FAIL(in_expr->set_param_exprs(sub_expr1, param))) {
        } else {
          ObItemType expr_type = (T_OP_IN == type) ? T_OP_EQ : T_OP_NE;
          in_expr->set_expr_type(expr_type);
          expr = in_expr;
        }
      } else {
        if (OB_FAIL(in_expr->set_param_exprs(sub_expr1, sub_expr2))) {
        } else {
          in_expr->set_expr_type(type);
          expr = in_expr;
        }
      }
    } else {
      if (OB_FAIL(in_expr->set_param_exprs(sub_expr1, sub_expr2))) {
      } else {
        ObItemType expr_type = (T_OP_IN == type) ? T_OP_EQ : T_OP_NE;
        in_expr->set_expr_type(expr_type);
        expr = in_expr;
      }
    }
  }
  return ret;
}

int ObRawExprResolverImpl::resolve_right_branch_of_in_op(const peg::Node &node,
                                                         const ObItemType op_type,
                                                         const ObRawExpr *left_expr,
                                                         const bool is_root_condition,
                                                         ObRawExpr *&right_expr,
                                                         const InListRows *merged_rows)
{
  int ret = OB_SUCCESS;
  bool is_enable_rewrite = false;
  bool is_question_mark = true;
  InListRows single_list;
  const InListRows &rows = merged_rows != nullptr ? *merged_rows : single_list;
  if (OB_ISNULL(left_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (peg::membership_source(node) == peg::MembershipSource::Query) {
    OZ (SMART_CALL(recursive_resolve(peg::membership_query(node), right_expr)));
  } else {
    if (merged_rows == nullptr) {
      OZ (single_list.segments_.push_back(&node));
    }
    OZ (ObInListResolver::check_inlist_rewrite_enable(rows,
        op_type, *left_expr, ctx_.current_scope_, is_root_condition,
        ctx_.is_need_print_, ctx_.secondary_namespace_ != nullptr,
        ctx_.session_info_, ctx_.param_list_, ctx_.parameterization_, ctx_.stmt_,
        ctx_.expr_factory_.get_allocator(), is_question_mark, is_enable_rewrite));
    if (OB_SUCC(ret) && is_enable_rewrite) {
      ObQueryRefRawExpr *sub_query_expr = nullptr;
      OZ (ctx_.expr_factory_.create_raw_expr(T_REF_QUERY, sub_query_expr));
      CK (OB_NOT_NULL(sub_query_expr), OB_NOT_NULL(ctx_.inlist_infos_));
      if (OB_SUCC(ret)) {
        ObInListInfo info;
        info.in_list_expr_ = sub_query_expr;
        info.column_cnt_ = left_expr->get_expr_type() == T_OP_ROW ? left_expr->get_param_count() : 1;
        info.row_cnt_ = rows.row_count();
        info.is_question_mark_ = is_question_mark;
        OZ (info.rows_.segments_.assign(rows.segments_));
        OZ (ctx_.inlist_infos_->push_back(info));
        OX (right_expr = sub_query_expr);
      }
    } else if (OB_SUCC(ret)) {
      ObOpRawExpr *row = nullptr;
      OZ (ctx_.expr_factory_.create_raw_expr(T_OP_ROW, row));
      OZ (row->init_param_exprs(rows.row_count()));
      for (int64_t segment = 0; OB_SUCC(ret) && segment < rows.segments_.count(); ++segment) {
        for (const auto &value : peg::membership_expressions(*rows.segments_.at(segment))) {
          ObRawExpr *element = nullptr;
          OZ(SMART_CALL(recursive_resolve(value, element)));
          OZ(row->add_param_expr(element));
        }
      }
      OX (right_expr = row);
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_case_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObCaseOpRawExpr *result = nullptr;
  OZ(ctx_.expr_factory_.create_raw_expr(peg::case_has_operand(node) ? T_OP_ARG_CASE : T_OP_CASE, result));
  if (OB_SUCC(ret) && peg::case_has_operand(node)) {
    ObRawExpr *operand = nullptr;
    OZ(SMART_CALL(recursive_resolve(peg::case_operand(node), operand)));
    OX(result->set_arg_param_expr(operand));
  }
  for (const auto &arm : peg::case_arms(node)) {
    ObRawExpr *condition = nullptr;
    ObRawExpr *value = nullptr;
    OZ(SMART_CALL(recursive_resolve(peg::case_condition(arm), condition)));
    OZ(SMART_CALL(recursive_resolve(peg::case_result(arm), value)));
    OZ(result->add_when_param_expr(condition));
    OZ(result->add_then_param_expr(value));
  }
  ObRawExpr *otherwise = nullptr;
  if (peg::case_has_else(node)) {
    OZ(SMART_CALL(recursive_resolve(peg::case_else(node), otherwise)));
  } else {
    OZ(build_implicit_literal({peg::LiteralKind::Null, 0, {}, {}, false, 0}, otherwise));
  }
  OX(result->set_default_param_expr(otherwise));
  OZ(ObRawExprUtils::try_add_bool_expr(result, ctx_.expr_factory_));
  OX(expr = result);
  return ret;
}


int ObRawExprResolverImpl::process_sub_query_node(const peg::Node &node, ObRawExpr *&expr)
{
  if (lambda_parameters_ != nullptr) {
    const rust::Slice<const uint8_t> sql(reinterpret_cast<const uint8_t *>(ctx_.syntax_source_.ptr()),
                                        ctx_.syntax_source_.length());
    for (const auto &symbol : peg::embedded_sql_symbols(node, sql)) {
      if (symbol.node.kind() != peg::NodeKind::Column) continue;
      const ObString name(syntax_text(symbol.name));
      for (int64_t i = 0; i < lambda_parameters_->count(); ++i) {
        if (lambda_parameters_->at(i) == name) {
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "expr in lambda function");
          return OB_NOT_SUPPORTED;
        }
      }
    }
  }
  int ret = OB_SUCCESS;
  ObQueryRefRawExpr *reference = nullptr;
  CK (OB_NOT_NULL(ctx_.sub_query_info_), OB_NOT_NULL(ctx_.session_info_));
  OZ (ctx_.expr_factory_.create_raw_expr(T_REF_QUERY, reference));
  if (OB_SUCC(ret)) {
    ObSubQueryInfo info;
    info.sub_query_ = &node;
    info.ref_expr_ = reference;
    info.parents_expr_info_ = ctx_.parents_expr_info_;
    OZ (ctx_.sub_query_info_->push_back(info));
    OX (expr = reference);
  }
  return ret;
}

int ObRawExprResolverImpl::process_agg_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObAggFunRawExpr *aggregate = nullptr;
  const auto type = callable_expression_type(node.kind());
  const auto arguments = peg::call_arguments(node);
  const auto quantifier = peg::aggregate_quantifier(node);
  const bool distinct = quantifier == peg::AggregateQuantifier::Distinct;
  const bool add_flag = !ctx_.parents_expr_info_.has_member(IS_AGG);
  size_t min_arguments = 1;
  size_t max_arguments = 1;
  switch (type) {
    case T_FUN_COUNT:
      if (distinct) max_arguments = arguments.size();
      break;
    case T_FUN_APPROX_COUNT_DISTINCT:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS:
    case T_FUN_GROUP_CONCAT:
      max_arguments = arguments.size();
      break;
    case T_FUN_TOP_FRE_HIST:
      min_arguments = max_arguments = 4;
      break;
    case T_FUN_HYBRID_HIST:
    case T_FUN_JSON_OBJECTAGG:
    case T_FUN_GROUP_PERCENTILE_CONT:
      min_arguments = max_arguments = 2;
      break;
    case T_FUN_SYS_ST_ASMVT:
      max_arguments = 5;
      break;
    default:
      break;
  }
  const auto name = peg::call_name(node);
  if (arguments.size() < min_arguments || arguments.size() > max_arguments) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(name.size()), name.data());
  } else if ((type == T_FUN_TOP_FRE_HIST
              && (arguments[0].kind() != peg::NodeKind::Number
                  || arguments[2].kind() != peg::NodeKind::Int))
      || (type == T_FUN_HYBRID_HIST && arguments[1].kind() != peg::NodeKind::Int)
      || (type == T_FUN_SYS_ST_ASMVT && arguments[0].kind() != peg::NodeKind::Column)) {
    ret = OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT;
    LOG_USER_ERROR(OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT, int(name.size()), name.data());
  }
  if (OB_SUCC(ret) && quantifier != peg::AggregateQuantifier::Default
      && (type == T_FUN_APPROX_COUNT_DISTINCT || type == T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS
          || type == T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE || type == T_FUN_TOP_FRE_HIST
          || type == T_FUN_HYBRID_HIST || type == T_FUN_SUM_OPNSIZE || type == T_FUN_JSON_OBJECTAGG
          || type == T_FUN_GROUPING || type == T_FUN_SYS_ST_ASMVT)) {
    ret = OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT;
    LOG_USER_ERROR(OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT, int(name.size()), name.data());
  }
  for (const auto &argument : arguments) {
    if (OB_SUCC(ret) && argument.kind() == peg::NodeKind::Star
        && (type != T_FUN_COUNT || distinct || arguments.size() != 1)) {
      ret = OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT;
      LOG_USER_ERROR(OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT, int(name.size()), name.data());
    }
  }
  OZ(ctx_.expr_factory_.create_raw_expr(type, aggregate));
  OZ(ctx_.aggr_exprs_->push_back(aggregate));
  if (add_flag) OZ(ctx_.parents_expr_info_.add_member(IS_AGG));
  if (OB_SUCC(ret) && distinct && (type == T_FUN_VAR_POP || type == T_FUN_VAR_SAMP
      || type == T_FUN_STDDEV_POP || type == T_FUN_STDDEV_SAMP || type == T_FUN_JSON_ARRAYAGG
      || type == T_FUN_STDDEV || type == T_FUN_VARIANCE || type == T_FUN_SYS_BIT_AND
      || type == T_FUN_SYS_BIT_OR || type == T_FUN_SYS_BIT_XOR)) {
    ret = OB_DISTINCT_NOT_ALLOWED;
  }
  for (size_t i = 0; OB_SUCC(ret) && i < arguments.size(); ++i) {
    if (type == T_FUN_COUNT && arguments[i].kind() == peg::NodeKind::Star) continue;
    ObRawExpr *value = nullptr;
    if (type == T_FUN_SYS_ST_ASMVT && i == 0) {
      OZ(process_column_ref_node(arguments[i], value, true));
    } else {
      OZ(SMART_CALL(recursive_resolve(arguments[i], value)));
    }
    if (OB_SUCC(ret) && type == T_FUN_TOP_FRE_HIST) {
      if ((i == 0 || i == 2 || i == 3) && !value->is_const_raw_expr()) {
        ret = OB_INVALID_ARGUMENT;
      } else if (i == 0) {
        ObConstRawExpr *one = nullptr;
        ObRawExpr *quotient = nullptr;
        ObSysFunRawExpr *ceiling = nullptr;
        OZ(ObRawExprUtils::build_const_int_expr(ctx_.expr_factory_, ObIntType, 1, one));
        OZ(ObRawExprUtils::build_common_binary_op_expr(ctx_.expr_factory_, T_OP_DIV, one, value, quotient));
        OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_SYS_CEIL, ceiling));
        OZ(ceiling->set_param_expr(quotient));
        OX(value = ceiling);
      }
    }
    if (type != T_FUN_GROUP_PERCENTILE_CONT || i != 0) {
      OZ(aggregate->add_real_param_expr(value));
    }
    if (OB_SUCC(ret) && i == 0 && (type == T_FUN_HYBRID_HIST
        || type == T_FUN_JSON_OBJECTAGG || type == T_FUN_GROUP_PERCENTILE_CONT)) {
      OrderItem order;
      order.expr_ = value;
      order.order_type_ = NULLS_FIRST_ASC;
      OZ(aggregate->add_order_item(order));
    }
  }
  if (OB_SUCC(ret) && type == T_FUN_GROUP_PERCENTILE_CONT && distinct) {
    ret = OB_DISTINCT_NOT_ALLOWED;
  }
  if (OB_SUCC(ret) && peg::aggregate_has_separator(node)) {
    ObRawExpr *separator = nullptr;
    OZ(SMART_CALL(recursive_resolve(peg::aggregate_separator(node), separator)));
    if (OB_SUCC(ret) && !separator->is_const_raw_expr()) ret = OB_ERR_UNEXPECTED;
    OX(aggregate->set_separator_param_expr(separator));
  }
  for (const auto &sort : peg::aggregate_order(node)) {
    OrderItem order;
    const auto &key = peg::sort_expression(sort);
    OZ(ObResolverUtils::set_direction_by_mode(sort, order));
    if (type == T_FUN_GROUP_CONCAT && key.kind() == peg::NodeKind::Int) {
      const int64_t position = peg::integer_literal_value(key);
      if (position <= 0 || position > aggregate->get_real_param_count()) ret = OB_ERR_BAD_FIELD_ERROR;
      else order.expr_ = aggregate->get_real_param_exprs().at(position - 1);
    } else if (type == T_FUN_GROUP_CONCAT && key.kind() == peg::NodeKind::Parameter) {
      ret = OB_ERR_PARSE_SQL;
    } else {
      OZ(SMART_CALL(recursive_resolve(key, order.expr_)));
    }
    OZ(not_row_check(order.expr_));
    OZ(aggregate->add_order_item(order));
  }
  OX(aggregate->set_param_distinct(distinct));
  OZ(aggregate->get_relation_ids().add_member(0));
  if (add_flag) {
    const int cleanup_ret = ctx_.parents_expr_info_.del_member(IS_AGG);
    if (OB_SUCC(ret)) ret = cleanup_ret;
  }
  OX(expr = aggregate);
  OZ(param_not_row_check(expr));
  return ret;
}

int ObRawExprResolverImpl::process_timestamp_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  OZ(check_scalar_quantifier(node));
  ObSysFunRawExpr *function = nullptr;
  const auto arguments = peg::call_arguments(node);
  const auto name = peg::call_name(node);
  const bool precision_allowed = node.kind() != peg::NodeKind::CurrentDate
      && node.kind() != peg::NodeKind::UtcDate && node.kind() != peg::NodeKind::CurrentUser;
  uint64_t scale = 0;
  if (OB_FAIL(ret)) {
  } else if (arguments.size() > (precision_allowed ? 1 : 0)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(name.size()), name.data());
  } else if (!arguments.empty() && arguments[0].kind() != peg::NodeKind::Int
      && arguments[0].kind() != peg::NodeKind::Uint) {
    ret = OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT;
    LOG_USER_ERROR(OB_ERR_WRONG_PARAMETERS_TO_NATIVE_FCT, int(name.size()), name.data());
  } else if (!arguments.empty()) {
    scale = peg::integer_literal_value(arguments[0]);
  }
  if (OB_FAIL(ret)) {
  } else if (scale > OB_MAX_DATETIME_PRECISION) {
    ret = OB_ERR_TOO_BIG_PRECISION;
    _LOG_USER_MSG(USER_ERROR, OB_ERR_TOO_BIG_PRECISION,
        "Too big precision %" PRIu64 " specified for column '%.*s'. Maximum is %ld.",
        scale, static_cast<int>(name.size()), name.data(), OB_MAX_DATETIME_PRECISION);
  } else {
    OZ(ctx_.expr_factory_.create_raw_expr(callable_expression_type(node.kind()), function));
    OX(function->set_scale(static_cast<int16_t>(scale)));
    if (OB_SUCC(ret)) {
      switch (node.kind()) {
        case peg::NodeKind::CurrentTimestamp: function->set_func_name(N_CUR_TIMESTAMP); break;
        case peg::NodeKind::CurrentTime: function->set_func_name(N_CUR_TIME); break;
        case peg::NodeKind::CurrentDate: function->set_func_name(N_CUR_DATE); break;
        case peg::NodeKind::UtcTimestamp: function->set_func_name(N_UTC_TIMESTAMP); break;
        case peg::NodeKind::UtcTime: function->set_func_name(N_UTC_TIME); break;
        case peg::NodeKind::UtcDate: function->set_func_name(N_UTC_DATE); break;
        case peg::NodeKind::Sysdate: function->set_func_name(N_SYSDATE); break;
        case peg::NodeKind::CurrentUser: function->set_func_name(N_CURRENT_USER); break;
        default: ret = OB_ERR_UNEXPECTED; break;
      }
    }
    OX(expr = function);
  }
  return ret;
}


int ObRawExprResolverImpl::process_collation_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  {
    ObConstRawExpr *c_expr = NULL;
    const auto node_text = node.text();
    ObString collation(syntax_text(node_text));
    ObCollationType collation_type = CS_TYPE_INVALID;
    if (0 == collation.case_compare("utf8mb4_name_case")) {
      if (OB_ORIGIN_AND_SENSITIVE == ctx_.case_mode_) {
        collation_type = CS_TYPE_UTF8MB4_BIN;
      } else if (OB_ORIGIN_AND_INSENSITIVE == ctx_.case_mode_ ||
                 OB_LOWERCASE_AND_INSENSITIVE == ctx_.case_mode_) {
        collation_type = CS_TYPE_UTF8MB4_GENERAL_CI;
      }
    } else {
      collation_type = ObCharset::collation_type(collation);
    }
    if (CS_TYPE_INVALID == collation_type) {
      ret = OB_ERR_UNKNOWN_COLLATION;
      LOG_USER_ERROR(OB_ERR_UNKNOWN_COLLATION, int(node_text.size()), syntax_text(node_text).ptr());
    } else if (OB_FAIL(ctx_.expr_factory_.create_raw_expr(T_INT, c_expr))) {
    } else if (OB_ISNULL(c_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObObj val;
      val.set_int(static_cast<int64_t>(collation_type));
      val.set_collation_type(collation_type);
      val.set_collation_level(CS_LEVEL_IGNORABLE);
      c_expr->set_value(val);
      expr = c_expr;
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_if_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  OZ(check_scalar_quantifier(node));
  const auto arguments = peg::call_arguments(node);
  ObCaseOpRawExpr *result = nullptr;
  ObRawExpr *condition = nullptr, *yes = nullptr, *no = nullptr;
  if (OB_FAIL(ret)) {
  } else if (arguments.size() != 3) {
    ret = OB_ERR_PARAM_SIZE;
    const auto name = peg::call_name(node);
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(name.size()), name.data());
  } else {
    OZ(ctx_.expr_factory_.create_raw_expr(T_OP_CASE, result));
    OZ(SMART_CALL(recursive_resolve(arguments[0], condition)));
    OZ(SMART_CALL(recursive_resolve(arguments[1], yes)));
    OZ(SMART_CALL(recursive_resolve(arguments[2], no)));
    OZ(result->add_when_param_expr(condition));
    OZ(result->add_then_param_expr(yes));
    OX(result->set_default_param_expr(no));
    OZ(ObRawExprUtils::try_add_bool_expr(result, ctx_.expr_factory_));
    OX(expr = result);
  }
  return ret;
}


int ObRawExprResolverImpl::process_isnull_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  OZ(check_scalar_quantifier(node));
  const auto arguments = peg::call_arguments(node);
  ObRawExpr *value = nullptr;
  if (OB_FAIL(ret)) {
  } else if (arguments.size() != 1) {
    ret = OB_ERR_PARAM_SIZE;
    const auto name = peg::call_name(node);
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(name.size()), name.data());
  } else {
    OZ(SMART_CALL(recursive_resolve(arguments[0], value)));
    OZ(ObRawExprUtils::build_is_not_null_expr(ctx_.expr_factory_, value, false, expr));
  }
  return ret;
}


int ObRawExprResolverImpl::process_call_param_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObCallParamRawExpr *result = nullptr;
  ObRawExpr *value = nullptr;
  const auto &identifier = peg::named_argument_name(node);
  if (identifier.kind() != peg::NodeKind::Ident) {
    ret = OB_ERR_CALL_WRONG_ARG;
  }
  ObString name;
  const auto identifier_text = identifier.text();
  OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(identifier_text), name));
  OZ(ctx_.expr_factory_.create_raw_expr(T_SP_CPARAM, result));
  OZ(SMART_CALL(recursive_resolve(peg::named_argument_value(node), value)));
  OX(result->set_name(name));
  OX(result->set_expr(value));
  OX(expr = result);
  return ret;
}


int ObRawExprResolverImpl::process_sqlerrm_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObPLSQLCodeSQLErrmRawExpr *function = nullptr;
  const auto arguments = peg::call_arguments(node);
  OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_PL_SQLCODE_SQLERRM, function));
  OX(function->set_func_name(N_PL_GET_SQLCODE_SQLERRM));
  OX(function->set_is_sqlcode(false));
  if (arguments.size() > 1) ret = OB_INVALID_ARGUMENT_NUM;
  if (OB_SUCC(ret) && !arguments.empty()) {
    ObRawExpr *value = nullptr;
    OZ(SMART_CALL(recursive_resolve(arguments[0], value)));
    OZ(function->set_param_expr(value));
  }
  OX(expr = function);
  return ret;
}


int ObRawExprResolverImpl::process_sqlcode_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObPLSQLCodeSQLErrmRawExpr *function = nullptr;
  if (!peg::call_arguments(node).empty()) ret = OB_INVALID_ARGUMENT_NUM;
  OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_PL_SQLCODE_SQLERRM, function));
  OX(function->set_func_name(N_PL_GET_SQLCODE_SQLERRM));
  OX(function->set_is_sqlcode(true));
  OX(expr = function);
  return ret;
}

int ObRawExprResolverImpl::check_first_node(const peg::Node &node)
{
  int ret = OB_SUCCESS;
  if (node.kind() == peg::NodeKind::Star || (node.kind() == peg::NodeKind::Column && peg::name_is_wildcard(node))) {
    ret = OB_ERR_INVALID_COLUMN_SPE;
  } else if (node.kind() == peg::NodeKind::ValueVector || node.kind() == peg::NodeKind::ExprList) {
    for (const auto &element : peg::row_elements(node)) OZ(SMART_CALL(check_first_node(element)));
  }
  return ret;
}


int ObRawExprResolverImpl::process_json_query_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  const bool array_cast = node.kind() == peg::NodeKind::Cast;
  peg::JsonOptions options{};
  const peg::Node *document_node = nullptr, *path_node = nullptr;
  const auto &target_node = array_cast ? peg::cast_target(node) : peg::json_returning(node);
  if (!array_cast) {
    options = peg::json_options(node);
    document_node = &peg::json_document(node);
    path_node = &peg::json_path(node);
  } else {
    options.asis = true;
    options.scalars = peg::JsonScalars::Unspecified;
    options.wrapper = peg::JsonWrapper::WithoutArray;
    options.on_error = peg::JsonResponse::Error;
    options.on_empty = peg::JsonResponse::Null;
    options.on_mismatch = peg::JsonResponse::Null;
    const peg::Node *operand = &peg::cast_argument(node);
    const auto unwrap = [](const peg::Node *value) {
      while (value->kind() == peg::NodeKind::Parenthesized) value = &peg::parenthesized_expression(*value);
      return value;
    };
    operand = unwrap(operand);
    const auto named_call = [](const peg::Node &value, const char *name, size_t count) {
      return value.kind() == peg::NodeKind::Function && peg::call_qualifiers(value).empty()
          && ObString(peg::call_name(value).size(), peg::call_name(value).data()).case_compare(name) == 0
          && peg::call_arguments(value).size() == count;
    };
    const bool unquoted = named_call(*operand, "json_unquote", 1);
    if (unquoted) {
      OZ(check_scalar_quantifier(*operand));
      operand = unwrap(&peg::call_arguments(*operand)[0]);
    }
    if (operand->kind() == peg::NodeKind::Column && !unquoted) {
      document_node = operand;
    } else if (operand->kind() == peg::NodeKind::JsonValue) {
      document_node = &peg::json_document(*operand);
      path_node = &peg::json_path(*operand);
    } else if (named_call(*operand, "json_extract", 2)) {
      OZ(check_scalar_quantifier(*operand));
      const auto arguments = peg::call_arguments(*operand);
      document_node = &arguments[0];
      path_node = &arguments[1];
    } else {
      ret = OB_INVALID_ARGUMENT;
      _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "CAST ARRAY requires a JSON column or extracted path");
    }
    if (OB_FAIL(ret)) return ret;
  }
  ObRawExprResType target;
  OZ(ObResolverUtils::resolve_cast_type(target_node, source_text(target_node), target));
  ObString path;
  if (path_node != nullptr && peg::is_literal(*path_node)) {
    const auto literal = peg::expression_literal(*path_node);
    path.assign_ptr(syntax_text(literal.text).ptr(), static_cast<int64_t>(literal.text.size()));
  }
  ObJsonPath json_path(path, &ctx_.expr_factory_.get_allocator());
  if (OB_SUCC(ret) && !path.empty() && OB_FAIL(json_path.parse_path())) {
    ret = OB_ERR_JSON_PATH_EXPRESSION_SYNTAX_ERROR;
    LOG_USER_ERROR(OB_ERR_JSON_PATH_EXPRESSION_SYNTAX_ERROR, path.length(), path.ptr());
  }
  OZ(cast_accuracy_check(target, source_text(*document_node)));
  if (OB_SUCC(ret) && target.get_type() == ObJsonType) {
    bool inferred = false;
    ObRawExprResType inferred_type;
    OZ(infer_json_returning_type(ctx_.expr_factory_.get_allocator(), path, target, true, inferred_type, inferred));
    if (OB_SUCC(ret) && inferred) target = inferred_type;
  }
  if (OB_SUCC(ret) && !options.asis && target.get_type() != ObVarcharType
      && target.get_type() != ObLongTextType && target.get_type() != ObJsonType) {
    ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
    LOG_USER_ERROR(OB_ERR_INVALID_DATA_TYPE_RETURNING);
  }
  OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_SYS_JSON_QUERY, function));
  OX(function->set_func_name(ObString::make_string("json_query")));
  OZ(function->init_param_exprs(13));
  ObRawExpr *document = nullptr, *path_expr = nullptr, *returning = nullptr;
  OZ(check_first_node(*document_node));
  OZ(SMART_CALL(recursive_resolve(*document_node, document)));
  if (path_node != nullptr) {
    OZ(SMART_CALL(recursive_resolve(*path_node, path_expr)));
  } else {
    OZ(build_implicit_literal({peg::LiteralKind::String, 0, {}, {}, false, 0}, path_expr));
  }
  OZ(build_cast_type(target_node, target, returning));
  OZ(function->add_param_expr(document));
  OZ(function->add_param_expr(path_expr));
  OZ(function->add_param_expr(returning));
  const auto response = [](peg::JsonResponse value) -> uint64_t {
    switch (value) {
      case peg::JsonResponse::Error: return 0;
      case peg::JsonResponse::Null: return 1;
      case peg::JsonResponse::Empty: return 2;
      case peg::JsonResponse::EmptyArray: return 3;
      case peg::JsonResponse::EmptyObject: return 4;
      default: return 5;
    }
  };
  uint64_t wrapper = 8;
  switch (options.wrapper) {
    case peg::JsonWrapper::Without: wrapper = 0; break;
    case peg::JsonWrapper::WithoutArray: wrapper = 1; break;
    case peg::JsonWrapper::With: wrapper = 2; break;
    case peg::JsonWrapper::WithArray: wrapper = 3; break;
    case peg::JsonWrapper::Unconditional: wrapper = 4; break;
    case peg::JsonWrapper::Conditional: wrapper = 5; break;
    case peg::JsonWrapper::UnconditionalArray: wrapper = 6; break;
    case peg::JsonWrapper::ConditionalArray: wrapper = 7; break;
    default: break;
  }
  const uint64_t configuration[] = {options.truncate,
      options.scalars == peg::JsonScalars::Allow ? 0u : options.scalars == peg::JsonScalars::Disallow ? 1u : 2u,
      options.pretty, options.ascii, wrapper, options.asis, response(options.on_error), response(options.on_empty),
      options.on_mismatch == peg::JsonResponse::Error ? 0u : options.on_mismatch == peg::JsonResponse::Null ? 1u
          : options.on_mismatch == peg::JsonResponse::Dot ? 3u : 2u, options.multivalue};
  for (const uint64_t value : configuration) {
    ObRawExpr *argument = nullptr;
    OZ(build_implicit_literal({peg::LiteralKind::Integer, value, {}, {}, false, 0}, argument));
    OZ(function->add_param_expr(argument));
  }
  OX(expr = function);
  return ret;
}

int ObRawExprResolverImpl::process_json_value_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  const auto options = peg::json_options(node);
  const auto &target_node = peg::json_returning(node);
  ObRawExprResType target;
  OZ(ObResolverUtils::resolve_cast_type(target_node, source_text(target_node), target));
  OZ(cast_accuracy_check(target, source_text(peg::json_document(node))));
  if ((options.on_empty == peg::JsonResponse::Default && peg::is_literal(peg::json_empty_default(node))
       && peg::expression_literal(peg::json_empty_default(node)).kind == peg::LiteralKind::Null)
      || (options.on_error == peg::JsonResponse::Default && peg::is_literal(peg::json_error_default(node))
          && peg::expression_literal(peg::json_error_default(node)).kind == peg::LiteralKind::Null)) {
    ret = OB_ERR_PARSER_SYNTAX;
  }
  OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_SYS_JSON_VALUE, function));
  OX(function->set_func_name(ObString::make_string("json_value")));
  OZ(function->init_param_exprs(11));
  ObRawExpr *document = nullptr, *path = nullptr, *returning = nullptr;
  OZ(check_first_node(peg::json_document(node)));
  OZ(SMART_CALL(recursive_resolve(peg::json_document(node), document)));
  OZ(SMART_CALL(recursive_resolve(peg::json_path(node), path)));
  OZ(build_cast_type(target_node, target, returning));
  OZ(function->add_param_expr(document));
  OZ(function->add_param_expr(path));
  OZ(function->add_param_expr(returning));
  const auto append_integer = [&](uint64_t value) -> int {
    ObRawExpr *argument = nullptr;
    int status = build_implicit_literal({peg::LiteralKind::Integer, value, {}, {}, false, 0}, argument);
    if (OB_SUCC(status)) status = function->add_param_expr(argument);
    return status;
  };
  OZ(append_integer(options.truncate));
  OZ(append_integer(options.ascii));
  const auto response = [](peg::JsonResponse value) -> uint64_t {
    switch (value) {
      case peg::JsonResponse::Error: return 0;
      case peg::JsonResponse::Null: return 1;
      case peg::JsonResponse::Default: return 2;
      default: return 3;
    }
  };
  OZ(append_integer(response(options.on_empty)));
  ObRawExpr *empty = nullptr, *error = nullptr;
  OZ(SMART_CALL(recursive_resolve(peg::json_empty_default(node), empty)));
  OZ(function->add_param_expr(empty));
  OZ(append_integer(response(options.on_error)));
  OZ(SMART_CALL(recursive_resolve(peg::json_error_default(node), error)));
  OZ(function->add_param_expr(error));
  // The grammar does not accept JSON_VALUE mismatch clauses; both engine defaults remain explicit.
  OZ(append_integer(3));
  OZ(append_integer(3));
  OX(expr = function);
  return ret;
}

int ObRawExprResolverImpl::cast_accuracy_check(const ObRawExprResType &target, const ObString &input)
{
  int ret = OB_SUCCESS;
  if (target.get_type() == ObFloatType || target.get_type() == ObDoubleType || target.get_type() == ObNumberType) {
    const int16_t precision = target.get_precision();
    const int16_t scale = target.get_scale();
    ObCStringHelper helper;
    if (precision < scale) {
      ret = OB_ERR_M_BIGGER_THAN_D;
      LOG_USER_ERROR(OB_ERR_M_BIGGER_THAN_D, helper.convert(input));
    } else if (precision > OB_MAX_DECIMAL_PRECISION) {
      ret = OB_ERR_TOO_BIG_PRECISION;
      LOG_USER_ERROR(OB_ERR_TOO_BIG_PRECISION, precision, helper.convert(input), OB_MAX_DECIMAL_PRECISION);
    } else if (scale > OB_MAX_DECIMAL_SCALE) {
      ret = OB_ERR_TOO_BIG_SCALE;
      LOG_USER_ERROR(OB_ERR_TOO_BIG_SCALE, scale, helper.convert(input), OB_MAX_DECIMAL_SCALE);
    }
  }
  return ret;
}

int ObRawExprResolverImpl::process_cast(const peg::Node &node, ObRawExpr *&expr)
{
  if (peg::cast_array(node)) return process_json_query_node(node, expr);
  int ret = OB_SUCCESS;
  ObRawExpr *value = nullptr, *target = nullptr;
  ObSysFunRawExpr *function = nullptr;
  ObRawExprResType type;
  const auto &syntax = peg::cast_target(node);
  OZ(ObResolverUtils::resolve_cast_type(syntax, source_text(syntax), type));
  OZ(cast_accuracy_check(type, source_text(peg::cast_argument(node))));
  OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_SYS, function));
  OX(function->set_func_name(N_CAST));
  OX(function->set_cast_mode(CM_EXPLICIT_CAST));
  if (OB_SUCC(ret) && peg::cast_ignore(node)) {
    if (ctx_.session_info_->is_inner() || ctx_.is_in_system_view_ || ctx_.is_from_show_resolver_) {
      function->set_cast_mode(CM_INTERNAL_CAST_IGNORE);
    } else ret = OB_ERR_PARSER_SYNTAX;
  }
  OZ(SMART_CALL(recursive_resolve(peg::cast_argument(node), value)));
  OZ(build_cast_type(syntax, type, target));
  OZ(function->set_param_exprs(value, target));
  OZ(process_sys_func_params(*function, ctx_.columns_->count()));
  OZ(ObRawExprUtils::function_alias(ctx_.expr_factory_, function));
  OZ(function->check_param_num());
  OX(expr = function);
  return ret;
}

int ObRawExprResolverImpl::build_cast_type(const peg::Node &node,
                                          const ObRawExprResType &target, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  peg::TypeSyntax syntax;
  if (OB_FAIL(ObResolverUtils::resolve_type_syntax(node, syntax))) return ret;
  if (ob_is_string_tc(target.get_type()) && syntax.has_length && syntax.length > UINT32_MAX) {
    ret = OB_ERR_TOO_BIG_DISPLAYWIDTH;
    LOG_USER_ERROR(OB_ERR_TOO_BIG_DISPLAYWIDTH, "cast of char", static_cast<int64_t>(UINT32_MAX));
  }
  OZ(build_cast_argument(target, syntax_text(syntax.charset), expr));
  if (OB_SUCC(ret) && target.get_type() == ObGeometryType) {
    auto *constant = static_cast<ObConstRawExpr *>(expr);
    CastTypeDescriptor descriptor(constant->get_value().get_int());
    descriptor.set_geometry_type(ObResolverUtils::resolve_geometry_type(syntax.geometry));
    ObObj value;
    value.set_int(descriptor.value());
    constant->set_value(value);
  }
  return ret;
}


int ObRawExprResolverImpl::process_fun_sys_node(const peg::Node &node,
                                               ObRawExpr *&expr,
                                               const bool is_root_expr)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  const auto spelling = peg::call_name(node);
  const ObString original_name(syntax_text(spelling));
  const bool natural_log = original_name.case_compare("ln") == 0;
  const ObString name = natural_log ? ObString::make_string("log") : original_name;
  const auto arguments = peg::call_arguments(node);
  OZ(check_scalar_quantifier(node));
  if (OB_SUCC(ret) && (natural_log || name.case_compare("log") == 0)
      && (arguments.empty() || arguments.size() > (natural_log ? 1 : 2))) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, original_name.length(), original_name.ptr());
  }
  if (name.case_compare("mod") == 0) {
    if (OB_SUCC(ret) && arguments.size() != 2) {
      ret = OB_ERR_PARAM_SIZE;
      LOG_USER_ERROR(OB_ERR_PARAM_SIZE, name.length(), name.ptr());
    }
    ObRawExpr *left = nullptr, *right = nullptr;
    if (OB_SUCC(ret)) {
      OZ(SMART_CALL(recursive_resolve(arguments[0], left)));
      OZ(SMART_CALL(recursive_resolve(arguments[1], right)));
      OZ(ObRawExprUtils::build_common_binary_op_expr(ctx_.expr_factory_, T_OP_MOD, left, right, expr));
    }
    return ret;
  }
  ObString owned_name;
  OZ(check_internal_function(name));
  if (OB_SUCC(ret) && (0 == name.case_compare("nextval")
      || (T_FROM_SCOPE != ctx_.current_scope_ && 0 == name.case_compare("generator")))) {
    ret = OB_ERR_FUNCTION_UNKNOWN;
    LOG_USER_ERROR(OB_ERR_FUNCTION_UNKNOWN, "FUNCTION", name.length(), name.ptr());
  }
  if (OB_SUCC(ret) && T_INVALID == ObExprOperatorFactory::get_type_by_name(name)) {
    ret = OB_ERR_FUNCTION_UNKNOWN;
  }
  OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), name, owned_name));
  OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_SYS, function));
  OX(function->set_func_name(owned_name));
  const int64_t column_count = ctx_.columns_->count();
  const bool log_base = name.case_compare("log") == 0 && arguments.size() == 1;
  const bool weight_options = name.case_compare("weight_string") == 0 && arguments.size() == 1;
  const int64_t hidden_arguments = node.kind() == peg::NodeKind::DefaultValue ? 4 : log_base ? 1 : 0;
  const int64_t argument_count = arguments.size() + hidden_arguments + (weight_options ? 4 : 0);
  OZ(function->init_param_exprs(argument_count));
  if (log_base) {
    ObConstRawExpr *base = nullptr;
    OZ(ObRawExprUtils::build_const_double_expr(ctx_.expr_factory_, ObDoubleType, 2.718281828459045, base));
    OZ(function->add_param_expr(base));
  }
  const bool mark_values = name.case_compare("values") == 0 && !ctx_.parents_expr_info_.has_member(IS_VALUES);
  if (mark_values) OZ(ctx_.parents_expr_info_.add_member(IS_VALUES));
  for (const auto &argument : arguments) {
    ObRawExpr *value = nullptr;
    if (OB_SUCC(ret) && argument.kind() == peg::NodeKind::AliasedArgument) {
      ret = OB_ERR_FUNCTION_UNKNOWN;
    }
    OZ(SMART_CALL(recursive_resolve(argument, value)));
    OZ(function->add_param_expr(value));
  }
  if (mark_values) {
    const int cleanup_ret = ctx_.parents_expr_info_.del_member(IS_VALUES);
    if (OB_SUCC(ret)) ret = cleanup_ret;
  }
  for (int64_t i = 0; OB_SUCC(ret) && !log_base && i < hidden_arguments; ++i) {
    ObRawExpr *placeholder = nullptr;
    OZ(ObRawExprUtils::build_null_expr(ctx_.expr_factory_, placeholder));
    OZ(function->add_param_expr(placeholder));
  }
  if (weight_options) OZ(resolve_weight_string_options(node, *function));
  if (OB_FAIL(ret) && function != nullptr) {
    const int original_ret = ret;
    ret = ObRawExprUtils::function_alias(ctx_.expr_factory_, function);
    if (OB_SUCC(ret)) ret = function->check_param_num(argument_count);
    if (OB_SUCC(ret)) ret = original_ret;
  }
  if (!arguments.empty()) OZ(process_sys_func_params(*function, column_count));
  OZ(ObRawExprUtils::function_alias(ctx_.expr_factory_, function));
  OZ(function->check_param_num());
  OX(expr = function);
  return ret;
}


int ObRawExprResolverImpl::resolve_weight_string_options(const peg::Node &node, ObSysFunRawExpr &function)
{
  int ret = OB_SUCCESS;
  const auto syntax = peg::weight_string_definition(node);
  uint64_t weights = 0, flags = 0;
  if (!syntax.length.empty()) {
    OZ(resolve_weight_integer(syntax.length[0], weights));
    if (OB_SUCC(ret) && weights == 0) {
      ret = OB_INVALID_ARGUMENT;
      _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "WEIGHT_STRING AS length must be positive");
    }
    flags = OB_STRXFRM_PAD_WITH_SPACE;
  }
  if (OB_SUCC(ret) && syntax.binary && !syntax.levels.empty()) {
    ret = OB_INVALID_ARGUMENT;
    _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "WEIGHT_STRING LEVEL requires character input");
  }
  for (const auto &level : syntax.levels) {
    if (OB_FAIL(ret)) break;
    const auto definition = peg::weight_string_level(level);
    uint64_t first = 0, last = 0;
    OZ(resolve_weight_integer(definition.numbers[0], first));
    if (OB_SUCC(ret)) first = std::max<uint64_t>(1, std::min<uint64_t>(first, OB_STRXFRM_NLEVELS)) - 1;
    if (definition.numbers.size() == 2) {
      OZ(resolve_weight_integer(definition.numbers[1], last));
      if (OB_SUCC(ret)) last = std::max(first, std::max<uint64_t>(1,
          std::min<uint64_t>(last, OB_STRXFRM_NLEVELS)) - 1);
    } else {
      last = first;
    }
    uint64_t bits = 1;
    if (definition.descending) bits |= OB_STRXFRM_DESC_LEVEL1;
    if (definition.reverse) bits |= OB_STRXFRM_REVERSE_LEVEL1;
    for (uint64_t position = first; OB_SUCC(ret) && position <= last; ++position) flags |= bits << position;
  }
  for (const uint64_t value : {uint64_t{0}, weights, flags, uint64_t(syntax.binary)}) {
    ObRawExpr *parameter = nullptr;
    OZ(build_implicit_literal({value > INT64_MAX ? peg::LiteralKind::Unsigned : peg::LiteralKind::Integer,
        value, {}, {}, false, 0}, parameter));
    OZ(function.add_param_expr(parameter));
  }
  return ret;
}

int ObRawExprResolverImpl::process_sys_func_params(ObSysFunRawExpr &func_expr, int current_columns_count)
{
  int ret = OB_SUCCESS;
  const ObExprOperatorType expr_type = ObExprOperatorFactory::get_type_by_name(func_expr.get_func_name());
  switch (expr_type)
  {
    case T_FUN_SYS_NAME_CONST:
      if (current_columns_count != ctx_.columns_->count()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, N_NAME_CONST);
      }
      break;
    case T_FUN_SYS_UUID2BIN:
    case T_FUN_SYS_BIN2UUID:
      if (2 == func_expr.get_param_count()) {
        //add bool expr for the second param
        ObRawExpr *param_expr = func_expr.get_param_expr(1);
        ObRawExpr *new_param_expr = NULL;
        if (OB_FAIL(ObRawExprUtils::try_create_bool_expr(param_expr, new_param_expr, ctx_.expr_factory_))) {
        } else if (OB_ISNULL(new_param_expr)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          func_expr.replace_param_expr(1, new_param_expr);
        }
      }
      break;
    case T_FUN_SYS_GENERATOR:
      if (1 == func_expr.get_param_count()) {
        ObItemType type = func_expr.get_param_expr(0)->get_expr_type();
        if (T_QUESTIONMARK != type && T_INT != type && T_NUMBER != type) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "generator function. The argument should be a constant integer");
        }
      }
      break;
    case T_FUN_SYS_DATE_ADD:
        // Added a fourth parameter to distinguish between “date + interval” and “interval + date”
        // So you should use it to distinguish these two cases
        if(4 == func_expr.get_param_count()) {
          ObRawExpr *expr3 = func_expr.get_param_expr(3);
          if(OB_LIKELY(NULL != expr3 && expr3->is_const_raw_expr())) {
            ObConstRawExpr *const_expr = static_cast<ObConstRawExpr *>(expr3);
            int64_t expr3_val = OB_INVALID_ID;
            if(T_INT == const_expr->get_expr_type()) {
              if (OB_FAIL(const_expr->get_value().get_int(expr3_val))) {
                ret = OB_ERR_UNEXPECTED;
              } else if (2 == expr3_val) {
                // INTERVAL expr date_unit '+' bit_expr
                ObRawExpr *interval_expr = func_expr.get_param_expr(0);
                ObRawExpr *unit_expr = func_expr.get_param_expr(1);
                ObRawExpr *bit_expr = func_expr.get_param_expr(2);
                func_expr.reuse_child();
                if (OB_FAIL(func_expr.set_param_exprs(bit_expr, interval_expr, unit_expr))) {
                }
              } else if (1 == expr3_val) {
                // bit_expr '+' INTERVAL expr date_unit
                if (OB_FAIL(func_expr.remove_param_expr(3))) {
                  ret = OB_ERR_UNEXPECTED;
                }
              }
            }
          } else {
            ret = OB_ERR_UNEXPECTED;
          }
        }
      break;
    case T_FUN_SYS_CAST: {
      ObRawExpr* param0 = NULL;
      ObRawExpr* param1 = NULL;
      if (2 != func_expr.get_param_count()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(param0 = func_expr.get_param_expr(0)) || OB_ISNULL(param1 = func_expr.get_param_expr(1))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(!param1->is_const_raw_expr())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const ObObj &param = static_cast<ObConstRawExpr*>(param1)->get_param();
        ObObjType obj_type = static_cast<ObObjType>(static_cast<uint64_t>(param.get_int()) & UINT16_MAX);
        if (ob_is_extend(obj_type) && OB_NOT_NULL(ctx_.secondary_namespace_)) {
          const pl::ObUserDefinedType *user_type = NULL;
          if (OB_FAIL(ctx_.secondary_namespace_->get_pl_data_type_by_id(param1->get_udt_id(), user_type))) {
          } else if (OB_ISNULL(user_type)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (T_NULL != param0->get_expr_type() && user_type->is_associative_array_type()) {
            ret = OB_ERR_EXPRESSION_WRONG_TYPE;
          }
        }
      }
      break;
    }
    default:
      break;
  }
  return ret;
}

int ObRawExprResolverImpl::resolve_stored_function(const peg::Node &call, ObUDFInfo &udf_info)
{
  int ret = OB_SUCCESS;
  ObUDFRawExpr *function = nullptr;
  const auto name = peg::call_name(call);
  const auto qualifiers = peg::call_qualifiers(call);
  ObString database, package;
  if (qualifiers.size() > 2) {
    ret = OB_ERR_PARSER_SYNTAX;
  } else {
    new (&udf_info) ObUDFInfo();
    OZ(ObResolverUtils::resolve_udf_name(syntax_text(name), database, package,
        ctx_.case_mode_, ctx_.expr_factory_.get_allocator(), udf_info));
    OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_UDF, function));
    OZ(function->init_param_exprs(peg::call_arguments(call).size()));
    bool named = false;
    for (const auto &argument : peg::call_arguments(call)) {
      ObRawExpr *value = nullptr;
      if (argument.kind() == peg::NodeKind::PlNamedArgument) {
        named = true;
        const auto &identifier = peg::named_argument_name(argument);
        if (identifier.kind() != peg::NodeKind::Ident) {
          ret = OB_ERR_CALL_WRONG_ARG;
        }
        ObString parameter_name;
        const auto identifier_text = identifier.text();
        OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(identifier_text), parameter_name));
        OZ(udf_info.param_names_.push_back(parameter_name));
        OZ(SMART_CALL(recursive_resolve(peg::named_argument_value(argument), value)));
        OZ(udf_info.param_exprs_.push_back(value));
      } else if (named) {
        ret = OB_ERR_POSITIONAL_FOLLOW_NAME;
      } else {
        OZ(SMART_CALL(recursive_resolve(argument, value)));
        OZ(function->add_param_expr(value));
        OX(++udf_info.udf_param_num_);
      }
    }
  }
  if (OB_SUCC(ret) && ctx_.query_ctx_ != nullptr) {
    ctx_.query_ctx_->has_udf_ = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < ctx_.query_ctx_->all_user_variable_.count(); ++i) {
      if (OB_ISNULL(ctx_.query_ctx_->all_user_variable_.at(i))) ret = OB_ERR_UNEXPECTED;
      else ctx_.query_ctx_->all_user_variable_.at(i)->set_query_has_udf(true);
    }
  }
  OX(function->set_func_name(udf_info.udf_name_));
  OX(udf_info.ref_expr_ = function);
  OZ(function->extract_info());
  return ret;
}


int ObRawExprResolverImpl::process_match_against(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObMatchFunRawExpr *result = nullptr;
  const bool elastic = node.kind() == peg::NodeKind::ElasticMatch;
  OZ(ctx_.expr_factory_.create_raw_expr(elastic ? T_FUN_ES_MATCH : T_FUN_MATCH_AGAINST, result));
  OZ(ctx_.match_exprs_->push_back(result));
  for (const auto &column : peg::fulltext_columns(node)) {
    ObRawExpr *reference = nullptr;
    OZ(process_column_ref_node(peg::fulltext_column_reference(column), reference));
    OZ(result->get_match_columns().push_back(reference));
    if (OB_SUCC(ret) && elastic) {
      ObRawExpr *boost = nullptr;
      OZ(SMART_CALL(recursive_resolve(peg::fulltext_column_boost(column), boost)));
      OZ(boost->extract_info());
      OZ(result->get_columns_boosts().push_back(boost));
    }
  }
  ObRawExpr *search = nullptr;
  OZ(SMART_CALL(recursive_resolve(peg::fulltext_search(node), search)));
  OZ(search->extract_info());
  if (OB_SUCC(ret) && !search->is_static_const_expr()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "non-const search query");
  }
  OX(result->set_search_key(search));
  if (OB_SUCC(ret) && elastic) {
    ObRawExpr *options = nullptr;
    OZ(SMART_CALL(recursive_resolve(peg::fulltext_options(node), options)));
    OX(result->set_param_text_expr(options));
  } else if (OB_SUCC(ret)) {
    ObMatchAgainstMode mode = ObMatchAgainstMode::NATURAL_LANGUAGE_MODE;
    switch (peg::fulltext_mode(node)) {
      case peg::FulltextMode::Natural: break;
      case peg::FulltextMode::Boolean: mode = ObMatchAgainstMode::BOOLEAN_MODE; break;
      case peg::FulltextMode::Phrase: mode = ObMatchAgainstMode::MATCH_PHRASE_MODE; break;
      default:
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "search modes other than NATURAL_LANGUAGE_MODE, BOOLEAN_MODE or MATCH_PHRASE_MODE");
        break;
    }
    OX(result->set_mode_flag(mode));
    OZ(result->extract_info());
  }
  OX(expr = result);
  return ret;
}

int ObRawExprResolverImpl::process_match_score(ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  if (ctx_.stmt_ == nullptr || static_cast<ObDMLStmt *>(ctx_.stmt_)->get_match_exprs().empty()) {
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED, "The position of score() is");
  } else {
    const auto &matches = static_cast<ObDMLStmt *>(ctx_.stmt_)->get_match_exprs();
    if (!matches.at(0)->is_es_match()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "The score with match agaisnt is");
    } else {
      expr = matches.at(0);
      for (int64_t i = 1; OB_SUCC(ret) && i < matches.count(); ++i) {
        ObOpRawExpr *sum = nullptr;
        OZ(ObRawExprUtils::build_add_expr(ctx_.expr_factory_, matches.at(i), expr, sum));
        OX(expr = sum);
      }
    }
  }
  return ret;
}

int ObRawExprResolverImpl::not_int_check(const ObRawExpr *expr)
{
  int ret = OB_SUCCESS;
  if (NULL != expr && T_INT == expr->get_expr_type()) {
    ret = OB_ERR_WINDOW_ILLEGAL_ORDER_BY;
  }
  return ret;
}

int ObRawExprResolverImpl::not_row_check(const ObRawExpr *expr)
{
  int ret = OB_SUCCESS;
  if (NULL != expr && T_OP_ROW == expr->get_expr_type()) {
    ret = OB_ERR_INVALID_COLUMN_NUM;
    LOG_USER_ERROR(OB_ERR_INVALID_COLUMN_NUM, (int64_t)1);
  }
  return ret;
}

int ObRawExprResolverImpl::param_not_row_check(const ObRawExpr *expr)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && NULL != expr && i < expr->get_param_count(); i++) {
    OZ(not_row_check(expr->get_param_expr(i)));
  }
  return ret;
}

int ObRawExprResolverImpl::process_window_function_node(const peg::Node &node, ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObWinFunRawExpr *window = nullptr;
  const bool named = node.kind() == peg::NodeKind::NamedWindow;
  if (!is_win_expr_valid_scope(ctx_.current_scope_) || ctx_.parents_expr_info_.has_member(IS_AGG)
      || ctx_.parents_expr_info_.has_member(IS_WINDOW_FUNC)) {
    ret = OB_ERR_INVALID_WINDOW_FUNCTION_PLACE;
  }
  OZ(ctx_.expr_factory_.create_raw_expr(T_WINDOW_FUNCTION, window));
  ObRawExpr *aggregate = nullptr;
  ObArray<ObRawExpr *> parameters;
  ObItemType type = T_FUN_COUNT;
  if (OB_SUCC(ret) && named) {
    ObAggFunRawExpr *count = nullptr;
    OZ(ctx_.expr_factory_.create_raw_expr(T_FUN_COUNT, count));
    OX(aggregate = count);
    const auto &identifier = peg::named_window_name(node);
    ObString name;
    const auto identifier_text = identifier.text();
    OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(identifier_text), name));
    OX(window->set_win_name(name));
  } else if (OB_SUCC(ret)) {
    const auto &function = peg::window_function(node);
    type = callable_expression_type(function.kind());
    const bool aggregate_function = IS_AGGR_FUN(type) || type == T_FUN_VARIANCE || type == T_FUN_STDDEV
        || type == T_FUN_STDDEV_POP || type == T_FUN_STDDEV_SAMP;
    if (type == T_FUN_GROUP_PERCENTILE_CONT || type == T_FUN_SYS_ST_ASMVT || type == T_FUN_SUM_OPNSIZE
        || (!aggregate_function && (type < T_WIN_FUN_ROW_NUMBER || type > T_WIN_FUN_LAG))) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "The specified function as a window function is");
    }
    const bool was_window_aggregate = ctx_.is_win_agg_;
    OZ(ctx_.parents_expr_info_.add_member(IS_WINDOW_FUNC));
    if (aggregate_function) {
      ctx_.is_win_agg_ = true;
      OZ(SMART_CALL(recursive_resolve(function, aggregate)));
    } else {
      OZ(check_scalar_quantifier(function));
      const auto arguments = peg::call_arguments(function);
      if (((type == T_WIN_FUN_LEAD || type == T_WIN_FUN_LAG) && (arguments.empty() || arguments.size() > 3))
          || ((type == T_WIN_FUN_FIRST_VALUE || type == T_WIN_FUN_LAST_VALUE) && arguments.size() != 1)
          || (type == T_WIN_FUN_NTH_VALUE && arguments.size() != 2)
          || ((type == T_WIN_FUN_ROW_NUMBER || type == T_WIN_FUN_RANK
                    || type == T_WIN_FUN_DENSE_RANK || type == T_WIN_FUN_PERCENT_RANK
                    || type == T_WIN_FUN_CUME_DIST) && !arguments.empty())
          || (type == T_WIN_FUN_NTILE && arguments.size() != 1)) {
        ret = OB_ERR_PARAM_SIZE;
        const auto name = peg::call_name(function);
        LOG_USER_ERROR(OB_ERR_PARAM_SIZE, int(name.size()), name.data());
      }
      for (const auto &argument : arguments) {
        ObRawExpr *value = nullptr;
        OZ(SMART_CALL(recursive_resolve(argument, value)));
        OZ(parameters.push_back(value));
      }
      OX(window->set_is_ignore_null(peg::window_null_treatment(function) == peg::NullTreatment::Ignore));
      OX(window->set_is_from_first(peg::window_nth_origin(function) != peg::NthOrigin::Last));
      if (OB_SUCC(ret) && type == T_WIN_FUN_NTH_VALUE) {
        OZ(parameters.at(1)->extract_info());
        if (OB_SUCC(ret) && !parameters.at(1)->is_const_expr()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "nth_value");
        }
      } else if (OB_SUCC(ret) && (type == T_WIN_FUN_FIRST_VALUE || type == T_WIN_FUN_LAST_VALUE)) {
        window->set_is_from_first(type == T_WIN_FUN_FIRST_VALUE);
        type = T_WIN_FUN_NTH_VALUE;
        ObConstRawExpr *one = nullptr;
        OZ(ObRawExprUtils::build_const_int_expr(ctx_.expr_factory_, ObIntType, 1, one));
        OZ(parameters.push_back(one));
      }
    }
    ctx_.is_win_agg_ = was_window_aggregate;
    const int cleanup_ret = ctx_.parents_expr_info_.del_member(IS_WINDOW_FUNC);
    if (OB_SUCC(ret)) ret = cleanup_ret;
  }
  OX(window->set_func_type(type));
  OZ(window->set_func_params(parameters));
  OZ(resolve_window_spec(named ? peg::named_window_definition(node) : peg::window_definition(node), *window));
  if (OB_SUCC(ret) && aggregate != nullptr) {
    OZ(process_window_agg_node(type, window, aggregate, expr));
  } else {
    OZ(ctx_.win_exprs_->push_back(window));
    OX(expr = window);
  }
  OZ(param_not_row_check(expr));
  return ret;
}

int ObRawExprResolverImpl::resolve_window_spec(const peg::Node &specification, ObWinFunRawExpr &window)
{
  int ret = OB_SUCCESS;
  ObWinFunRawExpr *inherited = nullptr;
  const auto partition = peg::window_partition(specification);
  const auto order = peg::window_order(specification);
  const bool frame_written = peg::window_has_frame(specification);
  if (peg::window_has_reference(specification)) {
    const auto &identifier = peg::window_reference(specification);
    const auto identifier_text = identifier.text();
    const ObString name(syntax_text(identifier_text));
    if (OB_ISNULL(ctx_.stmt_) || !ctx_.stmt_->is_select_stmt()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const auto &windows = static_cast<ObSelectStmt *>(ctx_.stmt_)->get_window_func_exprs();
      for (int64_t i = 0; inherited == nullptr && i < windows.count(); ++i) {
        if (ObCharset::case_insensitive_equal(name, windows.at(i)->get_win_name())) inherited = windows.at(i);
      }
      if (inherited == nullptr) {
        ret = OB_ERR_WINDOW_NAME_IS_NOT_DEFINE;
        LOG_USER_ERROR(OB_ERR_WINDOW_NAME_IS_NOT_DEFINE, name.length(), name.ptr());
      } else if ((ctx_.current_scope_ == T_NAMED_WINDOWS_SCOPE || frame_written) && inherited->has_frame_orig()) {
        ret = OB_EER_WINDOW_NO_INHERIT_FRAME;
        LOG_USER_ERROR(OB_EER_WINDOW_NO_INHERIT_FRAME, name.length(), name.ptr());
      } else if (!partition.empty()) {
        ret = OB_EER_WINDOW_NO_CHILD_PARTITIONING;
        LOG_USER_ERROR(OB_EER_WINDOW_NO_CHILD_PARTITIONING);
      } else if (!order.empty() && inherited->has_order_items()) {
        ret = OB_EER_WINDOW_NO_REDEFINE_ORDER_BY;
      }
    }
  }
  ObArray<ObRawExpr *> partitions;
  ObArray<OrderItem> orders;
  ObFrame frame;
  ObString sort_text;
  if (inherited != nullptr) {
    OZ(partitions.assign(inherited->get_partition_exprs()));
    if (inherited->has_order_items()) {
      OZ(orders.assign(inherited->get_order_items()));
      OX(sort_text = inherited->sort_str_);
    }
    if (inherited->has_frame_orig()) OZ(frame.assign(*inherited));
  }
  for (const auto &key : partition) {
    ObRawExpr *value = nullptr;
    OZ(SMART_CALL(recursive_resolve(key, value)));
    OZ(not_row_check(value));
    OZ(partitions.push_back(value));
  }
  for (const auto &sort : order) {
    OrderItem item;
    item.order_type_ = peg::sort_direction(sort) == peg::SortDirection::Descending ? NULLS_LAST_DESC : NULLS_FIRST_ASC;
    OZ(SMART_CALL(recursive_resolve(peg::sort_expression(sort), item.expr_)));
    OZ(not_int_check(item.expr_));
    OZ(not_row_check(item.expr_));
    OZ(orders.push_back(item));
  }
  if (!order.empty()) {
    OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), source_text(peg::sort_expression(order[0])), sort_text));
  }
  if (frame_written) {
    OZ(process_frame_node(peg::window_frame(specification), frame));
    if (OB_SUCC(ret) && ((frame.get_upper().interval_expr_ != nullptr && !frame.get_upper().interval_expr_->is_const_expr())
        || (frame.get_lower().interval_expr_ != nullptr && !frame.get_lower().interval_expr_->is_const_expr()))) {
      ret = OB_ERR_WINDOW_RANGE_BOUND_NOT_CONSTANT;
      const ObString name = inherited == nullptr ? ObString("<unnamed window>") : inherited->get_win_name();
      LOG_USER_ERROR(OB_ERR_WINDOW_RANGE_BOUND_NOT_CONSTANT, name.length(), name.ptr());
    }
  }
  OX(window.set_has_frame_orig(frame_written));
  OZ(window.set_partition_exprs(partitions));
  OZ(window.set_order_items(orders));
  OZ(window.ObFrame::assign(frame));
  OX(window.sort_str_ = sort_text);
  return ret;
}


int ObRawExprResolverImpl::process_window_agg_node(const ObItemType func_type,
                                                   ObWinFunRawExpr *win_func,
                                                   ObRawExpr *agg_node_expr,
                                                   ObRawExpr *&expr)
{
  int ret = OB_SUCCESS;
  ObAggFunRawExpr *agg_expr = static_cast<ObAggFunRawExpr *>(agg_node_expr);
  if (OB_ISNULL(agg_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((T_FUN_SUM == func_type || T_FUN_COUNT == func_type || T_FUN_AVG == func_type ||
              T_FUN_VARIANCE == func_type || T_FUN_STDDEV == func_type) &&
             (agg_expr->is_param_distinct() &&
              (win_func->has_order_items() || win_func->has_frame_orig()))) {
    ret = OB_ORDERBY_CLAUSE_NOT_ALLOWED;
  } else if (FALSE_IT(win_func->set_agg_expr(agg_expr))) {
  } else if (OB_FAIL(ctx_.win_exprs_->push_back(win_func))) {
  } else {
    expr = win_func;
  }

  return ret;
}

int ObRawExprResolverImpl::process_frame_node(const peg::Node &node, ObFrame &frame)
{
  int ret = OB_SUCCESS;
  frame.win_type_ = peg::frame_unit(node) == peg::FrameUnit::Rows ? WINDOW_ROWS : WINDOW_RANGE;
  frame.is_between_ = peg::frame_has_end(node);
  OZ(process_bound_node(peg::frame_start(node), true, frame.get_upper()));
  if (frame.is_between_) OZ(process_bound_node(peg::frame_end(node), false, frame.get_lower()));
  if (OB_SUCC(ret) && frame.win_type_ == WINDOW_ROWS
      && ((frame.get_upper().type_ == BOUND_INTERVAL && !frame.get_upper().is_nmb_literal_)
          || (frame.get_lower().type_ == BOUND_INTERVAL && !frame.get_lower().is_nmb_literal_))) {
    ret = OB_ERR_WINDOW_ROWS_INTERVAL_USE;
  }
  return ret;
}


int ObRawExprResolverImpl::process_bound_node(const peg::Node &node, const bool is_upper, Bound &bound)
{
  int ret = OB_SUCCESS;
  const auto kind = peg::frame_bound(node);
  bound.type_ = BOUND_UNBOUNDED;
  bound.is_preceding_ = kind == peg::FrameBound::UnboundedPreceding || kind == peg::FrameBound::Preceding;
  if (kind == peg::FrameBound::CurrentRow) {
    bound.type_ = BOUND_CURRENT_ROW;
    bound.is_preceding_ = is_upper;
  } else if (kind == peg::FrameBound::Preceding || kind == peg::FrameBound::Following) {
    bound.type_ = BOUND_INTERVAL;
    bound.is_nmb_literal_ = !peg::frame_bound_has_unit(node);
    OZ(SMART_CALL(recursive_resolve(peg::frame_bound_offset(node), bound.interval_expr_)));
    OZ(bound.interval_expr_->extract_info());
    if (!bound.is_nmb_literal_) {
      OZ(SMART_CALL(recursive_resolve(peg::frame_bound_unit(node), bound.date_unit_expr_)));
      OZ(bound.date_unit_expr_->extract_info());
    }
  }
  return ret;
}

/**
 * window function which can't not specified windowing_clause
 */
bool ObRawExprResolverImpl::should_not_contain_window_clause(const ObItemType func_type)
{
  /**
   * Some analytic functions allow the windowing_clause.
   */
  return T_WIN_FUN_ROW_NUMBER == func_type
      || T_WIN_FUN_NTILE == func_type
      || T_WIN_FUN_CUME_DIST == func_type
      || T_WIN_FUN_LAG == func_type
      || T_WIN_FUN_LEAD == func_type
      || T_WIN_FUN_RANK == func_type
      || T_WIN_FUN_DENSE_RANK == func_type
      || T_WIN_FUN_PERCENT_RANK == func_type;
}

/**
 * window function which should contain order_by clause
 */
// When first_value or last_value specifies a window, there must be an ORDER BY.
// Before parsing it would be converted to nth_value.

int ObRawExprResolverImpl::check_and_canonicalize_window_expr(ObRawExpr *expr)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!expr->is_win_func_expr()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObWinFunRawExpr *w_expr = static_cast<ObWinFunRawExpr *>(expr);
    WindowType win_type = w_expr->get_window_type();
    bool is_between = w_expr->is_between();
    Bound upper = w_expr->get_upper();
    Bound lower = w_expr->get_lower();
    ObItemType func_type = w_expr->get_func_type();
    const ObIArray<OrderItem> &order_items = w_expr->get_order_items();
    // @note: do some semantic checks, this step is not done in parser to keep the syntax tree simple and clear
    bool parse_error = false;
    if (win_type != WINDOW_MAX) {
      if (BOUND_UNBOUNDED == upper.type_ && !upper.is_preceding_) {
        parse_error = true;
      }
      if (!parse_error && is_between) {
        if (BOUND_UNBOUNDED == lower.type_ && lower.is_preceding_) {
          parse_error = true;
        }
      }
    }
    if (!parse_error && win_type != WINDOW_MAX && !is_between && BOUND_INTERVAL == upper.type_ && !upper.is_preceding_) {
       parse_error = true;
    }

    if (parse_error) {
      ret = OB_ERR_PARSER_SYNTAX;
    }

    if (OB_SUCC(ret) && w_expr->has_frame_orig() &&
        WINDOW_RANGE == win_type && 0 == order_items.count() &&
        (w_expr->get_upper().type_ == BOUND_INTERVAL || w_expr->get_lower().type_ == BOUND_INTERVAL)) {
      /* if preceding or following has a specific value (not the default unbounded)
      * in mysql:
      * @OK: select c1, sum(c1) over(range between current row and current row) from t1;
      * @OK: select c1, sum(c1) over(range unbounded preceding) from t1;
      * @error: select c1, sum(c1) over(range 1 preceding) from t1; --error 3587
      */
      ret = OB_ERR_WINDOW_RANGE_FRAME_ORDER_TYPE;
    }

    // reset frame
    if (OB_SUCC(ret) && win_type != WINDOW_MAX && should_not_contain_window_clause(func_type)) {
      win_type = WINDOW_MAX;
      upper = Bound();
      lower = Bound();
    }

    if (OB_SUCC(ret)) {
      // @note: get a standard window in between form
      // 1. If window is not defined, define a range between (unbounded preceding) and (unbounded following or current row)
      // 2. Convert non-between windows to between
      if (WINDOW_MAX == win_type) {
        win_type = WINDOW_RANGE;
        is_between = true;
        if (0 == order_items.count()) {
          upper.type_ = BOUND_UNBOUNDED;
          lower.type_ = BOUND_UNBOUNDED;
        } else {
          upper.type_ = BOUND_UNBOUNDED;
          lower.type_ = BOUND_CURRENT_ROW;
        }
      } else {
        if (!is_between) {
          is_between = true;
          // Walked here, order by must be defined
          lower.type_ = BOUND_CURRENT_ROW;
          lower.is_preceding_ = false;
        }
      }
    }

    if (OB_SUCC(ret)) {
      //special case
      if (should_not_contain_window_clause(func_type)) {
        lower.type_ = BOUND_UNBOUNDED;
      }
    }
    // Fix unbounded is_preceding_
    // unbounded semantics under is_preceding_ is meaningless, here the correction is made solely for explain display use
    if (OB_SUCC(ret)) {
      if (BOUND_UNBOUNDED == upper.type_) {
        upper.is_preceding_ = true;
      }
      if (BOUND_UNBOUNDED == lower.type_) {
        lower.is_preceding_ = false;
      }
    }
    // The parsing phase directly throws an error for recognizable invalid windows.
    if (OB_SUCC(ret)) {
      if ((BOUND_CURRENT_ROW == upper.type_ && BOUND_INTERVAL == lower.type_ && lower.is_preceding_)
          || (BOUND_CURRENT_ROW == lower.type_ && BOUND_INTERVAL == upper.type_ && !upper.is_preceding_)
          || (BOUND_INTERVAL == upper.type_ && !upper.is_preceding_ && BOUND_INTERVAL == lower.type_ && lower.is_preceding_)) {
        ret = OB_ERR_INVALID_WINDOW_FUNC_USE;

      }
    }
    // When the sort column is a vector, range windows can only be unbounded or current row.
    if (OB_SUCC(ret)) {
      if (order_items.count() > 1 &&
          WINDOW_RANGE == win_type &&
          (upper.interval_expr_ != NULL || lower.interval_expr_ != NULL)) {
        ret = OB_ERR_INVALID_WINDOW_FUNC_USE;

      } else if (WINDOW_RANGE == win_type) {
        Bound *bs[2] = { &upper, &lower };
        for (int i = 0; i < 2 && OB_SUCC(ret); i++) {
          Bound *bound = bs[i];
          if (bound->interval_expr_ != NULL) {
            for (int j = 0; j < 2 && OB_SUCC(ret); j++) {
              if (bound->is_nmb_literal_) {//normal numeric type range addition and subtraction
                ObOpRawExpr *op_expr = NULL;
                if (j == 0 && OB_FAIL(ObRawExprUtils::build_add_expr(ctx_.expr_factory_,
                                                                     order_items.at(0).expr_,
                                                                     bound->interval_expr_,
                                                                     op_expr))) {
                } else if (j == 1 && OB_FAIL(ObRawExprUtils::build_minus_expr(ctx_.expr_factory_,
                                                                              order_items.at(0).expr_,
                                                                              bound->interval_expr_,
                                                                              op_expr))) {
                } else if (OB_ISNULL(op_expr)) {
                  ret = OB_ERR_UNEXPECTED;
                } else {
                  bound->exprs_[j] = op_expr;
                }
              } else if (!bound->is_nmb_literal_) {//date type range addition and subtraction
                ObSysFunRawExpr *sys_expr = NULL;
                if (j == 0 && OB_FAIL(ObRawExprUtils::build_date_add_expr(ctx_.expr_factory_,
                                                                          order_items.at(0).expr_,
                                                                          bound->interval_expr_,
                                                                          bound->date_unit_expr_,
                                                                          sys_expr))) {
                } else if (j == 1 && OB_FAIL(ObRawExprUtils::build_date_sub_expr(ctx_.expr_factory_,
                                                                                  order_items.at(0).expr_,
                                                                                  bound->interval_expr_,
                                                                                  bound->date_unit_expr_,
                                                                                  sys_expr))) {
                } else if (OB_ISNULL(sys_expr)) {
                  ret = OB_ERR_UNEXPECTED;
                } else {
                  bound->exprs_[j] = sys_expr;
                }
              }
            }
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      w_expr->set_window_type(win_type);
      w_expr->set_is_between(is_between);
      w_expr->set_upper(upper);
      w_expr->set_lower(lower);
    }
  }

  return ret;
}
// Query rewrite ratio_to_report(c1) over (partition by c2) ==>
// c1 / case sum(c1) over (partition by c2) when 0 then null else sum(c1) over (partition by c2) end

int ObRawExprResolverImpl::process_internal_sys_function_node(const peg::Node &node,
                                                              ObRawExpr *&expr, ObItemType type)
{
  int ret = OB_SUCCESS;
  ObSysFunRawExpr *function = nullptr;
  const auto spelling = peg::call_name(node);
  ObString name;
  OZ(ob_write_string(ctx_.expr_factory_.get_allocator(), syntax_text(spelling), name));
  OZ(ctx_.expr_factory_.create_raw_expr(type, function));
  OX(function->set_func_name(name));
  OZ(function->init_param_exprs(peg::call_arguments(node).size()));
  for (const auto &argument : peg::call_arguments(node)) {
    ObRawExpr *value = nullptr;
    OZ(SMART_CALL(recursive_resolve(argument, value)));
    OZ(function->add_param_expr(value));
  }
  if (node.kind() == peg::NodeKind::ArrayMembership) {
    OX(function->set_reverse_param_order(true));
  }
  OX(expr = function);
  if (OB_SUCC(ret) && type == T_FUN_SYS_SET_COLLATION && ctx_.is_in_system_view_) {
    const auto spelling = peg::collation_name(node);
    const ObString collation(syntax_text(spelling));
    if (collation.case_compare("utf8mb4_name_case") == 0
        && expr->get_param_expr(1)->get_collation_type() == CS_TYPE_UTF8MB4_GENERAL_CI) {
      expr = expr->get_param_expr(0);
    }
  }
  return ret;
}


int ObRawExprResolverImpl::check_internal_function(const ObString &name)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ctx_.session_info_));
  bool exist = false;
  bool is_internal = false;
  if (OB_FAIL(ret)) {
  } else if (ctx_.session_info_->is_inner()
            || ctx_.is_in_system_view_
            || ctx_.is_from_show_resolver_) {
    // ignore
  } else if (FALSE_IT(ObExprOperatorFactory::get_internal_info_by_name(name, exist, is_internal))) {
  } else if (exist && is_internal) {
    ret = OB_ERR_SP_DOES_NOT_EXIST;
    LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION",
                    ctx_.session_info_->get_database_name().length(),
                    ctx_.session_info_->get_database_name().ptr(),
                    name.length(), name.ptr());
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
