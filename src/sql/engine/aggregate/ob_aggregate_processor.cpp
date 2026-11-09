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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_aggregate_processor.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/executor/ob_execute_result.h"
#include "share/ob_lob_access_utils.h"
#include "share/geo/ob_geo_mvt.h"
#include "sql/engine/expr/ob_expr_add.h"
#include "sql/engine/expr/ob_expr_minus.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/expr/ob_expr_estimate_ndv.h"
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/aggregate/ob_aggregate_util.h"
#include "sql/engine/basic/ob_material_op_impl.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/optimizer/stat/ob_hybrid_hist_estimator.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
#include "sql/engine/expr/ob_expr_sys_op_opnsize.h"
#include "common/xml/ob_binary_aggregate.h"
#include "sql/engine/expr/ob_expr_xml_func_helper.h"
#include "sql/pl/ob_pl.h"
#include "share/datum/datum_value_cast.h"
#include "common/ob_hex_utils_base.h"
#include "common/datum/datum_parameter_codec.h"

namespace oceanbase
{

namespace share
{
namespace aggregate
{
using namespace common;
using namespace sql;

static bool is_rollup_expr(const ObAggrInfo &aggr_info, ObExpr *expr, const int64_t seq)
{
  bool is_rollup = false;
  if (has_exist_in_array(aggr_info.hash_rollup_info_->gby_exprs_, expr)
      || !has_exist_in_array(aggr_info.hash_rollup_info_->expand_exprs_, expr)) {
  } else {
    ObIArray<ObExpr *> &expand_exprs = aggr_info.hash_rollup_info_->expand_exprs_;
    bool found = false;
    for (int64_t i = expand_exprs.count() - seq - 1; !found && i >= 0; --i) {
      found = expr == expand_exprs.at(i);
    }
    is_rollup = !found;
  }
  return is_rollup;
}

bool is_grouping(const ObAggrInfo &aggr_info, const int64_t seq)
{
  OB_ASSERT(aggr_info.param_exprs_.count() == 1 && aggr_info.param_exprs_.at(0) != nullptr);
  return is_rollup_expr(aggr_info, aggr_info.param_exprs_.at(0), seq);
}

int get_grouping_id(const ObAggrInfo &aggr_info, const int64_t seq,
                    number::ObCompactNumber *grouping_id)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObExpr *> &param_exprs = aggr_info.param_exprs_;
  int512_t res = 0;
  int512_t base = 1;
  if (OB_UNLIKELY(param_exprs.count() <= 0) || OB_ISNULL(grouping_id)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "invalid grouping id arguments", K(ret), K(param_exprs.count()), KP(grouping_id));
  }
  for (int64_t i = param_exprs.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
    ObExpr *grouping_expr = param_exprs.at(i);
    if (OB_ISNULL(grouping_expr)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "invalid null expr", K(ret));
    } else if (is_rollup_expr(aggr_info, grouping_expr, seq)) {
      res += base;
    }
    base = base << 1;
  }
  if (OB_SUCC(ret)) {
    number::ObNumber tmp_nmb;
    ObNumStackOnceAlloc tmp_alloc;
    if (OB_FAIL(wide::to_number(res, 0, tmp_alloc, tmp_nmb))) {
    } else {
      grouping_id->desc_ = tmp_nmb.d_;
      MEMCPY(grouping_id->digits_, tmp_nmb.get_digits(),
             tmp_nmb.d_.len_ * sizeof(uint32_t));
    }
  }
  return ret;
}

int get_grouping_id(const ObAggrInfo &aggr_info, const int64_t seq, int64_t *grouping_id)
{
  int ret = OB_SUCCESS;
  const ObIArray<ObExpr *> &param_exprs = aggr_info.param_exprs_;
  int64_t res = 0;
  int64_t base = 1;
  if (OB_UNLIKELY(param_exprs.count() <= 0) || OB_ISNULL(grouping_id)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "invalid grouping id arguments", K(ret), K(param_exprs.count()), KP(grouping_id));
  }
  for (int64_t i = param_exprs.count() - 1; OB_SUCC(ret) && i >= 0; --i) {
    ObExpr *grouping_expr = param_exprs.at(i);
    if (OB_ISNULL(grouping_expr)) {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "invalid null expr", K(ret));
    } else if (is_rollup_expr(aggr_info, grouping_expr, seq)) {
      res += base;
    }
    base <<= 1;
  }
  if (OB_SUCC(ret)) {
    *grouping_id = res;
  }
  return ret;
}
} // end aggregate
} // end share
using namespace common;
using namespace common::number;
namespace sql
{

// These views are confined to the existing accumulator/fixed-row domain. They
// never form an alternate Expr ABI and never retain a native frame address.
struct LegacyAggrValue
{
  alignas(8) char scratch_[64];
  common::ObDatum value_;
  int prepare(const ObExpr &expr, const Datum &value)
  { return expr.get_datum_copy_plan().legacy_view(value, value_, scratch_, sizeof(scratch_)); }
};

static int eval_legacy_aggr(const ObExpr &expr, ObEvalCtx &ctx,
                            LegacyAggrValue &storage, common::ObDatum *&value)
{
  Datum *native = nullptr;
  int ret = expr.eval(ctx, native);
  if (OB_SUCC(ret)) { ret = storage.prepare(expr, *native); }
  if (OB_SUCC(ret)) { value = &storage.value_; }
  return ret;
}

static int publish_legacy_aggr(const ObExpr &expr, ObEvalCtx &ctx,
                               const common::ObDatum &value)
{
  Datum native;
  int ret = expr.get_datum_copy_plan().import_legacy(value, native);
  if (OB_SUCC(ret)) { ret = expr.deep_copy_datum(ctx, native); }
  return ret;
}

// One allocation per selected batch, never per row. Each selected value has its
// own coefficient/scalar scratch, valid until all synchronous math calls finish.
class LegacyAggrBatch : public common::ObDatumVector
{
public:
  template <typename Selector>
  int prepare_selected(const ObExpr &expr, ObEvalCtx &ctx, const Selector &selector)
  {
    int ret = OB_SUCCESS;
    const DatumVector native = expr.locate_expr_datumvector(ctx);
    int64_t count = 1;
    if (native.is_batch()) {
      for (auto it = selector.begin(); it < selector.end(); selector.next(it)) {
        count = MAX(count, static_cast<int64_t>(selector.get_batch_index(it)) + 1);
      }
    }
    if (OB_FAIL(cells_.prepare_allocate(count))) {
    } else if (OB_FAIL(scratch_.prepare_allocate(count))) {
    } else {
      datums_ = cells_.get_data();
      set_batch(native.is_batch());
      for (int64_t i = 0; i < count; ++i) { cells_.at(i).set_null(); }
      const common::DatumCopyPlan plan = expr.get_datum_copy_plan();
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        const int64_t index = mask_ & selector.get_batch_index(it);
        ret = plan.legacy_view(*native.at(index), cells_.at(index),
                              scratch_.at(index).bytes_, sizeof(Scratch));
      }
    }
    return ret;
  }
private:
  struct Scratch { alignas(8) char bytes_[64]; };
  common::ObSEArray<common::ObDatum, 16> cells_;
  common::ObSEArray<Scratch, 16> scratch_;
};

template <typename String>
static int pack_legacy_aggr_json(const ObExpr &expr, ObEvalCtx &ctx,
                                 common::ObDatum &result, String &text, ObIAllocator *owner)
{
  Datum native;
  LegacyAggrValue legacy;
  int ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, native, text, owner);
  if (OB_SUCC(ret)) { ret = legacy.prepare(expr, native); }
  if (OB_SUCC(ret)) { result = legacy.value_; } // JSON/text payload is owner allocated.
  return ret;
}

static int add_legacy_aggr_vector(ObExecContext &ctx, const ObExpr &expr,
                                  common::ObDatum &result, const common::ObDatum &input,
                                  ObIAllocator &owner, common::ObDatum *temporary = nullptr,
                                  bool negative = false)
{
  const DatumCopyPlan plan = expr.get_datum_copy_plan();
  Datum native_result, native_input, native_temporary;
  alignas(8) char result_scratch[64], temporary_scratch[64];
  int ret = plan.import_legacy(result, native_result);
  if (OB_SUCC(ret)) { ret = plan.import_legacy(input, native_input); }
  if (OB_SUCC(ret)) {
    ret = ObArrayExprUtils::vector_datum_add(ctx, native_result, native_input, owner,
                                            temporary != nullptr ? &native_temporary : nullptr, negative);
  }
  // Collection SQL values are reference spans; these views borrow the existing
  // accumulator or the vector library's owner allocation, never these scratch bytes.
  if (OB_SUCC(ret)) { ret = plan.legacy_view(native_result, result, result_scratch, sizeof(result_scratch)); }
  if (OB_SUCC(ret) && temporary != nullptr) {
    ret = plan.legacy_view(native_temporary, *temporary, temporary_scratch, sizeof(temporary_scratch));
  }
  return ret;
}

OB_SERIALIZE_MEMBER(HashRollupRTInfo, rollup_grouping_id_, expand_exprs_, gby_exprs_, dup_expr_pairs_);


OB_DEF_SERIALIZE(ObAggrInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              expr_,
              real_aggr_type_,
              has_distinct_,
              is_implicit_first_aggr_,
              has_order_by_,
              param_exprs_,
              distinct_collations_,
              distinct_cmp_funcs_,
              group_concat_param_count_,
              sort_collations_,
              sort_cmp_funcs_,
              separator_expr_,
              window_size_param_expr_,
              item_size_param_expr_,
              is_need_deserialize_row_,
              pl_agg_udf_type_id_,
              pl_agg_udf_params_type_,
              pl_result_type_,
              bucket_num_param_expr_,
              rollup_idx_,
              grouping_idxs_,
              group_idxs_,
              format_json_,
              strict_json_,
              absent_on_null_,
              returning_type_,
              with_unique_keys_,
              max_disuse_param_expr_
  );
  OB_UNIS_ENCODE(distinct_hash_funcs_);
  int8_t grouping_with_hash_rollup = 0;
  if (hash_rollup_info_ != nullptr) {
    grouping_with_hash_rollup = 1;
    OB_UNIS_ENCODE(grouping_with_hash_rollup);
    OB_UNIS_ENCODE(*hash_rollup_info_);
  } else {
    OB_UNIS_ENCODE(grouping_with_hash_rollup);
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObAggrInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              expr_,
              real_aggr_type_,
              has_distinct_,
              is_implicit_first_aggr_,
              has_order_by_,
              param_exprs_,
              distinct_collations_,
              distinct_cmp_funcs_,
              group_concat_param_count_,
              sort_collations_,
              sort_cmp_funcs_,
              separator_expr_,
              window_size_param_expr_,
              item_size_param_expr_,
              is_need_deserialize_row_,
              pl_agg_udf_type_id_,
              pl_agg_udf_params_type_,
              pl_result_type_,
              bucket_num_param_expr_,
              rollup_idx_,
              grouping_idxs_,
              group_idxs_,
              format_json_,
              strict_json_,
              absent_on_null_,
              returning_type_,
              with_unique_keys_,
              max_disuse_param_expr_
  );
  OB_UNIS_DECODE(distinct_hash_funcs_);
  int8_t grouping_with_hash_rollup = 0;
  OB_UNIS_DECODE(grouping_with_hash_rollup);
  if (OB_SUCC(ret) && grouping_with_hash_rollup) {
    CK(NULL != alloc_);
    if (OB_SUCC(ret)) {
      hash_rollup_info_ = OB_NEWx(HashRollupRTInfo, alloc_, (*alloc_));
      if (OB_ISNULL(hash_rollup_info_)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        OB_UNIS_DECODE(*hash_rollup_info_);
      }
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObAggrInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              expr_,
              real_aggr_type_,
              has_distinct_,
              is_implicit_first_aggr_,
              has_order_by_,
              param_exprs_,
              distinct_collations_,
              distinct_cmp_funcs_,
              group_concat_param_count_,
              sort_collations_,
              sort_cmp_funcs_,
              separator_expr_,
              window_size_param_expr_,
              item_size_param_expr_,
              is_need_deserialize_row_,
              pl_agg_udf_type_id_,
              pl_agg_udf_params_type_,
              pl_result_type_,
              bucket_num_param_expr_,
              rollup_idx_,
              grouping_idxs_,
              group_idxs_,
              format_json_,
              strict_json_,
              absent_on_null_,
              returning_type_,
              with_unique_keys_,
              max_disuse_param_expr_
  );
  OB_UNIS_ADD_LEN(distinct_hash_funcs_);
  int8_t grouping_id_with_hash_rollup = 0;
  OB_UNIS_ADD_LEN(grouping_id_with_hash_rollup);
  if (hash_rollup_info_ != nullptr) {
    OB_UNIS_ADD_LEN(*hash_rollup_info_);
  }
  return len;
}

ObAggrInfo::~ObAggrInfo()
{
}

int64_t ObAggrInfo::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(N_AGGR_FUNC, get_type_name(get_expr_type()),
       K_(real_aggr_type),
       KPC_(expr),
       N_DISTINCT, has_distinct_,
       K_(has_order_by),
       K_(is_implicit_first_aggr),
       K_(param_exprs),
       K_(distinct_collations),
       K_(distinct_cmp_funcs),
       K_(group_concat_param_count),
       K_(sort_collations),
       K_(sort_cmp_funcs),
       KPC_(separator_expr),
       KPC_(window_size_param_expr),
       KPC_(item_size_param_expr),
       K_(is_need_deserialize_row),
       K_(pl_agg_udf_type_id),
       K_(pl_result_type)
       );
  J_OBJ_END();
  return pos;
}

int ObAggrInfo::assign(const ObAggrInfo &rhs)
{
  int ret = OB_SUCCESS;
  set_allocator(rhs.alloc_);
  expr_ = rhs.expr_;
  real_aggr_type_ = rhs.real_aggr_type_;
  has_distinct_ = rhs.has_distinct_;
  is_implicit_first_aggr_ = rhs.is_implicit_first_aggr_;
  has_order_by_ = rhs.has_order_by_;

  group_concat_param_count_ = rhs.group_concat_param_count_;

  separator_expr_ = rhs.separator_expr_;
  window_size_param_expr_ = rhs.window_size_param_expr_;
  item_size_param_expr_ = rhs.item_size_param_expr_;
  is_need_deserialize_row_ = rhs.is_need_deserialize_row_;
  pl_agg_udf_type_id_ = rhs.pl_agg_udf_type_id_;

  pl_result_type_ = rhs.pl_result_type_;
  bucket_num_param_expr_ = rhs.bucket_num_param_expr_;
  rollup_idx_ = rhs.rollup_idx_;

  format_json_ = rhs.format_json_;
  strict_json_ = rhs.strict_json_;
  absent_on_null_ = rhs.absent_on_null_;
  returning_type_ = rhs.returning_type_;
  with_unique_keys_ = rhs.with_unique_keys_;
  max_disuse_param_expr_ = rhs.max_disuse_param_expr_;
  hash_rollup_info_ = nullptr;
  if (OB_FAIL(param_exprs_.assign(rhs.param_exprs_))) {
  } else if (OB_FAIL(distinct_collations_.assign(rhs.distinct_collations_))) {
  } else if (OB_FAIL(distinct_cmp_funcs_.assign(rhs.distinct_cmp_funcs_))) {
  } else if (OB_FAIL(sort_collations_.assign(rhs.sort_collations_))) {
  } else if (OB_FAIL(sort_cmp_funcs_.assign(rhs.sort_cmp_funcs_))) {
  } else if (OB_FAIL(pl_agg_udf_params_type_.assign(rhs.pl_agg_udf_params_type_))) {
  } else if (OB_FAIL(grouping_idxs_.assign(rhs.grouping_idxs_))) {
  } else if (OB_FAIL(group_idxs_.assign(rhs.group_idxs_))) {
  } else if (OB_FAIL(distinct_hash_funcs_.assign(rhs.distinct_hash_funcs_))) {
  } else if (rhs.hash_rollup_info_ != nullptr) {
    hash_rollup_info_ = OB_NEWx(HashRollupRTInfo, alloc_, (*alloc_));
    if (OB_ISNULL(hash_rollup_info_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(hash_rollup_info_->assign(*rhs.hash_rollup_info_))) {
    }
  }
  return ret;
}

int HashRollupRTInfo::assign(const HashRollupRTInfo &other)
{
  int ret = OB_SUCCESS;
  rollup_grouping_id_ = other.rollup_grouping_id_;
  if (OB_FAIL(expand_exprs_.assign(other.expand_exprs_))) {
  } else if (OB_FAIL(gby_exprs_.assign(other.gby_exprs_))) {
  } else if (OB_FAIL(dup_expr_pairs_.assign(other.dup_expr_pairs_))) {
  }
  return ret;
}

ObAggregateProcessor::AggrCell::~AggrCell()
{
  destroy();
}

// arena allocator alloc memory, it don't free
void ObAggregateProcessor::AggrCell::destroy()
{
  clear_iter_value();
  iter_buffer_ = nullptr;
  iter_capacity_ = 0;
  advance_collect_result_.set_null();
  collect_buf_ = nullptr;
  collect_buf_len_ = 0;
  if (NULL != extra_) {
    auto *&extra = extra_;
    extra->~ExtraResult();
    extra_ = NULL;
  }
}

int ObAggregateProcessor::AggrCell::reserve_iter_payload(int64_t minimum,
    ObIAllocator &owner, char *&payload, int64_t &capacity)
{
  constexpr int64_t prefix = 2 * sizeof(int64_t);
  if (minimum < 0 || minimum > INT64_MAX / 2 - prefix) { return OB_SIZE_OVERFLOW; }
  if (iter_buffer_ != nullptr) {
    int64_t stored_capacity = 0, magic = 0;
    MEMCPY(&stored_capacity, iter_buffer_, sizeof(stored_capacity));
    MEMCPY(&magic, iter_buffer_ + sizeof(int64_t), sizeof(magic));
    if (stored_capacity != iter_capacity_ + prefix || magic != STORED_ROW_MAGIC_NUM
        || (iter_result_.is_present() && iter_result_.length() > iter_capacity_)) {
      return OB_ERR_UNEXPECTED;
    }
  }
  if (minimum > iter_capacity_ || (minimum > 0 && iter_buffer_ == nullptr)) {
    const int64_t requested = minimum + prefix;
    const int64_t allocated = iter_buffer_ == nullptr ? requested : requested * 2;
    char *candidate = static_cast<char *>(owner.alloc(allocated));
    if (candidate == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    MEMCPY(candidate, &allocated, sizeof(allocated));
    const int64_t magic = STORED_ROW_MAGIC_NUM;
    MEMCPY(candidate + sizeof(int64_t), &magic, sizeof(magic));
    if (iter_result_.is_present() && iter_result_.length() > 0) {
      MEMMOVE(candidate + prefix, iter_result_.get_reference(), iter_result_.length());
      const int ret = iter_result_.set_reference(candidate + prefix, iter_result_.length(),
                                                 iter_result_.reference_flags());
      OB_ASSERT(ret == OB_SUCCESS); // same already validated length and flags
    }
    iter_buffer_ = candidate;
    iter_capacity_ = allocated - prefix;
  }
  payload = iter_buffer_ == nullptr ? nullptr : iter_buffer_ + prefix;
  capacity = iter_capacity_;
  return OB_SUCCESS;
}

int ObAggregateProcessor::AggrCell::copy_semantic_iter(const Datum &source,
    const SqlType &input_type, ObIAllocator &owner)
{
  const Datum value = source;
  const DatumCopyPlan plan(input_type);
  int64_t bytes = 0;
  int ret = plan.carrier_span_size(value, bytes);
  char *buffer = iter_buffer_ == nullptr ? nullptr : iter_buffer_ + 2 * sizeof(int64_t);
  int64_t capacity = iter_capacity_;
  if (OB_FAIL(ret)) {
  } else if (bytes > 0 && OB_FAIL(reserve_iter_payload(
      (input_type.kind() == ObNumberType || input_type.kind() == ObUNumberType)
          ? std::max<int64_t>(bytes, static_cast<int64_t>(number::ObNumber::MAX_BYTE_LEN)) : bytes,
      owner, buffer, capacity))) {
  } else {
    Datum candidate;
    int64_t pos = 0;
    if (OB_FAIL(plan.copy_carrier_span_borrowing_resources(value, candidate, buffer, capacity, pos))) {
    } else {
      iter_result_ = candidate;
      iter_layout_ = IterKernelLayout(IterValueDomain::SemanticValue);
    }
  }
  return ret;
}

int ObAggregateProcessor::AggrCell::export_semantic_iter(const SqlType &input_type,
    ObDatum &output, void *scratch, int64_t capacity) const
{
  return iter_layout_.domain_ != IterValueDomain::SemanticValue && !iter_result_.is_null()
      ? OB_STATE_NOT_MATCH
      : DatumCopyPlan(input_type).legacy_view(iter_result_, output, scratch, capacity);
}

static int prepare_kernel_iteration(const ObDatum &value,
    const ObAggregateProcessor::IterKernelLayout &layout, Datum &candidate, int64_t &minimum)
{
  using IterValueDomain = ObAggregateProcessor::IterValueDomain;
  SqlTypeMeta physical;
  int ret = OB_SUCCESS;
  minimum = value.len_;
  if (layout.coefficient_bytes_ != 0 && layout.domain_ != IterValueDomain::DecimalCoefficient) {
    ret = OB_INVALID_ARGUMENT;
  } else if (layout.register_bytes_ != 0 && layout.domain_ != IterValueDomain::HllRegisters) {
    ret = OB_INVALID_ARGUMENT;
  } else if (layout.domain_ == IterValueDomain::Float32) {
    physical.set_type(ObFloatType);
  } else if (layout.domain_ == IterValueDomain::Float64) {
    physical.set_type(ObDoubleType);
  } else if (layout.domain_ == IterValueDomain::Number) {
    physical.set_type(ObNumberType);
    minimum = std::max<int64_t>(minimum, static_cast<int64_t>(number::ObNumber::MAX_BYTE_LEN));
  } else if (layout.domain_ == IterValueDomain::DecimalCoefficient) {
    if (layout.coefficient_bytes_ != 4 && layout.coefficient_bytes_ != 8
        && layout.coefficient_bytes_ != 16 && layout.coefficient_bytes_ != 32
        && layout.coefficient_bytes_ != 64) { ret = OB_INVALID_ARGUMENT; }
    physical.set_decimal_int(0); // physical coefficient validation, not a SQL declaration
    if (!value.is_null() && value.len_ != layout.coefficient_bytes_) { ret = OB_INVALID_ARGUMENT; }
  } else if (layout.domain_ == IterValueDomain::HllRegisters) {
    physical.set_type(ObVarcharType); // opaque physical bytes, never a semantic SQL declaration
    physical.set_collation_type(CS_TYPE_BINARY);
    ObDatumDesc maximum;
    maximum.pack_ = UINT32_MAX;
    if (layout.register_bytes_ < ObAggregateProcessor::get_llc_size()
        || layout.register_bytes_ > maximum.len_
        || (!value.is_null() && (value.len_ != layout.register_bytes_ || value.ptr_ == nullptr))) {
      ret = OB_INVALID_ARGUMENT;
    }
  } else { ret = OB_INVALID_ARGUMENT; }
  if (OB_FAIL(ret)) {
  } else if (value.is_null()) {
    candidate.set_null();
  } else if (value.is_ext()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = DatumParameterCodec::borrow_schema_native_value(value, physical, candidate);
  }
  return ret;
}

int ObAggregateProcessor::AggrCell::copy_kernel_iter(const ObDatum &source,
    const IterKernelLayout &layout, ObIAllocator &owner)
{
  const ObDatum value = source;
  Datum candidate;
  int64_t minimum = 0;
  int ret = prepare_kernel_iteration(value, layout, candidate, minimum);
  if (OB_SUCC(ret) && !candidate.is_null() && layout.domain_ != IterValueDomain::Float32
      && layout.domain_ != IterValueDomain::Float64) {
    char *buffer = nullptr;
    int64_t capacity = 0;
    if (OB_FAIL(reserve_iter_payload(minimum, owner, buffer, capacity))) {
    } else {
      if (value.len_ > 0 && buffer != value.ptr_) { MEMMOVE(buffer, value.ptr_, value.len_); }
      ret = candidate.set_reference(buffer, value.len_, candidate.reference_flags());
    }
  }
  if (OB_SUCC(ret)) { iter_result_ = candidate; iter_layout_ = layout; }
  return ret;
}
int ObAggregateProcessor::AggrCell::commit_owned_kernel_iter(const ObDatum &source,
    const IterKernelLayout &layout)
{
  Datum candidate;
  int64_t minimum = 0;
  int ret = prepare_kernel_iteration(source, layout, candidate, minimum);
  if (OB_SUCC(ret) && !candidate.is_null() && candidate.length() > 0) {
    // In-place math can publish only this cell's reserved actual span.
    if (iter_buffer_ == nullptr || source.ptr_ != iter_buffer_ + 2 * sizeof(int64_t)
        || source.len_ > iter_capacity_) { ret = OB_INVALID_ARGUMENT; }
  }
  if (OB_SUCC(ret)) { iter_result_ = candidate; iter_layout_ = layout; }
  return ret;
}


int ObAggregateProcessor::AggrCell::export_kernel_iter(const IterKernelLayout &layout,
    ObDatum &output, void *scratch, int64_t capacity) const
{
  if (!iter_result_.is_null() && !(layout == iter_layout_)) { return OB_STATE_NOT_MATCH; }
  ObDatum candidate;
  int ret = OB_SUCCESS;
  if (iter_result_.is_null()) { candidate.set_null(); }
  else if (layout.domain_ == IterValueDomain::Float32 || layout.domain_ == IterValueDomain::Float64) {
    SqlTypeMeta physical;
    physical.set_type(layout.domain_ == IterValueDomain::Float32 ? ObFloatType : ObDoubleType);
    ret = DatumParameterCodec::export_native_value(iter_result_, physical, candidate, scratch, capacity);
  } else if (layout.domain_ == IterValueDomain::Number
             || layout.domain_ == IterValueDomain::DecimalCoefficient
             || layout.domain_ == IterValueDomain::HllRegisters) {
    SqlTypeMeta physical;
    if (layout.domain_ == IterValueDomain::Number) { physical.set_type(ObNumberType); }
    else if (layout.domain_ == IterValueDomain::DecimalCoefficient) { physical.set_decimal_int(0); }
    else { physical.set_type(ObVarcharType); physical.set_collation_type(CS_TYPE_BINARY); }
    ret = DatumParameterCodec::export_native_value(iter_result_, physical, candidate, scratch, capacity);
  } else { ret = OB_INVALID_ARGUMENT; }
  if (OB_SUCC(ret)) { output = candidate; }
  return ret;
}

int ObAggregateProcessor::AggrCell::reserve_kernel_iter(const IterKernelLayout &layout,
    int64_t minimum, ObIAllocator &owner, char *&payload, int64_t &capacity)
{
  if ((layout.domain_ != IterValueDomain::Number
       && layout.domain_ != IterValueDomain::DecimalCoefficient
       && layout.domain_ != IterValueDomain::HllRegisters) || minimum < 0) {
    return OB_INVALID_ARGUMENT;
  }
  ObDatum null_value;
  null_value.set_null();
  Datum ignored;
  int64_t required = 0;
  int ret = prepare_kernel_iteration(null_value, layout, ignored, required);
  if (OB_SUCC(ret)) {
    required = std::max<int64_t>(required, layout.domain_ == IterValueDomain::DecimalCoefficient
        ? layout.coefficient_bytes_ : layout.register_bytes_);
    ret = reserve_iter_payload(max(minimum, required), owner, payload, capacity);
  }
  return ret;
}

int ObAggregateProcessor::AggrCell::export_iter(const SqlType *semantic_type,
    ObDatum &output, void *scratch, int64_t capacity) const
{
  if (iter_layout_.domain_ == IterValueDomain::SemanticValue) {
    return semantic_type == nullptr ? OB_NOT_INIT
        : export_semantic_iter(*semantic_type, output, scratch, capacity);
  }
  return export_kernel_iter(iter_layout_, output, scratch, capacity);
}

static int export_hll_iteration(const ObAggregateProcessor::AggrCell &cell, ObDatum &output)
{
  using Domain = ObAggregateProcessor::IterValueDomain;
  // SYNOPSIS_MERGE historically accepts a register prefix followed by extra
  // bytes. Preserve that actual span; HLL math still consumes only the prefix.
  const auto layout = cell.native_iter_result().is_null()
      ? ObAggregateProcessor::IterKernelLayout(Domain::HllRegisters, 0, ObAggregateProcessor::get_llc_size())
      : cell.iter_layout();
  return layout.domain_ != Domain::HllRegisters ? OB_STATE_NOT_MATCH
      : cell.export_kernel_iter(layout, output, nullptr, 0);
}

static const SqlType *semantic_iteration_type(const ObAggrInfo &info)
{
  const ObExpr *producer = info.is_implicit_first_aggr() ? info.expr_
      : info.param_exprs_.count() > 0 ? info.param_exprs_.at(0) : nullptr;
  return producer == nullptr ? nullptr : &producer->get_sql_type();
}

static int publish_iter_result(const ObAggrInfo &info,
    const ObAggregateProcessor::AggrCell &cell, ObEvalCtx &ctx)
{
  alignas(8) char scratch[64];
  ObDatum value;
  int ret = cell.export_iter(semantic_iteration_type(info), value, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = publish_legacy_aggr(*info.expr_, ctx, value); }
  return ret;
}

int ObAggregateProcessor::reserve_kernel_cell(AggrCell &cell,
    const IterKernelLayout &layout, int64_t bytes, ObDatum &view)
{
  char *payload = nullptr;
  int64_t capacity = 0;
  int ret = cell.reserve_kernel_iter(layout, bytes, aggr_alloc_, payload, capacity);
  if (OB_SUCC(ret)) {
    // Old math writes this synchronous descriptor, never a resident shadow.
    // NULL may reserve capacity without becoming a Present value.
    view.ptr_ = payload;
  }
  return ret;
}

int ObAggregateProcessor::clone_aggr_cell(AggrCell &cell, const ObDatum &source,
    const SqlType &actual_input_type)
{
  Datum value;
  int ret = DatumCopyPlan(actual_input_type).import_legacy(source, value);
  if (OB_SUCC(ret)) { ret = cell.copy_semantic_iter(value, actual_input_type, aggr_alloc_); }
  return ret;
}

int ObAggregateProcessor::clone_kernel_cell(AggrCell &cell, const ObDatum &source,
    const IterKernelLayout &layout, ObDatum *view, void *scratch, int64_t capacity)
{
  // A requested scalar view must be exportable before publishing the cell.
  const int64_t scalar_bytes = layout.domain_ == IterValueDomain::Float32 ? sizeof(float)
      : layout.domain_ == IterValueDomain::Float64 ? sizeof(double) : 0;
  int ret = OB_SUCCESS;
  if (view != nullptr && !source.is_null() && scalar_bytes > 0
      && (scratch == nullptr || capacity < scalar_bytes)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(cell.copy_kernel_iter(source, layout, aggr_alloc_))) {
  } else if (view != nullptr) {
    ret = cell.export_kernel_iter(layout, *view, scratch, capacity);
  }
  return ret;
}

// This cache belongs to the same execution/domain as the producing Expr.
// Copy reference wrapper bytes, preserving the existing pointee borrowing rule.
// Complete preflight precedes writes; inline values and NULL need no allocation.
static int copy_final_result(const Datum &source, const DatumCopyPlan &plan,
    ObIAllocator &alloc, Datum &target, char *&buffer, int64_t &capacity,
    const int64_t minimum_capacity = 0)
{
  int ret = OB_SUCCESS;
  const Datum value = source; // source may alias target
  Datum candidate;
  int64_t span_size = 0;
  int64_t pos = 0;
  char *candidate_buffer = buffer;
  int64_t candidate_capacity = capacity;
  bool allocated = false;
  if (OB_FAIL(plan.carrier_span_size(value, span_size))) {
  } else {
    if (span_size > 0 && (buffer == nullptr || capacity < span_size)) {
      candidate_capacity = next_pow2(max(span_size, minimum_capacity));
      candidate_buffer = static_cast<char *>(alloc.alloc(candidate_capacity));
      if (OB_ISNULL(candidate_buffer)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        allocated = true;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(plan.copy_carrier_span_borrowing_resources(
        value, candidate, candidate_buffer, candidate_capacity, pos))) {
    } else {
      target = candidate;
      buffer = candidate_buffer;
      capacity = candidate_capacity;
    }
  }
  if (OB_FAIL(ret) && allocated) {
    alloc.free(candidate_buffer);
  }
  return ret;
}

int ObAggregateProcessor::AggrCell::deep_copy_advance_collect_result(
    const Datum &datum, const DatumCopyPlan &plan, ObIAllocator &alloc)
{
  return copy_final_result(datum, plan, alloc, advance_collect_result_, collect_buf_, collect_buf_len_);
}

int ObAggregateProcessor::AggrCell::collect_result(
  const SqlTypeClass tc, ObEvalCtx &eval_ctx, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  alignas(8) char iter_scratch[64];
  ObDatum iteration;
  if (OB_FAIL(export_iter(semantic_iteration_type(aggr_info), iteration, iter_scratch, sizeof(iter_scratch)))) {
    return ret;
  }
  alignas(8) char result_buffer[ObNumber::MAX_CALC_BYTE_LEN + 64];
  common::ObDatum result;
  result.ptr_ = result_buffer;
  // sum(count(*)) should return same type as count(*), which is bigint in mysql mode
  if (T_FUN_COUNT_SUM == aggr_info.get_expr_type()) {
    result.set_int(tiny_num_int_);
    if (ObIntTC != tc) {
      ret = OB_ERR_UNEXPECTED;
    }
  } else if (is_tiny_num_used_ && (ObIntTC == tc || ObUIntTC == tc)) {
    if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
      const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
      if (iteration.is_null()) {
        if (ObIntTC == tc && OB_FAIL(DecIntAggFuncCtx::int_to_decimalint(tiny_num_int_,
                                                                         precision, result))) {
        } else if (ObUIntTC == tc && OB_FAIL(DecIntAggFuncCtx::int_to_decimalint(tiny_num_uint_,
                                                                                 precision,
                                                                                 result))) {
        }
      } else {
        // need add iteration
        if (ObIntTC == tc
              && OB_FAIL(DecIntAggFuncCtx::add_int(iteration, precision, tiny_num_int_))) {
        } else if (ObUIntTC == tc
                && OB_FAIL(DecIntAggFuncCtx::add_int(iteration, precision, tiny_num_uint_))) {
        } else {
          ret = commit_owned_kernel_iter(iteration,
              IterKernelLayout(IterValueDomain::DecimalCoefficient, iteration.len_));
          if (OB_SUCC(ret)) {
            result.set_decimal_int(iteration.get_decimal_int(), iteration.len_);
            tiny_num_uint_ = 0;
          }
        }
      }
    } else {
      ObNumStackAllocator<2> tmp_alloc;
      ObNumber result_nmb;
      const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
      ObNumber right_nmb;
      if (ObIntTC == tc) {
        if (OB_FAIL(right_nmb.from(tiny_num_int_, tmp_alloc))) {
        }
      } else {
        if (OB_FAIL(right_nmb.from(tiny_num_uint_, tmp_alloc))) {
        }
      }

      if (OB_SUCC(ret)) {
        if (iteration.is_null()) {
          result.set_number(right_nmb);
        } else {
          ObNumber left_nmb(iteration.get_number());
          if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, strict_mode))) {
          } else {
            result.set_number(result_nmb);
          }
        }
      }
    }
  } else if (ObDecimalIntTC == tc) {
    if (OB_UNLIKELY(iteration.is_null())) {
      result.set_null();
    } else {
      result.set_decimal_int(iteration.get_decimal_int(), iteration.len_);
      if (OB_UNLIKELY(aggr_info.expr_->datum_meta_.precision_ >=
            OB_MAX_DECIMAL_POSSIBLE_PRECISION)) {
        check_mysql_decimal_int_overflow(result); // no ret
      }
    }
  } else {
    result = iteration;
  }
  if (OB_SUCC(ret)) { ret = publish_legacy_aggr(*aggr_info.expr_, eval_ctx, result); }
  if (OB_SUCC(ret)) { aggr_info.expr_->set_evaluated_projected(eval_ctx); }
  return ret;
}

int64_t ObAggregateProcessor::AggrCell::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(row_count),
       K_(tiny_num_int),
       K_(tiny_num_uint),
       K_(is_tiny_num_used),
       K_(need_advance_collect),
       K_(iter_result),
       KPC_(extra));
  J_OBJ_END();
  return pos;
}

void ObAggregateProcessor::ExtraResult::reuse()
{
  if (NULL != unique_sort_op_) {
    unique_sort_op_->reuse();
  }
}

ObAggregateProcessor::ExtraResult::~ExtraResult()
{
  if (NULL != unique_sort_op_) {
    unique_sort_op_->~ObUniqueSortImpl();
    alloc_.free(unique_sort_op_);
    unique_sort_op_ = NULL;
  }
}

int ObAggregateProcessor::ExtraResult::init_distinct_set(const ObAggrInfo &aggr_info, ObEvalCtx &eval_ctx, const bool need_rewind,
    ObIOEventObserver *io_event_observer)
{
  int ret = OB_SUCCESS;
  need_rewind_ = need_rewind;
  if (OB_ISNULL(unique_sort_op_ = static_cast<ObUniqueSortImpl *>(
      alloc_.alloc(sizeof(ObUniqueSortImpl))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    new (unique_sort_op_) ObUniqueSortImpl(op_monitor_info_);
    if (OB_FAIL(unique_sort_op_->init(&aggr_info.distinct_collations_,
                                      &aggr_info.distinct_cmp_funcs_,
                                      &eval_ctx,
                                      &eval_ctx.exec_ctx_,
                                      need_rewind,
                                      (4L << 10) /* 4kb */))) {
    } else {
      unique_sort_op_->set_io_event_observer(io_event_observer);
      is_inited_ = true;
    }
  }

  if (OB_FAIL(ret)) {
    if (NULL != unique_sort_op_) {
      unique_sort_op_->~ObUniqueSortImpl();
      alloc_.free(unique_sort_op_);
      unique_sort_op_ = NULL;
    }
  }
  return ret;
}

int ObAggregateProcessor::ObSigResultHolder::init(const common::ObIArray<ObExpr *> &exprs,
                                                  ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    // do nothing
  } else {
    exprs_ = &exprs;
    eval_ctx_ = &eval_ctx;
    datums_ = static_cast<Datum *>(
      eval_ctx.exec_ctx_.get_allocator().alloc(exprs.count() * sizeof(*datums_)));
    if (OB_ISNULL(datums_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < exprs.count(); ++i) { new (datums_ + i) Datum; }
      inited_ = true;
    }
  }
  return ret;
}

int ObAggregateProcessor::ObSigResultHolder::save()
{
  int ret = OB_SUCCESS;
  if (NULL == datums_) {
    ret = OB_NOT_INIT;
  } else {
    for (int64_t i = 0; i < exprs_->count(); i++) {
      ObExpr *e = exprs_->at(i);
      datums_[i] = e->locate_expr_datum(*eval_ctx_);
    }
  }
  return ret;
}

int ObAggregateProcessor::ObSigResultHolder::restore()
{
  int ret = OB_SUCCESS;
  if (NULL == datums_) {
    ret = OB_NOT_INIT;
  } else {
    for (int64_t i = 0; i < exprs_->count(); i++) {
      ObExpr *e = exprs_->at(i);
      e->locate_expr_datum(*eval_ctx_) = datums_[i];
    }
  }
  return ret;
}

void ObAggregateProcessor::HashBasedDistinctExtraResult::reuse()
{
  if (nullptr != hp_infras_ && hp_infras_mgr_->is_inited()) {
    hp_infras_mgr_->free_one_hp_infras(hp_infras_);
  }
  flags_ = 0;
  hp_infras_ = nullptr;
  brs_holder_.reset();
  ExtraResult::reuse();
  int ret = OB_SUCCESS;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::rewind()
{
  int ret = OB_SUCCESS;
  if (nullptr != hp_infras_ && need_rewind_) {
    if (OB_FAIL(hp_infras_->rewind())) {
    } else {
      got_row_ = false;
    }
  } else if (nullptr != unique_sort_op_ && need_rewind_) {
    unique_sort_op_->rewind();
  }
  return ret;
}

ObAggregateProcessor::HashBasedDistinctExtraResult::~HashBasedDistinctExtraResult()
{
  reuse();
  if (nullptr != hash_values_for_batch_) {
    alloc_.free(hash_values_for_batch_);
    hash_values_for_batch_ = nullptr;
  }
  if (nullptr != my_skip_) {
    alloc_.free(my_skip_);
    my_skip_ = nullptr;
  }
  brs_holder_.destroy();
  hp_infras_ = nullptr;
  aggr_info_ = nullptr;
  hp_infras_mgr_ = nullptr;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::init_my_skip(const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  void *data = nullptr;
  if (OB_ISNULL(data = alloc_.alloc(ObBitVector::memory_size(batch_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    my_skip_ = to_bit_vector(data);
    my_skip_->reset(batch_size);
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::init_hp_infras()
{
  int ret = OB_SUCCESS;
  if (inited_hp_infras_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(hp_infras_mgr_->init_one_hp_infras(need_rewind_, &aggr_info_->distinct_collations_,
    &aggr_info_->distinct_cmp_funcs_, &aggr_info_->distinct_hash_funcs_, hp_infras_))) {
  } else {
    inited_hp_infras_ = true;
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::init_distinct_set(
    const ObAggrInfo &aggr_info, const bool need_rewind,
    HashPartInfrasMgr &hp_infras_mgr, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  hp_infras_mgr_ = &hp_infras_mgr;
  aggr_info_ = &aggr_info;
  need_rewind_ = need_rewind;
  
  if (!hp_infras_mgr.is_inited()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(srs_holder_.init(aggr_info.param_exprs_, eval_ctx))) {
  } else if (eval_ctx.max_batch_size_ > 0) {
    if (OB_ISNULL(hash_values_for_batch_
                  = static_cast<uint64_t *> (alloc_.alloc(eval_ctx.max_batch_size_ * sizeof(uint64_t))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(init_my_skip(eval_ctx.max_batch_size_))) {
    } else if (OB_FAIL(brs_holder_.init(aggr_info.param_exprs_, eval_ctx, &alloc_))) {
    }
  }
  if (OB_FAIL(ret)) {
    if (nullptr != hash_values_for_batch_) {
      alloc_.free(hash_values_for_batch_);
      hash_values_for_batch_ = nullptr;
    }
    if (nullptr != my_skip_) {
      alloc_.free(my_skip_);
      my_skip_ = nullptr;
    }
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::insert_row(
    const common::ObIArray<ObExpr*> &exprs)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  bool inserted = false;
  if (!inited_hp_infras_ && OB_FAIL(init_hp_infras())) {
  } else if (OB_FAIL(hp_infras_->insert_row(exprs, exists, inserted))) {
  } else {
    LOG_DEBUG("succ to insert row", K(exists), K(inserted));
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::insert_row_for_batch(
    const common::ObIArray<ObExpr*> &exprs,
    const int64_t batch_size,
    const ObBitVector *skip,
    const int64_t start_pos)
{
  int ret = OB_SUCCESS;
  ObBitVector *output_vec = nullptr;
  if (OB_ISNULL(my_skip_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (start_pos > 0) {
    my_skip_->deep_copy(*skip, batch_size);
    for (int64_t i = 0; i < start_pos; i++) {
      my_skip_->set(i);
    }
  }
  if (OB_FAIL(ret)) {
  } else if (!inited_hp_infras_ && OB_FAIL(init_hp_infras())) {
  } else if (OB_FAIL(hp_infras_->calc_hash_value_for_batch(exprs,
                                                    batch_size,
                                                    start_pos > 0 ? my_skip_ : skip,
                                                    hash_values_for_batch_))) {
  } else if (OB_FAIL(hp_infras_->insert_row_for_batch(exprs,
                                                      hash_values_for_batch_,
                                                      batch_size,
                                                      start_pos > 0 ? my_skip_ : skip,
                                                      output_vec))) {
  } else {
    // int64_t got_rows = batch_size - output_vec->accumulate_bit_cnt(batch_size);
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::build_distinct_data(
    const common::ObIArray<ObExpr*> &exprs)
{
  int ret = OB_SUCCESS;
  bool exists = false;
  bool inserted = false;
  int64_t n_times = 0;
  const ObChunkDatumStore::StoredRow *store_row = nullptr;
  auto try_check_status = [&] () -> int {
    return ((++n_times) % 1024 == 0)
      ? THIS_WORKER.check_status()
      : common::OB_SUCCESS;
  };
  while (OB_SUCC(ret)) {
    ret = hp_infras_->get_left_next_row(store_row, exprs);
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      if (OB_FAIL(hp_infras_->finish_insert_row())) {
      } else if (OB_FAIL(hp_infras_->close_cur_part(InputSide::LEFT))) {
      } else {
        break;
      }
    } else if (OB_FAIL(ret)) {
    } else if (OB_FAIL(try_check_status())) {
    } else if (OB_FAIL(hp_infras_->insert_row(exprs, exists, inserted))) {
    }
  } //end of while
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::build_distinct_data_for_batch(
    const common::ObIArray<ObExpr*> &exprs,
    const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  int64_t read_rows = -1;
  int64_t n_times = 0;
  ObBitVector *output_vec = nullptr;
  auto try_check_status = [&] () -> int {
    return ((++n_times) % 1024 == 0)
      ? THIS_WORKER.check_status()
      : common::OB_SUCCESS;
  };
  while (OB_SUCC(ret)) {
    ret = hp_infras_->get_left_next_batch(exprs, batch_size, read_rows, hash_values_for_batch_);
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      if (OB_FAIL(hp_infras_->finish_insert_row())) {
      } else if (OB_FAIL(hp_infras_->close_cur_part(InputSide::LEFT))) {
      } else {
        break;
      }
    } else if (OB_FAIL(ret)) {
    } else if (OB_FAIL(try_check_status())) {
    } else if (OB_FAIL(hp_infras_->insert_row_for_batch(exprs,
                                                        hash_values_for_batch_,
                                                        read_rows,
                                                        nullptr,
                                                        output_vec))) {
    }
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::get_next_unique_hash_table_row(
    const ObChunkDatumStore::StoredRow *&store_row,
    const common::ObIArray<ObExpr*> *exprs)
{
  int ret = OB_SUCCESS;
  if (!got_row_) {
    if (!inited_hp_infras_ && OB_FAIL(init_hp_infras())) {
    } else if (OB_FAIL(hp_infras_->finish_insert_row())) {
    } else if (OB_FAIL(hp_infras_->open_hash_table_part())) {
    } else {
      got_row_ = true;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(hp_infras_->get_next_hash_table_row(store_row, exprs))) {
    if (OB_ITER_END == ret) {
      if (OB_FAIL(hp_infras_->end_round())) {
      } else if (OB_FAIL(hp_infras_->start_round())) {
      } else if (OB_FAIL(hp_infras_->get_next_partition(InputSide::LEFT))) {
        if (OB_ITER_END != ret) {
        }
      } else if (OB_FAIL(hp_infras_->open_cur_part(InputSide::LEFT))) {
      } else if (OB_FAIL(hp_infras_->resize(
          hp_infras_->get_cur_part_row_cnt(InputSide::LEFT)))) {
      } else if (OB_FAIL(build_distinct_data(*exprs))) {
        if (OB_ITER_END == ret) {
          ret = OB_ERR_UNEXPECTED;
        }
      } else if (OB_FAIL(hp_infras_->open_hash_table_part())) {
      } else if (OB_FAIL(SMART_CALL(get_next_unique_hash_table_row(store_row, exprs)))) {
      }
    } else {
    }
  }
  return ret;
}

int ObAggregateProcessor::HashBasedDistinctExtraResult::get_next_unique_hash_table_batch(
    const common::ObIArray<ObExpr *> &exprs,
    const int64_t max_row_cnt,
    int64_t &read_rows)
{
  int ret = OB_SUCCESS;
  if (!got_row_) {
    if (!inited_hp_infras_ && OB_FAIL(init_hp_infras())) {
    } else if (OB_FAIL(hp_infras_->finish_insert_row())) {
    } else if (OB_FAIL(hp_infras_->open_hash_table_part())) {
    } else {
      got_row_ = true;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(hp_infras_->get_next_hash_table_batch(exprs,
                                                           max_row_cnt,
                                                           read_rows,
                                                           nullptr))) {
    if (OB_ITER_END == ret) {
      if (OB_FAIL(hp_infras_->end_round())) {
      } else if (OB_FAIL(hp_infras_->start_round())) {
      } else if (OB_FAIL(hp_infras_->get_next_partition(InputSide::LEFT))) {
        if (OB_ITER_END != ret) {
        }
      } else if (OB_FAIL(hp_infras_->open_cur_part(InputSide::LEFT))) {
      } else if (OB_FAIL(hp_infras_->resize(
          hp_infras_->get_cur_part_row_cnt(InputSide::LEFT)))) {
      } else if (OB_FAIL(build_distinct_data_for_batch(exprs, max_row_cnt))) {
        if (OB_ITER_END == ret) {
          ret = OB_ERR_UNEXPECTED;
        }
      } else if (OB_FAIL(hp_infras_->open_hash_table_part())) {
      } else if (OB_FAIL(SMART_CALL(get_next_unique_hash_table_batch(exprs, max_row_cnt, read_rows)))) {
      }
    } else {
    }
  }
  return ret;
}

int ObAggregateProcessor::GroupConcatExtraResult::init(const ObAggrInfo &aggr_info, ObEvalCtx &eval_ctx, const bool need_rewind, int64_t dir_id,
    ObIOEventObserver *io_event_observer)
{
  int ret = OB_SUCCESS;
  {
    row_count_ = 0;
    iter_idx_ = 0;
    need_rewind_ = need_rewind;

    if (aggr_info.has_order_by_) {
      if (OB_ISNULL(sort_op_ = static_cast<ObSortOpImpl *>(alloc_.alloc(sizeof(ObSortOpImpl))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        new (sort_op_) ObSortOpImpl(op_monitor_info_);
        if (OB_FAIL(sort_op_->init(&aggr_info.sort_collations_,
                                   &aggr_info.sort_cmp_funcs_,
                                   &eval_ctx,
                                   &eval_ctx.exec_ctx_,
                                   false,
                                   false,
                                   need_rewind))) {
        } else {
          sort_op_->set_io_event_observer(io_event_observer);
        }
      }
    } else {
      int64_t sort_area_size = 0;
      if (OB_FAIL(ObSqlWorkareaUtil::get_workarea_size(
                  SORT_WORK_AREA, &eval_ctx.exec_ctx_, sort_area_size))) {
      } else if (OB_FAIL(row_store_.init(sort_area_size,
                                         ObCtxIds::WORK_AREA,
                                         ObModIds::OB_SQL_AGGR_FUN_GROUP_CONCAT,
                                         true /* enable dump */))) {
      } else {
        row_store_.set_dir_id(dir_id);
        row_store_.set_io_event_observer(io_event_observer);
      }
    }
  }

  if (OB_FAIL(ret)) {
    if (NULL != sort_op_) {
      sort_op_->~ObSortOpImpl();
      alloc_.free(sort_op_);
      sort_op_ = NULL;
    }
  }
  return ret;
}

int ObAggregateProcessor::GroupConcatExtraResult::copy_separator(
    const Datum &source, const DatumCopyPlan &plan)
{
  int64_t bytes = 0;
  int ret = plan.carrier_span_size(source, bytes);
  char *buffer = nullptr;
  if (OB_FAIL(ret)) {
  } else if (bytes > 0 && OB_ISNULL(buffer = static_cast<char *>(alloc_.alloc(bytes)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    Datum candidate;
    int64_t pos = 0;
    if (OB_FAIL(plan.copy_carrier_span_borrowing_resources(source, candidate, buffer, bytes, pos))) {
      if (buffer != nullptr) { alloc_.free(buffer); }
    } else {
      separator_datum_ = candidate;
      has_separator_ = true;
    }
  }
  return ret;
}

int ObAggregateProcessor::GroupConcatExtraResult::export_separator(
    const DatumCopyPlan &plan, ObDatum &output, void *scratch, int64_t capacity) const
{
  return !has_separator_ ? OB_NOT_INIT
      : plan.legacy_view(separator_datum_, output, scratch, capacity);
}

ObAggregateProcessor::GroupConcatExtraResult::~GroupConcatExtraResult()
{
  if (sort_op_ != NULL) {
    sort_op_->~ObSortOpImpl();
    alloc_.free(sort_op_);
    sort_op_ = NULL;
  } else {
    row_store_iter_.reset();
    row_store_.reset();
  }
}

void ObAggregateProcessor::GroupConcatExtraResult::reuse_self()
{
  if (sort_op_ != NULL) {
    sort_op_->reuse();
  } else {
    row_store_iter_.reset();
    row_store_.reset();
   }
  bool_mark_.reset();
  row_count_ = 0;
  iter_idx_ = 0;
};

void ObAggregateProcessor::GroupConcatExtraResult::reuse()
{
  reuse_self();
  HashBasedDistinctExtraResult::reuse();
}

int ObAggregateProcessor::GroupConcatExtraResult::finish_add_row()
{
  iter_idx_ = 0;
  int ret = OB_SUCCESS;
  if (sort_op_ != NULL) {
    ret = sort_op_->sort();
  } else {
    row_store_iter_.reset();
    ret = row_store_iter_.init(&row_store_);
  }
  return ret;
}

int ObAggregateProcessor::GroupConcatExtraResult::rewind()
{
  int ret = OB_SUCCESS;
  if (sort_op_ != NULL) {
    if (OB_FAIL(sort_op_->rewind())) {
    }
  } else {
    row_store_iter_.reset();
    if (OB_FAIL(row_store_iter_.init(&row_store_))) {
    }
  }
  iter_idx_ = 0;
  return ret;
}

int64_t ObAggregateProcessor::GroupConcatExtraResult::to_string(char *buf,
    const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(row_count),
       K_(iter_idx),
       K_(row_store),
       KP_(sort_op)
       );
  J_OBJ_END();
  return pos;
}

int ObAggregateProcessor::GroupConcatExtraResult::get_bool_mark(int64_t col_index, bool &is_bool)
{
  INIT_SUCC(ret);
  if (col_index + 1 > bool_mark_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_bool = bool_mark_[col_index];
  }
  return ret;
}

int ObAggregateProcessor::GroupConcatExtraResult::set_bool_mark(int64_t col_index, bool is_bool)
{
  INIT_SUCC(ret);
  if (col_index > bool_mark_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (col_index == bool_mark_.count()) {
    bool_mark_.push_back(is_bool);
  } else {
    bool_mark_[col_index] = is_bool;
  }
  return ret;
}

ObAggregateProcessor::HybridHistExtraResult::~HybridHistExtraResult()
{
  if (sort_op_ != NULL) {
    sort_op_->~ObSortOpImpl();
    alloc_.free(sort_op_);
    sort_op_ = NULL;
  }
  if (mat_op_ != NULL) {
    mat_op_->~ObMaterialOpImpl();
    alloc_.free(mat_op_);
    mat_op_ = NULL;
  }
}

void ObAggregateProcessor::HybridHistExtraResult::reuse_self()
{
  if (sort_op_ != NULL) {
    sort_op_->reuse();
  }
  if (mat_op_ != NULL) {
    mat_op_->reuse();
  }
  sort_row_count_ = 0;
  material_row_count_ = 0;
};

void ObAggregateProcessor::HybridHistExtraResult::reuse()
{
  reuse_self();
  ExtraResult::reuse();
}

int ObAggregateProcessor::HybridHistExtraResult::init(const ObAggrInfo &aggr_info, ObEvalCtx &eval_ctx, const bool need_rewind, 
    ObIOEventObserver *io_event_observer,
    ObMonitorNode &op_monitor_info)
{
  int ret = OB_SUCCESS;
  sort_row_count_ = 0;
  material_row_count_ = 0;
  {
    if (OB_ISNULL(sort_op_ = static_cast<ObSortOpImpl *>(alloc_.alloc(sizeof(ObSortOpImpl))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fall to alloc buff", "size", sizeof(ObSortOpImpl));
    } else {
      new (sort_op_) ObSortOpImpl(op_monitor_info_);
      if (OB_FAIL(sort_op_->init(&aggr_info.sort_collations_,
                                 &aggr_info.sort_cmp_funcs_,
                                 &eval_ctx,
                                 &eval_ctx.exec_ctx_,
                                 false,
                                 false,
                                 need_rewind))) {
      } else {
        sort_op_->set_io_event_observer(io_event_observer);
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(mat_op_ = static_cast<ObMaterialOpImpl *>(alloc_.alloc(sizeof(ObMaterialOpImpl))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fall to alloc buff", "size", sizeof(ObMaterialOpImpl));
    } else {
      new (mat_op_) ObMaterialOpImpl(op_monitor_info);
      if (OB_FAIL(mat_op_->init(&eval_ctx,
                                &eval_ctx.exec_ctx_,
                                io_event_observer))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
    if (NULL != sort_op_) {
      sort_op_->~ObSortOpImpl();
      alloc_.free(sort_op_);
      sort_op_ = NULL;
    }
    if (NULL != mat_op_) {
      mat_op_->~ObMaterialOpImpl();
      alloc_.free(mat_op_);
      mat_op_ = NULL;
    }
  }
  return ret;
}

int ObAggregateProcessor::HybridHistExtraResult::add_sort_row(
    const ObIArray<ObExpr *> &expr, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sort_op_)) {
    if (OB_FAIL(sort_op_->add_row(expr))) {
    } else {
      ++sort_row_count_;
    }
  }
  return ret;
}

int ObAggregateProcessor::HybridHistExtraResult::add_sort_row(
    const ObChunkDatumStore::StoredRow &sr)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sort_op_)) {
    if (OB_FAIL(sort_op_->add_stored_row(sr))) {
    } else {
      ++sort_row_count_;
    }
  }
  return ret;
}

int ObAggregateProcessor::HybridHistExtraResult::get_next_row_from_sort(
    const ObChunkDatumStore::StoredRow *&sr)
{
  int ret = OB_SUCCESS;
  sr = NULL;
  if (OB_NOT_NULL(sort_op_)) {
    ret = sort_op_->get_next_row(sr);
  }
  return ret;
}

int ObAggregateProcessor::HybridHistExtraResult::finish_add_sort_row()
{
  
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(sort_op_)) {
    if (OB_FAIL(sort_op_->sort())) {
    }
  }
  return ret;
}

int ObAggregateProcessor::HybridHistExtraResult::add_material_row(
    const ObChunkDatumStore::StoredRow &src_row,
    const int64_t extra_size,
    const ObChunkDatumStore::StoredRow *&store_row)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(mat_op_)) {
    // Hybrid histogram has exactly one endpoint column. The legacy material
    // input copies this single local descriptor synchronously; neither the
    // descriptor nor an array view of packed stored cells can escape.
    if (OB_UNLIKELY(src_row.cnt_ != 1)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const ObDatum endpoint = StoredDatumCodec::decode_legacy_view(src_row.stored_cells()[0]);
      if (OB_FAIL(mat_op_->add_row(&endpoint, 1, extra_size, store_row))) {
      } else {
        ++material_row_count_;
      }
    }
  }
  return ret;
}
int ObAggregateProcessor::HybridHistExtraResult::get_next_row_from_material(
    const ObChunkDatumStore::StoredRow *&sr)
{
  int ret = OB_SUCCESS;
  sr = NULL;
  if (OB_NOT_NULL(mat_op_)) {
    ret = mat_op_->get_next_row(sr);
  }
  return ret;
}

int ObAggregateProcessor::HybridHistExtraResult::finish_add_material_row()
{
  
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(mat_op_)) {
    if (OB_FAIL(mat_op_->finish_add_row())) {
    }
  }
  return ret;
}

int64_t ObAggregateProcessor::HybridHistExtraResult::to_string(
    char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(sort_row_count),
       K_(material_row_count),
       KP_(sort_op),
       KP_(mat_op)
      );
  J_OBJ_END();
  return pos;
}

int64_t ObAggregateProcessor::ExtraResult::to_string(char *buf,
    const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  if (nullptr != unique_sort_op_) {
    J_KV(KP_(unique_sort_op));
  }
  J_OBJ_END();
  return pos;
}

int64_t ObAggregateProcessor::HashBasedDistinctExtraResult::to_string(char *buf,
    const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(got_row));
  J_KV(K_(need_rewind));
  if (nullptr != hp_infras_) {
    J_KV(KP_(hp_infras));
  }
  J_OBJ_END();
  return pos;
}

int ObAggrInfo::eval_aggr(ObChunkDatumStore::ShadowStoredRow &curr_row_results,
    ObEvalCtx &ctx) const
{
  int ret = OB_SUCCESS;
  if (param_exprs_.empty() && T_FUN_COUNT == get_expr_type()) {
    //do nothing
  } else {
    ObChunkDatumStore::StoredRow *store_row = curr_row_results.get_store_row();
    if (OB_ISNULL(curr_row_results.get_store_row())) {
      ret = common::OB_ERR_UNEXPECTED;
      SQL_ENG_LOG(WARN, "NULL datums or count mismatch", K(ret),
                  KPC(store_row), K(param_exprs_.count()));
    } else {
      Datum *datum = nullptr;
      StoredDatumCell *cells = store_row->stored_cells();
      store_row->cnt_ = param_exprs_.count();
      for (int64_t i = 0; OB_SUCC(ret) && i < param_exprs_.count(); i++) {
        if (OB_FAIL(param_exprs_.at(i)->eval(ctx, datum))) {
        } else {
          common::ObDatum legacy;
          char *scratch = store_row->payload_ + sizeof(StoredDatumCell) * param_exprs_.count() + i * 64;
          if (OB_FAIL(param_exprs_.at(i)->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, 64))) {
          } else {
            cells[i] = StoredDatumCodec::encode_legacy_view(legacy);
          }
        }
      }
    }
  }
  return ret;
}

int ObAggrInfo::eval_param_batch(const ObBatchRows &brs, ObEvalCtx &ctx) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < param_exprs_.count(); i++) {
    if (OB_FAIL(param_exprs_.at(i)->eval_batch(ctx, *(brs.skip_), brs.size_))) {
    }
  }
  return ret;
}

ObAggregateProcessor::ObAggregateProcessor(ObEvalCtx &eval_ctx,
                                           ObIArray<ObAggrInfo> &aggr_infos,
                                           const lib::ObLabel &label,
                                           ObMonitorNode &op_monitor_info)
    : has_distinct_(false),
      has_order_by_(false),
      has_group_concat_(false),
      in_window_func_(false),
      has_extra_(false),
      eval_ctx_(eval_ctx),
      datum_access_ctx_(nullptr),
      aggr_alloc_(label,
                  common::OB_MALLOC_MIDDLE_BLOCK_SIZE,
                  ObCtxIds::WORK_AREA),
      cur_batch_group_idx_(0),
      cur_batch_group_buf_(nullptr),
      aggr_infos_(aggr_infos),
      aggr_func_ctxs_(eval_ctx.exec_ctx_.get_allocator()),
      group_rows_(),
      concat_str_max_len_(OB_DEFAULT_EXTENDED_GROUP_CONCAT_MAX_LEN),
      cur_concat_buf_len_(0),
      concat_str_buf_(NULL),
      skip_(nullptr),
      aggr_code_idx_(OB_INVALID_INDEX_INT64),
      distinct_aggr_count_(0),
      aggr_code_expr_(nullptr),
      aggr_stage_(ObThreeStageAggrStage::NONE_STAGE),
      dir_id_(-1),
      tmp_store_row_(nullptr),
      io_event_observer_(nullptr),
      removal_info_(),
      support_fast_single_row_agg_(false),
      op_eval_infos_(nullptr),
      op_monitor_info_(op_monitor_info),
      need_advance_collect_(false),
      distinct_count_(0),
      hp_infras_mgr_(nullptr),
      enable_hash_distinct_(false)
{
}

int ObAggregateProcessor::init()
{
  int ret = OB_SUCCESS;
  // add aggr columns
  has_distinct_ = false;
  has_order_by_ = false;
  has_group_concat_ = false;
  distinct_count_ = 0;
  removal_info_.reset();
  (void)0;
  
  group_rows_.set_ctx_id(ObCtxIds::DEFAULT_CTX_ID);

  if (OB_ISNULL(eval_ctx_.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(eval_ctx_.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_FAIL(group_rows_.reserve(1))) {
  } else if (OB_FAIL(aggr_func_ctxs_.prepare_allocate(aggr_infos_.count()))) {
  }
  int64_t max_param_expr_cnt = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(i);
    max_param_expr_cnt = std::max(max_param_expr_cnt, aggr_info.get_child_output_count());
    if (OB_ISNULL(aggr_info.expr_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      has_distinct_ |= aggr_info.has_distinct_;
      has_group_concat_ |= (T_FUN_GROUP_CONCAT == aggr_info.get_expr_type() ||
                            T_FUN_WM_CONCAT == aggr_info.get_expr_type() ||
                            T_FUN_JSON_ARRAYAGG == aggr_info.get_expr_type() ||
                            T_FUN_ORA_JSON_ARRAYAGG == aggr_info.get_expr_type() ||
                            T_FUN_JSON_OBJECTAGG == aggr_info.get_expr_type() ||
                            T_FUN_ORA_JSON_OBJECTAGG == aggr_info.get_expr_type());
      has_order_by_ |= aggr_info.has_order_by_;
      if (!has_extra_) {
        has_extra_ |= aggr_info.has_distinct_;
        has_extra_ |= need_extra_info(aggr_info.get_expr_type());
      }
      if (aggr_info.has_distinct_) {
        ++distinct_count_;
      }

      if (T_FUN_MEDIAN == aggr_info.get_expr_type()
          || T_FUN_GROUP_PERCENTILE_CONT == aggr_info.get_expr_type()) {
        // ObAggregateProcessor::init would be invoked many times under groupby rescan
        // Only create LinearInterAggrFuncCtx once to prevent memory leak.
        //
        // Details:
        // Normally ObAggregateProcessor::init would ONLY be invoked once under open
        // stage and NEVER be triggered any more. Typically window function follows 
        // this rule.
        // However, ObAggregateProcessor::init would be invoked many times under groupby
        // rescan cases, see ObGroupByOp::inner_rescan. And LinearInterAggrFuncCtx would
        // be created repeatedly. This break init semantic(invoked once) and leading
        // memory leak. 
        //
        // Solution:
        // Only create LinearInterAggrFuncCtx once when ObAggregateProcessor::init is called
        // repeatedly. So both window function and groupby cases would be well handled.
        //
        // TODO qubin.qb:
        // Refactor group by rescan API to stop calling ObAggregateProcessor::init so that 
        // init semantic (only invoke one time) would be strictly followed
        if (aggr_func_ctxs_.at(i) == nullptr) {
          LinearInterAggrFuncCtx *ctx = OB_NEWx(LinearInterAggrFuncCtx,
                                                (&eval_ctx_.exec_ctx_.get_allocator()));
          if (NULL == ctx) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            aggr_func_ctxs_.at(i) = ctx;
          }
        }
      } else if (T_FUN_SUM == aggr_info.get_expr_type() && !aggr_info.is_implicit_first_aggr()
                   && OB_ISNULL(aggr_func_ctxs_.at(i))) {
        const bool is_decint_type_arg = ob_is_decimal_int_tc(aggr_info.get_first_child_type());
        const bool is_decint_type_res = ob_is_int_uint_tc(aggr_info.get_first_child_type())
                     && ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_);
        if (is_decint_type_arg || is_decint_type_res) {
          DecIntAggFuncCtx *ctx = OB_NEWx(DecIntAggFuncCtx, (&eval_ctx_.exec_ctx_.get_allocator()));
          if (NULL == ctx) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else if (is_decint_type_arg) {
            ret = ctx->init(aggr_info, eval_ctx_.max_batch_size_);
          } else { // the args type is int/uint, and result type is decimal int
            const int64_t res_prec = aggr_info.expr_->datum_meta_.precision_;
            const int res_type = static_cast<int>(get_decimalint_type(res_prec));
            if (OB_UNLIKELY(res_prec <= MAX_PRECISION_DECIMAL_INT_64
                              || res_prec > MAX_PRECISION_DECIMAL_INT_256)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_ISNULL(ctx->merge_func = DEC_INT_MERGE_FUNCS[res_type][0])) {// for rollup
              ret = OB_ERR_UNEXPECTED;
            }
          }
          if (OB_SUCC(ret)) {
            aggr_func_ctxs_.at(i) = ctx;
          }
        }
      }
    }
  } // end for

  // vector in case
  if (eval_ctx_.is_vectorized()) {
    void * data = nullptr;
    if (OB_ISNULL(data = aggr_alloc_.alloc(ObBitVector::memory_size(eval_ctx_.max_batch_size_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      skip_ = to_bit_vector(data);
      skip_->reset(eval_ctx_.max_batch_size_);
    }
  }
  if (OB_FAIL(ret) || OB_NOT_NULL(tmp_store_row_)) {
  } else if (OB_ISNULL(tmp_store_row_
                = static_cast<ObChunkDatumStore::ShadowStoredRow *>
                  (eval_ctx_.exec_ctx_.get_allocator()
                                      .alloc(sizeof(ObChunkDatumStore::ShadowStoredRow))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    new (tmp_store_row_) ObChunkDatumStore::ShadowStoredRow();
    if (OB_FAIL(tmp_store_row_->init(eval_ctx_.exec_ctx_.get_allocator(), max_param_expr_cnt))) {
    }
  }
  return ret;
}

void ObAggregateProcessor::destroy()
{
  for (int64_t i = 0; i < group_rows_.count(); ++i) {
    group_rows_.at(i)->destroy();
  }
  if (concat_str_buf_ != NULL) {
    aggr_alloc_.free(concat_str_buf_);
    concat_str_buf_ = NULL;
  }
  if (nullptr != skip_) {
    aggr_alloc_.free(skip_);
    skip_ = NULL;
  }
  if (nullptr != tmp_store_row_) {
    tmp_store_row_->reset();
    tmp_store_row_ = nullptr;
  }
  group_rows_.reset();
  cur_batch_group_idx_ = 0;
  cur_batch_group_buf_ = nullptr;
  aggr_alloc_.reset();

  FOREACH_CNT(it, aggr_func_ctxs_) {
    if (NULL != *it) {
      (*it)->~IAggrFuncCtx();
      *it = NULL;
    }
  }
  aggr_func_ctxs_.reset();
  removal_info_.reset();
}

void ObAggregateProcessor::reuse()
{
  if (has_extra_) {
    // Only extra information needs to be cleaned up, otherwise aggr_alloc_ directly resets the memory to release it
    for (int64_t i = 0; i < group_rows_.count(); ++i) {
      group_rows_.at(i)->destroy();
    }
  }
  if (concat_str_buf_ != NULL) {
    aggr_alloc_.free(concat_str_buf_);
    concat_str_buf_ = NULL;
  }
  if (nullptr != skip_) {
    aggr_alloc_.free(skip_);
    skip_ = NULL;
  }
  cur_concat_buf_len_ = 0;
  group_rows_.reuse();
  cur_batch_group_idx_ = 0;
  cur_batch_group_buf_ = nullptr;
  need_advance_collect_ = false;
  aggr_alloc_.reset_remain_one_page();
  removal_info_.reset();
}

int ObAggregateProcessor::clone_number_cell(const ObNumber &src_number, AggrCell &cell, ObDatum *view)
{
  alignas(8) char scratch[number::ObNumber::MAX_CALC_BYTE_LEN + sizeof(number::ObNumber::Desc)];
  if (src_number.get_length() > number::ObNumber::OB_CALC_BUFFER_SIZE) { return OB_INVALID_ARGUMENT; }
  ObDatum source;
  source.ptr_ = scratch;
  source.set_number(src_number);
  return clone_kernel_cell(cell, source, IterKernelLayout(IterValueDomain::Number), view);
}

int ObAggregateProcessor::clone_vector_cell(const ObDatum &source, AggrCell &cell,
    const SqlType &actual_input_type, ObDatum *view, void *scratch, int64_t capacity)
{
  // CopyPlan requires caller-owned scratch even for a Reference view. Validate
  // it before capture; a callee-local scalar buffer must never escape here.
  int ret = OB_SUCCESS;
  if (view != nullptr && capacity < 64) { ret = OB_BUF_NOT_ENOUGH; }
  else if (view != nullptr && (scratch == nullptr || capacity > UINT32_MAX)) { ret = OB_INVALID_ARGUMENT; }
  else if (OB_FAIL(clone_aggr_cell(cell, source, actual_input_type))) {
  } else if (view != nullptr) {
    ret = cell.export_semantic_iter(actual_input_type, *view, scratch, capacity);
  }
  return ret;
}

int ObAggregateProcessor::prepare(GroupRow &group_row)
{
  int ret = OB_SUCCESS;
  // init aggr_info
  for (int64_t i = 0; OB_SUCC(ret) && i < group_row.n_cells_; ++i) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(i);
    AggrCell &aggr_cell = group_row.aggr_cells_[i];
    if (aggr_info.has_distinct_) {
      HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
      //only last one add distinct
      if (OB_ISNULL(ad_result) || !ad_result->is_inited()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (T_FUN_TOP_FRE_HIST == aggr_info.get_expr_type()) {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "distinct on topk fre hist");
      }
    }
  }
  // for sort-based group by operator, for performance reason,
  // after producing a group, we will invoke reuse_group() function to clear the group
  // thus, we do not need to allocate the group space again here, simply reuse the space
  // process aggregate columns
  if (OB_SUCC(ret)) {
    if (OB_FAIL(process(group_row, true))) {
    }
  }
  return ret;
}

int ObAggregateProcessor::inner_process(
  GroupRow &group_row, int64_t start_idx, int64_t end_idx, bool is_prepare)
{
  int ret = OB_SUCCESS;
  // for sort-based group by operator, for performance reason,
  // after producing a group, we will invoke reuse_group() function to clear the group
  // thus, we do not need to allocate the group space again here, simply reuse the space
  // process aggregate columns
  for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(i);
    AggrCell &aggr_cell = group_row.aggr_cells_[i];
    if (OB_ISNULL(aggr_info.expr_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (aggr_info.is_implicit_first_aggr()) {
      if (!aggr_cell.get_is_evaluated()) {
        Datum *result = nullptr;
        LegacyAggrValue legacy;
        if (OB_FAIL(aggr_info.expr_->eval(eval_ctx_, result))) {
        } else if (OB_FAIL(legacy.prepare(*aggr_info.expr_, *result))) {
        } else if (OB_FAIL(clone_aggr_cell(aggr_cell, legacy.value_, aggr_info.expr_->get_sql_type()))) {
        } else {
          aggr_cell.set_is_evaluated(true);
        }
        LOG_DEBUG("debug prepare implicit aggr", K(ret), K(i),
          K(EXPR2STR(eval_ctx_, *aggr_info.expr_)));
      } else {
        LOG_DEBUG("debug prepare implicit aggr", K(ret), K(i),
          K(EXPR2STR(eval_ctx_, *aggr_info.expr_)));
      }
    } else if (OB_FAIL(aggr_info.eval_aggr(*tmp_store_row_, eval_ctx_))) {
    } else {
      if (aggr_info.has_distinct_) {
        HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
        //only last one add distinct
        if (enable_hash_distinct_) {
          if (OB_FAIL(ad_result->insert_row(aggr_info.param_exprs_))) {
          }
        } else {
          if (OB_FAIL(ad_result->unique_sort_op_->add_row(aggr_info.param_exprs_))) {
          }
        }
      } else {
        if (is_prepare && OB_FAIL(prepare_aggr_result(*tmp_store_row_->get_store_row(),
                                        &(aggr_info.param_exprs_),
                                        aggr_cell, aggr_info))) {
        } else if (!is_prepare && OB_FAIL(process_aggr_result(*tmp_store_row_->get_store_row(),
                                        &(aggr_info.param_exprs_),
                                        aggr_cell, aggr_info))) {
        }
      }
    }
    OX(LOG_DEBUG("finish prepare", K(aggr_cell)));
  } // end for
  return ret;
}

int ObAggregateProcessor::process(GroupRow &group_row, bool is_prepare)
{
  int ret = OB_SUCCESS;
  int64_t start_idx = 0;
  int64_t end_idx = group_row.n_cells_;
  if (ObThreeStageAggrStage::NONE_STAGE != aggr_stage_) {
    int64_t aggr_code = -1;
    // get aggr_code and then decide to go which path
    if (ObThreeStageAggrStage::THIRD_STAGE == aggr_stage_) {
      Datum *datum = nullptr;
      if (OB_FAIL(aggr_code_expr_->eval(eval_ctx_, datum))) {
      } else {
        aggr_code = datum->get_int();
      }
    } else if (ObThreeStageAggrStage::SECOND_STAGE == aggr_stage_) {
      // only process one group and only process one aggregate function
      if (OB_INVALID_INDEX_INT64 == aggr_code_idx_) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        aggr_code = StoredDatumCodec::decode_legacy_view(group_row.groupby_store_row_->stored_cells()[aggr_code_idx_]).get_int();
      }
    }
    if (OB_SUCC(ret) && -1 != aggr_code) {
      if (aggr_code >= distinct_aggr_count_) {
        start_idx = distinct_aggr_count_;
        if (aggr_code != distinct_aggr_count_) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(dist_aggr_group_idxes_) || 0 == start_idx) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          start_idx = dist_aggr_group_idxes_->at(distinct_aggr_count_ - 1);
        }
      } else {
        start_idx = aggr_code;
        end_idx = aggr_code + 1;
        if (OB_ISNULL(dist_aggr_group_idxes_)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          start_idx = 0 == start_idx ? 0 : dist_aggr_group_idxes_->at(aggr_code - 1);
          end_idx = dist_aggr_group_idxes_->at(aggr_code);
        }
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(inner_process(group_row, start_idx, end_idx, is_prepare))) {
  }
  return ret;
}

int ObAggregateProcessor::collect(const int64_t group_id/*= 0*/,
    const ObExpr *diff_expr /*= NULL*/,
    const int64_t max_group_cnt /*= INT64_MIN*/)
{
  int ret = OB_SUCCESS;
  GroupRow *group_row = NULL;
  if (OB_FAIL(group_rows_.at(group_id, group_row))) {
  } else if (OB_FAIL(collect_group_row(group_row, group_id, diff_expr, max_group_cnt))) {
  }
  return ret;
}

int ObAggregateProcessor::collect_group_row(GroupRow *group_row,
                                            const int64_t group_id /*= 0*/,
                                            const ObExpr *diff_expr /*= NULL*/,
                                            const int64_t max_group_cnt /*= INT64_MIN*/)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(group_row));
  for (int64_t i = 0; OB_SUCC(ret) && i < group_row->n_cells_; ++i) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(i);
    AggrCell &aggr_cell = group_row->aggr_cells_[i];
    if (OB_ISNULL(aggr_info.expr_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (aggr_info.is_implicit_first_aggr()) {
      if (OB_FAIL(publish_iter_result(aggr_info, aggr_cell, eval_ctx_))) {
      } else { aggr_info.expr_->set_evaluated_projected(eval_ctx_); }
      LOG_DEBUG("debug implicit first aggr", K(ret), K(i),
        K(EXPR2STR(eval_ctx_, *aggr_info.expr_)));
    } else {
      if (aggr_info.has_distinct_) {
        HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
        if (OB_ISNULL(ad_result)
          || !ad_result->is_inited()) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          if (group_id > 0) {
            // Group id greater than zero in sort based group by must be rollup,
            // distinct set is sorted and iterated in rollup_process(), rewind here.
            if (OB_FAIL(ad_result->rewind())) {
            }
          } else if (!enable_hash_distinct_) {
            if (OB_FAIL(ad_result->unique_sort_op_->sort())) {
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(process_aggr_result_from_distinct(aggr_cell, aggr_info))) {
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(collect_aggr_result(aggr_cell, diff_expr, aggr_info, group_id, max_group_cnt))) {
        }
      }
    }
    OX(LOG_DEBUG("finish collect", K(group_id), K(aggr_cell)));
  } // end for
  return ret;
}

int ObAggregateProcessor::eval_aggr_param_batch(const ObBatchRows &brs)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
    ObAggrInfo &aggr_info = aggr_infos_.at(i);
    if (OB_FAIL(aggr_info.eval_param_batch(brs, eval_ctx_))) {
    }
  }

  return ret;
}

int ObAggregateProcessor::prefetch_group_rows(GroupRow &group_rows)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < group_rows.n_cells_; ++i) {
    ObAggrInfo &aggr_info = aggr_infos_.at(i);
    AggrCell &aggr_cell = group_rows.aggr_cells_[i];
    const Datum &datum = aggr_cell.native_iter_result();
    if (datum.is_present() && datum.length() > 0) {
      __builtin_prefetch(datum.get_reference(), 0/* read */, 2 /*high temp locality*/);
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::inner_process_batch(
  GroupRow &group_rows, T &selector, int64_t start_idx, int64_t end_idx)
{
  int ret = OB_SUCCESS;
  // process aggregate columns
  for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
    ObAggrInfo &aggr_info = aggr_infos_.at(i);
    AggrCell &aggr_cell = group_rows.aggr_cells_[i];
    if (!aggr_info.is_implicit_first_aggr() && !aggr_info.has_distinct_) {
      if (OB_FAIL(process_aggr_batch_result(
              &(aggr_info.param_exprs_), aggr_cell, aggr_info, selector))) {
      }
    } else if (aggr_info.is_implicit_first_aggr()) {
      DatumVector results = aggr_info.expr_->locate_expr_datumvector(eval_ctx_);
      Datum *result = nullptr;
      LegacyAggrValue legacy;
      for (auto it = selector.begin();
           OB_SUCC(ret) && !aggr_cell.get_is_evaluated() && it < selector.end();
           selector.next(it)) {
        result = results.at(selector.get_batch_index(it));
        if (OB_FAIL(legacy.prepare(*aggr_info.expr_, *result))) {
        } else if (OB_FAIL(clone_aggr_cell(aggr_cell, legacy.value_, aggr_info.expr_->get_sql_type()))) {
        } else {
          aggr_cell.set_is_evaluated(true);
        }
      }
    } else if (aggr_info.has_distinct_) {
      HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
      //only last one add distinct
      if (OB_ISNULL(ad_result)
        || !ad_result->is_inited()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (T_FUN_TOP_FRE_HIST == aggr_info.get_expr_type()) {
        // parse should report error instead of runtime
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "distinct on topk fre hist");
      } else if (OB_FAIL(selector.add_batch(
          &aggr_info.param_exprs_, ad_result, nullptr, eval_ctx_))) {
      } else {
        LOG_DEBUG("batch process disticnt", K(ret));
      }
    }
  } // end for
  return ret;
}

template <typename T>
int ObAggregateProcessor::inner_process_three_stage_batch(GroupRow &group_row, T &selector)
{
  int ret = OB_SUCCESS;
  // process aggregate columns
  int64_t aggr_code = distinct_aggr_count_;
  int64_t start_idx = 0;
  int64_t end_idx = group_row.n_cells_;
  // get aggr_code and then decide to go which path
  if (ObThreeStageAggrStage::THIRD_STAGE == aggr_stage_) {
  } else if (OB_INVALID_INDEX_INT64 == aggr_code_idx_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ObThreeStageAggrStage::SECOND_STAGE == aggr_stage_) {
    // only process one group and only process one aggregate function
    aggr_code = StoredDatumCodec::decode_legacy_view(group_row.groupby_store_row_->stored_cells()[aggr_code_idx_]).get_int();
  }
  if (OB_FAIL(ret)) {
  } else if (aggr_code < distinct_aggr_count_) {
    if (ObThreeStageAggrStage::FIRST_STAGE == aggr_stage_) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // distinct aggregate function on first stage and second stage
      start_idx = 0 == aggr_code ? 0 : dist_aggr_group_idxes_->at(aggr_code - 1);
      end_idx = dist_aggr_group_idxes_->at(aggr_code);
    }
  } else if (aggr_code != distinct_aggr_count_) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // calc the non-distinct aggregate function in first stage and second stage
    // or calc all aggregate function and implicit aggregate functions on third stage
    start_idx = ObThreeStageAggrStage::SECOND_STAGE != aggr_stage_ ? 0 : aggr_code;
    start_idx = 0 == start_idx ? 0 : dist_aggr_group_idxes_->at(start_idx - 1);
  }
  if (OB_SUCC(ret) && OB_FAIL(inner_process_batch(group_row, selector, start_idx, end_idx))) {
  }
  return ret;
}

int ObAggregateProcessor::process_batch(
    const ObBatchRows *brs, GroupRow &group_rows, const uint16_t* selector_array, uint16_t count)
{
  int ret = OB_SUCCESS;
  if (ObThreeStageAggrStage::NONE_STAGE == aggr_stage_) {
    ObSelector selector(brs, selector_array, count);
    if (OB_FAIL(inner_process_batch(group_rows, selector, 0, group_rows.n_cells_))) {
    }
  } else {
    if (OB_UNLIKELY(ObThreeStageAggrStage::THIRD_STAGE == aggr_stage_)) {
      DatumVector aggr_code_datums = aggr_code_expr_->locate_expr_datumvector(eval_ctx_);
      int64_t i = 0;
      uint16_t tmp_selector_array;
      ObSelector selector(brs, &tmp_selector_array, 1);
      for (int64_t row_idx = 0; OB_SUCC(ret) && row_idx < count; ++row_idx) {
        int64_t i = selector_array[row_idx];
        (&tmp_selector_array)[0] = i;
        int64_t aggr_code = aggr_code_datums.at(i)->get_int();
        if (aggr_code < distinct_aggr_count_) {
          int64_t start_idx = 0 == aggr_code ? 0 : dist_aggr_group_idxes_->at(aggr_code - 1);
          int64_t end_idx = dist_aggr_group_idxes_->at(aggr_code);
          if (OB_FAIL(inner_process_batch(group_rows, selector, start_idx, end_idx))) {
          }
        } else {
          int64_t start_idx = dist_aggr_group_idxes_->at(distinct_aggr_count_ - 1);
          if (OB_FAIL(inner_process_batch(group_rows, selector, start_idx, group_rows.n_cells_))) {
          }
        }
      }
    } else {
      // only for first and second aggregate stage
      ObSelector selector(brs, selector_array, count);
      if (OB_FAIL(inner_process_three_stage_batch(group_rows, selector))) {
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::process_batch(
    GroupRow &group_rows, const ObBatchRows &brs, uint16_t begin, uint16_t end)
{
  int ret = OB_SUCCESS;
  if (ObThreeStageAggrStage::NONE_STAGE == aggr_stage_) {
    ObBatchRowsSlice brs_slice(&brs, begin, end);
    if (OB_FAIL(inner_process_batch(group_rows, brs_slice, 0, group_rows.n_cells_))) {
    }
  } else {
    if (ObThreeStageAggrStage::THIRD_STAGE == aggr_stage_) {
      DatumVector aggr_code_datums = aggr_code_expr_->locate_expr_datumvector(eval_ctx_);
      for (int64_t i = begin; OB_SUCC(ret) && i < end; ++i) {
        if (brs.skip_->at(i)) {
          continue;
        }
        int64_t aggr_code = aggr_code_datums.at(i)->get_int();
        if (aggr_code < distinct_aggr_count_) {
          ObBatchRowsSlice brs_slice(&brs, i, i + 1);
          int64_t start_idx = 0 == aggr_code ? 0 : dist_aggr_group_idxes_->at(aggr_code - 1);
          int64_t end_idx = dist_aggr_group_idxes_->at(aggr_code);
          if (OB_FAIL(inner_process_batch(group_rows, brs_slice, start_idx, end_idx))) {
          }
        } else {
          ObBatchRowsSlice brs_slice(&brs, i, i + 1);
          int64_t start_idx = dist_aggr_group_idxes_->at(distinct_aggr_count_ - 1);
          if (OB_FAIL(inner_process_batch(group_rows, brs_slice, start_idx, group_rows.n_cells_))) {
          }
        }
      }
    } else {
      ObBatchRowsSlice brs_slice(&brs, begin, end);
      if (OB_FAIL(inner_process_three_stage_batch(group_rows, brs_slice))) {
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::advance_collect_result(int64_t group_id)
{
  int ret = OB_SUCCESS;
  int64_t aggr_cnt = aggr_infos_.count();
  GroupRow *group_row = NULL;
  ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
  guard.set_batch_size(1);
  for (int64_t aggr_idx = 0; OB_SUCC(ret) && aggr_idx < aggr_cnt; ++aggr_idx) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(aggr_idx);
    group_row = group_rows_.at(group_id);
    AggrCell &aggr_cell = group_row->aggr_cells_[aggr_idx];
    if (aggr_cell.get_need_advance_collect()) {
      if (aggr_info.has_distinct_) {
        if (OB_FAIL(process_distinct_batch(group_id, aggr_cell, aggr_info,
                                           eval_ctx_.max_batch_size_))) {
        }
      }
      if (OB_SUCC(ret)) {
        guard.set_batch_idx(0);
        if (OB_FAIL(collect_aggr_result(aggr_cell, NULL, aggr_info))) {
        } else if (OB_FAIL(aggr_cell.deep_copy_advance_collect_result(
            aggr_info.expr_->locate_expr_datum(eval_ctx_), aggr_info.expr_->get_datum_copy_plan(), aggr_alloc_))) {
        } else {
          LOG_TRACE("finish collect", K(group_id), K(aggr_cell), K(aggr_cell.get_advance_collect_result()));
        }
      }
      if (OB_SUCC(ret)) {
        aggr_cell.set_is_advance_evaluated();
        aggr_cell.reset_extra();
      }
    }
  } // end for
  return ret;
}

int ObAggregateProcessor::collect_result_batch(const ObIArray<ObExpr *> &group_exprs,
                                               const int64_t output_batch_size,
                                               ObBatchRows &output_brs,
                                               int64_t &cur_group_id)
{
  int ret = OB_SUCCESS;
  int64_t start_group_idx = cur_group_id;
  int64_t loop_cnt = min(output_batch_size,
                         group_rows_.count() - cur_group_id);
  int64_t aggr_cnt = aggr_infos_.count();
  int64_t group_col_cnt = group_exprs.count();
  GroupRow *group_row = NULL;
  ObEvalCtx::BatchInfoScopeGuard guard(eval_ctx_);
  guard.set_batch_size(loop_cnt);
  for (int64_t aggr_idx = 0; OB_SUCC(ret) && aggr_idx < aggr_cnt; ++aggr_idx) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(aggr_idx);
    for (int64_t loop_idx = 0; OB_SUCC(ret) && loop_idx < loop_cnt; loop_idx++) {
      int64_t group_cur_idx = start_group_idx + loop_idx;
      int64_t output_batch_idx = output_brs.size_ + loop_idx;
      group_row = group_rows_.at(group_cur_idx);
      AggrCell &aggr_cell = group_row->aggr_cells_[aggr_idx];
      guard.set_batch_idx(output_batch_idx);
      if (aggr_cell.get_need_advance_collect() && aggr_cell.get_is_advance_evaluated()) {
        OZ(aggr_info.expr_->deep_copy_datum(eval_ctx_, aggr_cell.get_advance_collect_result()));
        LOG_TRACE("fill aggr result ", K(aggr_cell.get_advance_collect_result()),
          K(aggr_cell.get_is_evaluated()));
      } else {
        if (aggr_info.has_distinct_) {
          if (OB_FAIL(process_distinct_batch(0, aggr_cell, aggr_info, loop_cnt))) {
          }
        }
        if (OB_FAIL(ret)) {
        } else if (aggr_info.is_implicit_first_aggr()) {
          // aggr_info.expr_ skip check null, check in cg
          // judge aggr_cell.get_is_evaluated() whether implicit expr is evaluated
          OZ(publish_iter_result(aggr_info, aggr_cell, eval_ctx_));
          LOG_DEBUG("first aggr result ", K(aggr_cell.native_iter_result()),
            K(aggr_cell.get_is_evaluated()));
        } else {
          if (OB_FAIL(collect_aggr_result(aggr_cell, NULL, aggr_info))) {
          }
        }
      }
    } // end for
    aggr_info.expr_->get_eval_info(eval_ctx_).projected_ = true;
  }
  // clear operator evaluated flags to recalc expr after rollup set_null
  clear_op_evaluated_flag();
  // project group expr result
  for (int64_t loop_idx = 0; OB_SUCC(ret) && loop_idx < loop_cnt; loop_idx++) {
    guard.set_batch_idx(output_brs.size_ + loop_idx);
    group_row = group_rows_.at(start_group_idx + loop_idx);
    // need to skip const value
    if (OB_NOT_NULL(group_row->groupby_store_row_) &&
        OB_FAIL(group_row->groupby_store_row_->to_expr(group_exprs, eval_ctx_))) {
    } else {
      LOG_DEBUG("debug group by exprs", K(ret), K(ROWEXPR2STR(eval_ctx_, group_exprs)),
        K(start_group_idx + loop_idx), K(output_brs.size_ + loop_idx));
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < group_col_cnt; i++) {
    ObExpr *group_expr = group_exprs.at(i);
    group_expr->set_evaluated_projected(eval_ctx_);
  }
  cur_group_id += loop_cnt;
  output_brs.size_ += loop_cnt;

  LOG_DEBUG("debug group by exprs2", K(ret), K(ROWEXPR2STR(eval_ctx_, group_exprs)));
  return ret;
}

int ObAggregateProcessor::process_distinct_batch(
  const int64_t group_id,
  AggrCell &aggr_cell,
  const ObAggrInfo &aggr_info,
  const int64_t max_cnt)
{
  int ret = OB_SUCCESS;
  HashBasedDistinctExtraResult *extra_info = static_cast<HashBasedDistinctExtraResult*>(aggr_cell.get_extra());
  if (OB_ISNULL(extra_info)
    || !extra_info->is_inited()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // In non-rollup group_id must be 0
    if (group_id > 0) {
      // Group id greater than zero in sort based group by must be rollup,
      // distinct set is sorted and iterated in rollup_process(), rewind here.
      if (OB_FAIL(extra_info->rewind())) {
      }
    } else {
      if (!enable_hash_distinct_) {
        if (OB_FAIL(extra_info->unique_sort_op_->sort())) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(precompute_distinct_aggr_result(aggr_cell, aggr_info, max_cnt))) {
    }
  }
  return ret;
}

int ObAggregateProcessor::precompute_distinct_aggr_result(
  AggrCell &aggr_cell,
  const ObAggrInfo &aggr_info,
  const int64_t max_cnt)
{
  int ret = OB_SUCCESS;
  HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
  if (OB_ISNULL(ad_result)
    || !ad_result->is_inited()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (enable_hash_distinct_
    && OB_FAIL(ad_result->brs_holder_.save(max_cnt))) {
    // Hash base distinct fetch data from the hash table by batch and fill in the value to expr
    // so backup restore is needed here
  } else {
    bool is_first = true;
    while (OB_SUCC(ret)) {
      const ObChunkDatumStore::StoredRow *stored_row = NULL;
      int64_t read_rows = 0;
      if (enable_hash_distinct_
        && OB_FAIL(ad_result->get_next_unique_hash_table_batch(
          aggr_info.param_exprs_, max_cnt, read_rows))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
        }
        break;
      } else if (!enable_hash_distinct_
        && OB_FAIL(ad_result->unique_sort_op_->get_next_batch(
          aggr_info.param_exprs_, max_cnt, read_rows))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
        }
        break;
      } else if (read_rows <= 0) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ObBatchRows brs;
        brs.skip_ = skip_;
        brs.size_ = read_rows;
        ObBatchRowsSlice selector(&brs, 0, read_rows);
        if (OB_FAIL(process_aggr_batch_result(
                &(aggr_info.param_exprs_), aggr_cell, aggr_info, selector))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (enable_hash_distinct_
      && OB_FAIL(ad_result->brs_holder_.restore())) {
      LOG_WARN("restore datum failed", K(ret));
    }
  }
  return ret;
}

int ObAggregateProcessor::collect_scalar_batch(const ObBatchRows &brs, const int64_t group_id,
    const ObExpr *diff_expr, const int64_t max_cnt)
{
  UNUSED(brs);
  int ret = OB_SUCCESS;
  GroupRow *group_row = NULL;
  if (OB_FAIL(group_rows_.at(group_id, group_row))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < group_row->n_cells_; ++i) {
      const ObAggrInfo &aggr_info = aggr_infos_.at(i);
      AggrCell &aggr_cell = group_row->aggr_cells_[i];
      if (OB_ISNULL(aggr_info.expr_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (aggr_info.is_implicit_first_aggr()) {
        if (OB_FAIL(publish_iter_result(aggr_info, aggr_cell, eval_ctx_))) {
        } else { aggr_info.expr_->set_evaluated_projected(eval_ctx_); }
      } else {
        if (aggr_info.has_distinct_) {
          if (OB_FAIL(process_distinct_batch(group_id, aggr_cell, aggr_info, max_cnt))) {
          }
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(collect_aggr_result(aggr_cell, diff_expr, aggr_info))) {
          }
        }
      }
      OX(LOG_DEBUG("finish collect", K(group_id), K(aggr_cell)));
    } // end for
  }
  return ret;
}


int ObAggregateProcessor::collect_for_empty_set()
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
    ObAggrInfo &aggr_info = aggr_infos_.at(i);
    if (OB_ISNULL(aggr_info.expr_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (aggr_info.is_implicit_first_aggr() && aggr_stage_ != ObThreeStageAggrStage::THIRD_STAGE) {
      set_expr_datum_null(aggr_info.expr_);
    } else {
      switch (aggr_info.get_expr_type()) {
        case T_FUN_COUNT:
        case T_FUN_COUNT_SUM:
        case T_FUN_APPROX_COUNT_DISTINCT:
        case T_FUN_SUM_OPNSIZE: {
          Datum &result = aggr_info.expr_->locate_datum_for_write(eval_ctx_);
          aggr_info.expr_->set_evaluated_projected(eval_ctx_);
          result.set_int(0);
          break;
        }
        case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS:
        case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE: {
          ret = llc_init_empty(*aggr_info.expr_, eval_ctx_);
          break;
        }
        case T_FUN_SYS_BIT_AND:
        case T_FUN_SYS_BIT_OR:
        case T_FUN_SYS_BIT_XOR: {
          Datum &result = aggr_info.expr_->locate_datum_for_write(eval_ctx_);
          aggr_info.expr_->set_evaluated_projected(eval_ctx_);
          uint64_t init_val =
            aggr_info.get_expr_type() == T_FUN_SYS_BIT_AND ? UINT_MAX_VAL[ObUInt64Type] : 0;
          result.set_uint(init_val);
          break;
        }
        default: {
          set_expr_datum_null(aggr_info.expr_);
          //do nothing
          break;
        }
      }
    }
  }
  return ret;
}

void ObAggregateProcessor::set_expr_datum_null(ObExpr *expr)
{
  // not check whether expr is null, it needs to be checked before call this function.
  Datum &datum = expr->locate_datum_for_write(eval_ctx_);
  expr->set_evaluated_flag(eval_ctx_);
  datum.set_null();
  if (expr->is_batch_result()) {
    expr->get_eval_info(eval_ctx_).notnull_ = false;
  }
}

int ObAggregateProcessor::process_aggr_result_from_distinct(
  AggrCell &aggr_cell,
  const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
  if (OB_ISNULL(ad_result) || !ad_result->is_inited()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (enable_hash_distinct_
    && OB_FAIL(ad_result->srs_holder_.save())) {
  } else {
    bool is_first = true;
    while (OB_SUCC(ret)) {
      const ObChunkDatumStore::StoredRow *stored_row = NULL;
      if (enable_hash_distinct_
       && OB_FAIL(ad_result->get_next_unique_hash_table_row(stored_row, &aggr_info.param_exprs_))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
        }
        break;
      } else if (!enable_hash_distinct_
        && OB_FAIL(ad_result->unique_sort_op_->get_next_stored_row(stored_row))) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        } else {
        }
        break;
      } else if (OB_ISNULL(stored_row) || OB_ISNULL(stored_row->stored_cells())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        if (is_first) {
          is_first = false;
          if (OB_FAIL(prepare_aggr_result(*stored_row, NULL, aggr_cell,
                                          aggr_info))) {
          }
        } else {
          if (OB_FAIL(process_aggr_result(*stored_row, NULL, aggr_cell,
                                          aggr_info))) {
          }
        }
        OX(LOG_DEBUG("succ iter prepare/process aggr result", K(stored_row), K(aggr_cell)));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (enable_hash_distinct_
    && OB_FAIL(ad_result->srs_holder_.restore())) {
    LOG_WARN("restore datum failed", K(ret));
  }
  return ret;
}

int ObAggregateProcessor::init_group_rows(const int64_t col_count, bool is_empty/*false*/)
{
  int ret = OB_SUCCESS;
  if (is_empty) {
    if (OB_FAIL(group_rows_.reserve(col_count))) {
    }
    for(int64_t i = group_rows_.count(); OB_SUCC(ret) && i < col_count; ++i) {
      if (OB_FAIL(group_rows_.push_back(nullptr))) {
      }
    }
  } else if (col_count > 1) {
    if (OB_FAIL(group_rows_.reserve(col_count))) {
    }
    for(int64_t i = group_rows_.count(); OB_SUCC(ret) && i < col_count; ++i) {
      if (OB_FAIL(init_one_group(i))) {
      }
    }
  } else {
    if (OB_FAIL(init_one_group())) {
    }
  }
  return ret;
}

int ObAggregateProcessor::generate_group_row(GroupRow *&new_group_row,
                                             const int64_t group_id)
{
  int ret = OB_SUCCESS;
  new_group_row = NULL;
  const int64_t alloc_size = GROUP_ROW_SIZE + GROUP_CELL_SIZE * aggr_infos_.count();
  if (0 == cur_batch_group_idx_ % BATCH_GROUP_SIZE) {
    if (OB_ISNULL(cur_batch_group_buf_ = (char *)aggr_alloc_.alloc(
                alloc_size * BATCH_GROUP_SIZE))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      // The memset is not needed here because the object will be constructed by NEW.
      // But we memset first then NEW got a better performance because of better CPU cache locality.
      MEMSET(cur_batch_group_buf_, 0, alloc_size * BATCH_GROUP_SIZE);
    }
  }

  if (OB_SUCC(ret)) {
    new_group_row = new(cur_batch_group_buf_) GroupRow();
    AggrCell *aggr_cells = new (cur_batch_group_buf_ + GROUP_ROW_SIZE)AggrCell[aggr_infos_.count()];
    new_group_row->n_cells_ = aggr_infos_.count();
    new_group_row->aggr_cells_ = aggr_cells;

    cur_batch_group_buf_ += alloc_size;
    ++cur_batch_group_idx_;
    cur_batch_group_idx_ %= BATCH_GROUP_SIZE;
  }

  if (OB_SUCC(ret) && has_extra_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
      const ObAggrInfo &aggr_info = aggr_infos_.at(i);
      AggrCell &aggr_cell = new_group_row->aggr_cells_[i];
      switch (aggr_info.get_expr_type()) {
        case T_FUN_GROUP_CONCAT:
        case T_FUN_MEDIAN:
        case T_FUN_GROUP_PERCENTILE_CONT:
        case T_FUN_GROUP_PERCENTILE_DISC:
        case T_FUN_WM_CONCAT:
        case T_FUN_PL_AGG_UDF:
        case T_FUN_JSON_ARRAYAGG:
        case T_FUN_ORA_JSON_ARRAYAGG:
        case T_FUN_JSON_OBJECTAGG:
        case T_FUN_ORA_JSON_OBJECTAGG: 
        case T_FUN_SYS_ST_ASMVT: 
        case T_FUNC_SYS_ARRAY_AGG:
        {
          void *tmp_buf = NULL;
          set_need_advance_collect();
          aggr_cell.set_need_advance_collect();
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(GroupConcatExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            GroupConcatExtraResult *result = new (tmp_buf) GroupConcatExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
            const bool need_rewind = (in_window_func_ || group_id > 0);
            if (-1 == dir_id_) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(result->init(aggr_info,
                                     eval_ctx_,
                                     need_rewind, dir_id_,
                                     io_event_observer_))) {
            } else if (aggr_info.separator_expr_ != NULL && aggr_info.separator_expr_->is_const_expr()) {
              Datum *separator_result = nullptr;
              if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(aggr_info.separator_expr_->eval(eval_ctx_, separator_result))) {
              } else if (OB_FAIL(result->copy_separator(*separator_result,
                  aggr_info.separator_expr_->get_datum_copy_plan()))) {
              }
            }
          }
          break;
        }
        case T_FUN_HYBRID_HIST: {
          void *tmp_buf = NULL;
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(HybridHistExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate memory failed", "size", sizeof(HybridHistExtraResult));
          } else {
            HybridHistExtraResult *result = new (tmp_buf) HybridHistExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
            const bool need_rewind = (in_window_func_ || group_id > 0);
            if (OB_FAIL(result->init(aggr_info,
                                     eval_ctx_,
                                     need_rewind,
                                     io_event_observer_,
                                     op_monitor_info_))) {
            }
          }
          break;
        }
        case T_FUN_TOP_FRE_HIST: {
          void *tmp_buf = NULL;
          set_need_advance_collect();
          aggr_cell.set_need_advance_collect();
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(TopKFreHistExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            TopKFreHistExtraResult *result = new (tmp_buf) TopKFreHistExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
          }
          break;
        }

        default:
          break;
      }

      if (OB_SUCC(ret) && aggr_info.has_distinct_) {
        set_need_advance_collect();
        aggr_cell.set_need_advance_collect();
        if (NULL == aggr_cell.get_extra()) {
          void *tmp_buf = NULL;
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(HashBasedDistinctExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            ExtraResult *result =
              new (tmp_buf) HashBasedDistinctExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
          }
        }

        if (OB_SUCC(ret)) {
          // In window function, get result will be called more than once, rewind is needed.
          //
          // The distinct set will iterate twice with rollup, for rollup processing and aggregation,
          // rewind is needed for the second iteration.
          //
          // Rollup is supported and only supported in sort based group by with multi-groups,
          // only groups with group id greater than zero need to rewind.
          // The groupid of hash groupby also is greater then 0, then need rewind ???
          const bool need_rewind = (in_window_func_ || group_id > 0);
          if (enable_hash_distinct_) {
            if (OB_ISNULL(hp_infras_mgr_)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra())
                                 ->init_distinct_set(aggr_info, need_rewind, *hp_infras_mgr_,
                                                     eval_ctx_))) {
            }
          } else {
            if (OB_FAIL(aggr_cell.get_extra()->init_distinct_set(
                  aggr_info,
                  eval_ctx_, need_rewind, io_event_observer_))) {
            }
          }
        }
      }
    }//end of for
  }
  return ret;
}

int ObAggregateProcessor::fill_group_row(GroupRow *new_group_row,
                                         const int64_t group_id)
{
  int ret = OB_SUCCESS;
  const int64_t alloc_size = GROUP_CELL_SIZE * aggr_infos_.count();
  if (alloc_size > 0 && 0 == cur_batch_group_idx_ % BATCH_GROUP_SIZE) {
    if (OB_ISNULL(cur_batch_group_buf_ = (char *)aggr_alloc_.alloc(
                alloc_size * BATCH_GROUP_SIZE))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      // The memset is not needed here because the object will be constructed by NEW.
      // But we memset first then NEW got a better performance because of better CPU cache locality.
      MEMSET(cur_batch_group_buf_, 0, alloc_size * BATCH_GROUP_SIZE);
    }
  }

  if (OB_SUCC(ret)) {
    AggrCell *aggr_cells = new (cur_batch_group_buf_)AggrCell[aggr_infos_.count()];
    new_group_row->n_cells_ = aggr_infos_.count();
    new_group_row->aggr_cells_ = aggr_cells;

    cur_batch_group_buf_ += alloc_size;
    ++cur_batch_group_idx_;
    cur_batch_group_idx_ %= BATCH_GROUP_SIZE;
  }

  if (OB_SUCC(ret) && has_extra_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
      const ObAggrInfo &aggr_info = aggr_infos_.at(i);
      AggrCell &aggr_cell = new_group_row->aggr_cells_[i];
      switch (aggr_info.get_expr_type()) {
        case T_FUN_GROUP_CONCAT:
        case T_FUN_MEDIAN:
        case T_FUN_GROUP_PERCENTILE_CONT:
        case T_FUN_GROUP_PERCENTILE_DISC:
        case T_FUN_WM_CONCAT:
        case T_FUN_PL_AGG_UDF:
        case T_FUN_JSON_ARRAYAGG:
        case T_FUN_ORA_JSON_ARRAYAGG:
        case T_FUN_JSON_OBJECTAGG:
        case T_FUN_ORA_JSON_OBJECTAGG: 
        case T_FUN_SYS_ST_ASMVT:
        case T_FUNC_SYS_ARRAY_AGG:
        {
          void *tmp_buf = NULL;
          set_need_advance_collect();
          aggr_cell.set_need_advance_collect();
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(GroupConcatExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            GroupConcatExtraResult *result = new (tmp_buf) GroupConcatExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
            const bool need_rewind = (in_window_func_ || group_id > 0);
            if (-1 == dir_id_) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(result->init(aggr_info,
                                     eval_ctx_,
                                     need_rewind, dir_id_,
                                     io_event_observer_))) {
            } else if (aggr_info.separator_expr_ != NULL && aggr_info.separator_expr_->is_const_expr()) {
              Datum *separator_result = nullptr;
              if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(aggr_info.separator_expr_->eval(eval_ctx_, separator_result))) {
              } else if (OB_FAIL(result->copy_separator(*separator_result,
                  aggr_info.separator_expr_->get_datum_copy_plan()))) {
              }
            }
          }
          break;
        }
        case T_FUN_HYBRID_HIST: {
          void *tmp_buf = NULL;
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(HybridHistExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate memory failed", "size", sizeof(HybridHistExtraResult));
          } else {
            HybridHistExtraResult *result = new (tmp_buf) HybridHistExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
            const bool need_rewind = (in_window_func_ || group_id > 0);
            if (OB_FAIL(result->init(aggr_info,
                                     eval_ctx_,
                                     need_rewind,
                                     io_event_observer_,
                                     op_monitor_info_))) {
            }
          }
          break;
        }
        case T_FUN_TOP_FRE_HIST: {
          void *tmp_buf = NULL;
          set_need_advance_collect();
          aggr_cell.set_need_advance_collect();
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(TopKFreHistExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            TopKFreHistExtraResult *result = new (tmp_buf) TopKFreHistExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
          }
          break;
        }

        default:
          break;
      }

      if (OB_SUCC(ret) && aggr_info.has_distinct_) {
        set_need_advance_collect();
        aggr_cell.set_need_advance_collect();
        if (NULL == aggr_cell.get_extra()) {
          void *tmp_buf = NULL;
          if (OB_ISNULL(tmp_buf = aggr_alloc_.alloc(sizeof(HashBasedDistinctExtraResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            ExtraResult *result = new (tmp_buf) HashBasedDistinctExtraResult(aggr_alloc_, op_monitor_info_);
            aggr_cell.set_extra(result);
          }
        }

        if (OB_SUCC(ret)) {
          // In window function, get result will be called more than once, rewind is needed.
          //
          // The distinct set will iterate twice with rollup, for rollup processing and aggregation,
          // rewind is needed for the second iteration.
          //
          // Rollup is supported and only supported in sort based group by with multi-groups,
          // only groups with group id greater than zero need to rewind.
          // The groupid of hash groupby also is greater then 0, then need rewind ???
          const bool need_rewind = (in_window_func_ || group_id > 0);
          if (enable_hash_distinct_) {
            if (OB_ISNULL(hp_infras_mgr_)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(static_cast<HashBasedDistinctExtraResult*>(
                      aggr_cell.get_extra())->init_distinct_set(aggr_info,
                                                                need_rewind,
                                                                *hp_infras_mgr_,
                                                                eval_ctx_))) {
            }
          } else {
            if (OB_FAIL(static_cast<ExtraResult*>(aggr_cell.get_extra())->init_distinct_set(
                aggr_info,
                eval_ctx_,
                need_rewind,
                io_event_observer_))) {
            }
          }
        }
      }
    }//end of for
  }
  return ret;
}

int ObAggregateProcessor::init_one_group(const int64_t group_id,
                                         bool fill_pos /* false */)
{
  int ret = OB_SUCCESS;
  GroupRow *group_row = nullptr;
  if (OB_FAIL(generate_group_row(group_row, group_id))) {
  } else if (fill_pos) {
    if (group_rows_.count() <= group_id) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      group_rows_.at(group_id) = group_row;
    }
  } else if (OB_FAIL(group_rows_.push_back(group_row))) {
  } else {
    LOG_DEBUG("succ init group_row", K(group_id), KPC(group_row), K(ret));
  }
  return ret;
}


int ObAggregateProcessor::rollup_process(
  const int64_t group_id,
  const int64_t rollup_group_id,
  const int64_t max_group_cnt,
  const ObExpr *diff_expr /*= NULL*/)
{
  int ret = OB_SUCCESS;
  GroupRow *group_row = NULL;
  GroupRow *rollup_row = NULL;
  if (OB_FAIL(group_rows_.at(group_id, group_row))) {
  } else if (rollup_group_id < max_group_cnt) {
    // Fixed GROUP BY prefix columns do not have a rollup target.
  } else if (OB_FAIL(group_rows_.at(rollup_group_id, rollup_row))) {
  } else if (OB_ISNULL(group_row) || OB_ISNULL(rollup_row)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(rollup_base_process(group_row, rollup_row, diff_expr, group_id, max_group_cnt))) {
  }
  return ret;
}

int ObAggregateProcessor::rollup_batch_process(
  const int64_t group_row_id,
  const int64_t rollup_group_row_id,
  int64_t diff_group_idx,
  const int64_t max_group_cnt/*=INT64_MIN*/)
{
  int ret = OB_SUCCESS;
  GroupRow *group_row = NULL;
  GroupRow *rollup_row = NULL;
  if (OB_FAIL(group_rows_.at(group_row_id, group_row))) {
  } else if (OB_FAIL(group_rows_.at(rollup_group_row_id, rollup_row))) {
  } else if (OB_ISNULL(group_row) || OB_ISNULL(rollup_row)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(rollup_base_process(group_row, rollup_row, nullptr, diff_group_idx, max_group_cnt))) {
  }
  return ret;
}

int ObAggregateProcessor::rollup_base_process(
  GroupRow *group_row,
  GroupRow *rollup_row,
  const ObExpr *diff_expr /*= NULL*/,
  int64_t diff_group_idx,
  const int64_t max_group_cnt/*INT64_MIN*/)
{
  int ret = OB_SUCCESS;
  // copy aggregation column
  for (int64_t i = 0; OB_SUCC(ret) && i < group_row->n_cells_; ++i) {
    const ObAggrInfo &aggr_info = aggr_infos_.at(i);
    AggrCell &aggr_cell = group_row->aggr_cells_[i];
    AggrCell &rollup_cell = rollup_row->aggr_cells_[i];
    if (T_FUN_TOP_FRE_HIST == aggr_info.get_expr_type()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "topk fre hist in group by rollup");
    } else if (aggr_info.is_implicit_first_aggr()) {
      if (OB_FAIL(rollup_cell.copy_semantic_iter(aggr_cell.native_iter_result(),
          aggr_info.expr_->get_sql_type(), aggr_alloc_))) {
      }
    } else {
      if (aggr_info.has_distinct_) {
        if(OB_FAIL(rollup_distinct(aggr_info, aggr_cell, rollup_cell))) {
        }
      } else {
        if (OB_FAIL(rollup_aggregation(aggr_cell, rollup_cell, diff_expr, aggr_info, diff_group_idx, max_group_cnt))) {
        }
      }
    }
    OX(LOG_DEBUG("finish rollup_process iter", K(i), K(aggr_cell), K(rollup_cell), K(ret)));
  }
  OX(LOG_DEBUG("finish rollup_process", K(diff_group_idx), KPC(group_row),
            KPC(rollup_row), KPC(diff_expr),K(ret)));
  return ret;
}

int ObAggregateProcessor::rollup_aggregation(AggrCell &aggr_cell, AggrCell &rollup_cell,
    const ObExpr *diff_expr, const ObAggrInfo &aggr_info, int64_t cur_rollup_group_idx,
    const int64_t max_group_cnt/*=INT64_MIN*/)
{
  int ret = OB_SUCCESS;
  const ObItemType aggr_fun = aggr_info.get_expr_type();
  switch (aggr_fun) {
    case T_FUN_COUNT: {
      rollup_cell.add_row_count(aggr_cell.get_row_count());
      break;
    }
    case T_FUN_MAX: {
      alignas(8) char source_scratch[64];
      ObDatum source;
      const SqlType &input_type = aggr_info.param_exprs_.at(0)->get_sql_type();
      if (OB_FAIL(aggr_cell.export_semantic_iter(input_type, source, source_scratch, sizeof(source_scratch)))) {
      } else { ret = max_calc(rollup_cell, source,
          storage_predicate_functions(*aggr_info.expr_)->null_first_cmp_, input_type); }
      break;
    }
    case T_FUN_MIN: {
      alignas(8) char source_scratch[64];
      ObDatum source;
      const SqlType &input_type = aggr_info.param_exprs_.at(0)->get_sql_type();
      if (OB_FAIL(aggr_cell.export_semantic_iter(input_type, source, source_scratch, sizeof(source_scratch)))) {
      } else { ret = min_calc(rollup_cell, source,
          storage_predicate_functions(*aggr_info.expr_)->null_first_cmp_, input_type); }
      break;
    }
    case T_FUN_AVG:
      ret = OB_ERR_UNEXPECTED;
    case T_FUN_SUM:
    case T_FUN_COUNT_SUM: {
      ret = rollup_add_calc(aggr_cell, rollup_cell, aggr_info);
      break;
    }
    case T_FUN_APPROX_COUNT_DISTINCT:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE: {
      ObDatum source, target;
      if (OB_FAIL(export_hll_iteration(aggr_cell, source))) {
      } else if (OB_FAIL(export_hll_iteration(rollup_cell, target))) {
      } else if (target.len_ <= 0) {
        ret = clone_kernel_cell(rollup_cell, source,
            IterKernelLayout(IterValueDomain::HllRegisters, 0,
                source.is_null() ? get_llc_size() : source.len_));
      } else { ret = llc_add(target, source); }
      break;
    }
    case T_FUN_GROUPING: {
      if (OB_UNLIKELY(aggr_info.param_exprs_.count() != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("param_exprs_ count is not 1", K(aggr_info));
      } else {
        if (diff_expr != NULL && diff_expr == aggr_info.param_exprs_.at(0)) {
          aggr_cell.set_tiny_num_int(1);
          LOG_DEBUG("rollup grouping", K(aggr_info.rollup_idx_), K(aggr_cell.get_tiny_num_int()));
        }
        if (-1 == cur_rollup_group_idx) {
          rollup_cell.set_tiny_num_int(aggr_cell.get_tiny_num_int());
          LOG_DEBUG("rollup grouping", K(aggr_info.rollup_idx_), K(aggr_cell.get_tiny_num_int()));
        } else if (INT64_MAX == aggr_info.rollup_idx_) {
          rollup_cell.set_tiny_num_int(aggr_cell.get_tiny_num_int());
          LOG_DEBUG("rollup grouping", K(aggr_info.rollup_idx_), K(aggr_cell.get_tiny_num_int()));
        } else if (aggr_info.rollup_idx_ >= cur_rollup_group_idx) {
          rollup_cell.set_tiny_num_int(1);
        } else {
          rollup_cell.set_tiny_num_int(aggr_cell.get_tiny_num_int());
        }
      }
      break;
    }
    case T_FUN_GROUPING_ID: {
      if (-1 == cur_rollup_group_idx) {
        rollup_cell.set_tiny_num_uint(aggr_cell.get_tiny_num_uint());
      } else {
        uint64_t new_value = 0;
        /*in vector engine: previous value should be used, if not match, use the previous value.
        * like the last branch in case T_FUN_GROUPING:
        * "
        * else {
        *   rollup_cell.set_tiny_num_int(aggr_cell.get_tiny_num_int());
        * }
        * "
        * we set the grouping_id as new_value | old_value
        */
        uint64_t old_value = aggr_cell.get_tiny_num_uint();
        for (int64_t i = 0; i < aggr_info.grouping_idxs_.count(); i++) {
          new_value = new_value << 1;
          int64_t grouping_idx = aggr_info.grouping_idxs_.at(i);
          if (grouping_idx >= cur_rollup_group_idx) {
            new_value++;
          }
        } 
        if (diff_expr == NULL) {
          rollup_cell.set_tiny_num_uint(new_value | old_value);
        } else {
          aggr_cell.set_tiny_num_uint(new_value);
          rollup_cell.set_tiny_num_uint(new_value);
        }
      }
      break;
    }
    case T_FUN_GROUP_ID: {
      bool match = false;
      // in batch process, cur_rollup_group_idx == max_group_cnt means that the diff expr is not distinct.
      // e.g., group by c1, rollup(c1);
      // in normal process, max_group_cnt is INT64_MIN.
      if (max_group_cnt == INT64_MIN) {
        // from normal process;
        for (int64_t i = 0; !match && i < aggr_info.group_idxs_.count(); i++) {
          if (cur_rollup_group_idx == aggr_info.group_idxs_.at(i)) {
            match = true;
          }
        }
        if (match) {
          aggr_cell.set_tiny_num_uint(aggr_cell.get_tiny_num_uint() + 1);
          rollup_cell.set_tiny_num_uint(aggr_cell.get_tiny_num_uint());
        } else {
          aggr_cell.set_tiny_num_uint(0);
          rollup_cell.set_tiny_num_uint(0);
        }
        LOG_WARN("1", K(match));
      } else {
        // from batch rollup
        match = cur_rollup_group_idx == max_group_cnt;
        if (match) {
          rollup_cell.set_tiny_num_uint(aggr_cell.get_tiny_num_uint() + 1);
        } else {
          rollup_cell.set_tiny_num_uint(0);
        }
      }
      break;
    }
    case T_FUN_GROUP_CONCAT:
    case T_FUN_MEDIAN:
    case T_FUN_GROUP_PERCENTILE_CONT:
    case T_FUN_GROUP_PERCENTILE_DISC:
    case T_FUN_WM_CONCAT:
    case T_FUN_PL_AGG_UDF:
    case T_FUN_JSON_ARRAYAGG: 
    case T_FUN_ORA_JSON_ARRAYAGG: 
    case T_FUN_JSON_OBJECTAGG:
    case T_FUN_ORA_JSON_OBJECTAGG: 
    case T_FUN_SYS_ST_ASMVT:
    case T_FUNC_SYS_ARRAY_AGG:
    {
      GroupConcatExtraResult *aggr_extra = NULL;
      GroupConcatExtraResult *rollup_extra = NULL;
      if (OB_ISNULL(aggr_extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))
          || OB_ISNULL(rollup_extra = static_cast<GroupConcatExtraResult *>(rollup_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(aggr_extra->finish_add_row())) {
      } else {
        const ObChunkDatumStore::StoredRow *stored_row = NULL;
        if (aggr_fun == T_FUN_JSON_ARRAYAGG || aggr_fun == T_FUN_JSON_OBJECTAGG) {
          int64_t len = aggr_extra->get_bool_mark_size();
          if (OB_FAIL(rollup_extra->reserve_bool_mark_count(len))) {
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < len; i++) {
            bool is_bool = false;
            if (OB_FAIL(aggr_extra->get_bool_mark(i, is_bool))) {
            } else if (OB_FAIL(rollup_extra->set_bool_mark(i, is_bool))) {
            }
          }
        }
        if (OB_SUCC(ret) && aggr_info.separator_expr_ != NULL && !aggr_info.separator_expr_->is_const_expr()) {
          Datum *separator_result = nullptr;
          aggr_info.separator_expr_->clear_evaluated_flag(eval_ctx_);
          if (!aggr_extra->has_separator() || aggr_extra->separator_value().is_null()) {
            // Preserve the existing unset/SQL-NULL separator policy.
          } else if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(aggr_info.separator_expr_->eval(eval_ctx_, separator_result))) {
          } else if (separator_result->is_null()) {
            aggr_extra->clear_separator();
            rollup_extra->clear_separator();
          } else {
            alignas(8) char input_scratch[64], saved_scratch[64];
            ObDatum input_view, saved_view;
            const DatumCopyPlan &separator_plan = aggr_info.separator_expr_->get_datum_copy_plan();
            int cmp_ret = 0;
            if (OB_FAIL(separator_plan.legacy_view(*separator_result,
                input_view, input_scratch, sizeof(input_scratch)))) {
            } else if (OB_FAIL(aggr_extra->export_separator(separator_plan,
                saved_view, saved_scratch, sizeof(saved_scratch)))) {
            } else if (OB_FAIL(storage_predicate_functions(*aggr_info.separator_expr_)->null_first_cmp_(
                input_view, saved_view, cmp_ret, datum_access_ctx_))) {
            } else if (0 != cmp_ret && OB_FAIL(aggr_extra->copy_separator(*separator_result, separator_plan))) {
            }
            if (OB_SUCC(ret) && max_group_cnt != INT64_MIN && cur_rollup_group_idx - max_group_cnt > 1) {
              ret = rollup_extra->copy_separator(*separator_result, separator_plan);
            }
          }
        }
        while (OB_SUCC(ret) && OB_SUCC(aggr_extra->get_next_row(stored_row))) {
          if (OB_ISNULL(stored_row)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(rollup_extra->add_row(*stored_row))) {
          }
        }
        if (ret == OB_ITER_END) {
          ret = OB_SUCCESS;
        }
      }
      break;
    }
    case T_FUN_HYBRID_HIST: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "rollup contain hybrid hist");
      break;
    }
    case T_FUN_SYS_BIT_AND: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "rollup contain bit_and");
      break;
    }
    case T_FUN_SYS_BIT_OR: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "rollup contain bit_or");
      break;
    }
    case T_FUN_SYS_BIT_XOR: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "rollup contain bit_xor");
      break;
    }
    case T_FUN_SUM_OPNSIZE: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "rollup contain sum_opnsize");
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown aggr function type", K(aggr_fun));
      break;
  }
  return ret;
}

int ObAggregateProcessor::rollup_distinct(const ObAggrInfo &aggr_info,
    AggrCell &aggr_cell, AggrCell &rollup_cell)
{
  int ret = OB_SUCCESS;
  HashBasedDistinctExtraResult *ad_result = static_cast<HashBasedDistinctExtraResult *>(aggr_cell.get_extra());
  HashBasedDistinctExtraResult *rollup_result = static_cast<HashBasedDistinctExtraResult *>(rollup_cell.get_extra());
  if (OB_ISNULL(ad_result)
      || !ad_result->is_inited()
      || OB_ISNULL(rollup_result)
      || !rollup_result->is_inited()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!enable_hash_distinct_
    && OB_FAIL(ad_result->unique_sort_op_->sort())) {
  } else if (enable_hash_distinct_ && eval_ctx_.max_batch_size_ <= 0
    && OB_FAIL(ad_result->srs_holder_.save())) {
  } else if (enable_hash_distinct_ && eval_ctx_.max_batch_size_ > 0
    && OB_FAIL(ad_result->brs_holder_.save(eval_ctx_.max_batch_size_))) {
  } else {
    while (OB_SUCC(ret)) {
      const ObChunkDatumStore::StoredRow *stored_row = NULL;
      if (enable_hash_distinct_) {
        if (OB_FAIL(ad_result->get_next_unique_hash_table_row(stored_row, &aggr_info.param_exprs_))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
          }
          break;
        } else if (OB_ISNULL(stored_row) || OB_ISNULL(stored_row->stored_cells())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(rollup_result->insert_row(aggr_info.param_exprs_))) {
        }
      } else {
        if (OB_FAIL(ad_result->unique_sort_op_->get_next_stored_row(stored_row))) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
          }
          break;
        } else if (OB_ISNULL(stored_row) || OB_ISNULL(stored_row->stored_cells())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(rollup_result->unique_sort_op_->add_stored_row(*stored_row))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (enable_hash_distinct_ && eval_ctx_.max_batch_size_ <= 0
      && OB_FAIL(ad_result->srs_holder_.restore())) {
      LOG_WARN("restore datum failed", K(ret));
    } else if (enable_hash_distinct_ && eval_ctx_.max_batch_size_ > 0
      && OB_FAIL(ad_result->brs_holder_.restore())) {
      LOG_WARN("restore datum failed", K(ret));
    }
  }
  return ret;
}

int ObAggregateProcessor::prepare_aggr_result(const ObChunkDatumStore::StoredRow &stored_row,
    const ObIArray<ObExpr *> *param_exprs, AggrCell &aggr_cell, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  const ObItemType aggr_fun = aggr_info.get_expr_type();
   if (NULL != param_exprs && param_exprs->empty()) {
    if (T_FUN_COUNT == aggr_fun) {
      aggr_cell.inc_row_count();
    } else if (T_FUN_GROUP_ID == aggr_fun) {
     aggr_cell.set_tiny_num_uint(0);
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  } else {
    switch (aggr_fun) {
      case T_FUN_COUNT: {
        bool has_null = false;
        for (int64_t i = 0; !has_null && i < stored_row.cnt_; ++i) {
          has_null = stored_row.stored_cells()[i].is_null();
        }
        if (!has_null) {
          aggr_cell.inc_row_count();
        }
        break;
      }
      case T_FUN_MAX:
      case T_FUN_MIN:
      case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE: {
        if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("curr_row_results count is not 1", K(stored_row));
        } else if (!stored_row.stored_cells()[0].is_null()) {
          if (aggr_fun == T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE) {
            if (OB_UNLIKELY(stored_row.stored_cells()[0].len_ < get_llc_size())) {
              ret = OB_INVALID_ARGUMENT;
              LOG_WARN("invalid argument length", K(ret), K(stored_row.stored_cells()[0].len_));
            }
          }
          if (OB_SUCC(ret)) {
            const ObDatum input = StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]);
            ret = aggr_fun == T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE
                ? clone_kernel_cell(aggr_cell, input,
                    IterKernelLayout(IterValueDomain::HllRegisters, 0, input.len_))
                : clone_aggr_cell(aggr_cell, input, aggr_info.param_exprs_.at(0)->get_sql_type());
          }
        }
        break;
      }
      case T_FUN_AVG: {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
      case T_FUN_COUNT_SUM:
      case T_FUN_SUM: {
        if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("curr_row_results count is not 1", K(stored_row));
        } else if (!stored_row.stored_cells()[0].is_null()) {
          ret = prepare_add_calc(StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]), aggr_cell, aggr_info);
        } else {
          removal_info_.null_cnt_++;
        }
        break;
      }
      case T_FUN_GROUPING: {
        if (aggr_info.hash_rollup_info_ != nullptr) {
          Datum grouping_val =
            aggr_info.hash_rollup_info_->rollup_grouping_id_->locate_expr_datum(eval_ctx_);
          aggr_cell.set_tiny_num_int(share::aggregate::is_grouping(aggr_info, grouping_val.get_int()));
          aggr_cell.set_is_evaluated(true);
        } else {
          aggr_cell.set_tiny_num_int(0);
        }
        break;
      }
      case T_FUN_GROUPING_ID: {
        if (aggr_info.hash_rollup_info_ != nullptr) {
          Datum grouping_val =
          aggr_info.hash_rollup_info_->rollup_grouping_id_->locate_expr_datum(eval_ctx_);
          int64_t res_int = -1;
          if (OB_FAIL(share::aggregate::get_grouping_id(aggr_info, grouping_val.get_int(), &res_int))) {
          } else {
            aggr_cell.set_tiny_num_int(res_int);
          }
        } else {
          aggr_cell.set_tiny_num_uint(0);
        }
        break;
      }
    case T_FUN_APPROX_COUNT_DISTINCT:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS: {
      ObDatum llc_bitmap;
      if (OB_FAIL(llc_init(aggr_cell))) {
      } else if (OB_FAIL(export_hll_iteration(aggr_cell, llc_bitmap))) {
      } else {
        bool has_null_cell = false;
        uint64_t hash_value = 0;
        if (OB_FAIL(llc_calc_hash_value(stored_row,
                                        aggr_info.param_exprs_,
                                        has_null_cell,
                                        hash_value))) {
        } else if (has_null_cell) {
          /*do nothing*/
        } else {
          ret = llc_add_value(hash_value, llc_bitmap.get_string());
        }
      }
      break;
    }
    case T_FUN_GROUP_CONCAT:
    case T_FUN_MEDIAN:
    case T_FUN_GROUP_PERCENTILE_CONT:
    case T_FUN_GROUP_PERCENTILE_DISC:
    case T_FUN_WM_CONCAT:
    case T_FUN_PL_AGG_UDF:
    case T_FUN_JSON_ARRAYAGG:
    case T_FUN_ORA_JSON_ARRAYAGG:
    case T_FUN_JSON_OBJECTAGG:
    case T_FUN_ORA_JSON_OBJECTAGG: 
    case T_FUN_SYS_ST_ASMVT:
    case T_FUNC_SYS_ARRAY_AGG:
    {
      GroupConcatExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        extra->reuse_self();
        if (param_exprs == NULL && OB_FAIL(extra->add_row(stored_row))) {
        } else if (param_exprs != NULL && OB_FAIL(extra->add_row(*param_exprs, eval_ctx_))) {
        } else {
          if (aggr_fun == T_FUN_JSON_ARRAYAGG || aggr_fun == T_FUN_JSON_OBJECTAGG) {
            if (param_exprs == NULL) {
              ret = OB_ERR_UNEXPECTED;
            } else {
              int64_t len = param_exprs->count();
              if (OB_FAIL(extra->reserve_bool_mark_count(len))) {
              }
              for (int64_t i = 0; OB_SUCC(ret) && i < len; i++) {
                ObExpr *tmp = NULL;
                if (OB_FAIL(param_exprs->at(i, tmp))){
                } else {
                  bool is_bool = (tmp->is_boolean_ == 1);
                  if (OB_FAIL(extra->set_bool_mark(i, is_bool))){
                  }
                }
              }
            }
          }
          if (OB_SUCC(ret) && aggr_info.separator_expr_ != NULL) {
            Datum *separator_result = nullptr;
            if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(aggr_info.separator_expr_->eval(eval_ctx_, separator_result))) {
            } else {
              ret = extra->copy_separator(*separator_result,
                  aggr_info.separator_expr_->get_datum_copy_plan());
            }
          }
        }
      }
      break;
    }
    case T_FUN_HYBRID_HIST: {
      HybridHistExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<HybridHistExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        extra->reuse_self();
        if (param_exprs == NULL && OB_FAIL(extra->add_sort_row(stored_row))) {
          LOG_WARN("fail to add row");
        } else if (param_exprs != NULL && OB_FAIL(extra->add_sort_row(*param_exprs, eval_ctx_))) {
          LOG_WARN("fail to add row");
        } else {
          LOG_DEBUG("succ to add row", K(stored_row), KPC(extra));
        }
      }
      break;
    }
    case T_FUN_TOP_FRE_HIST: {
      TopKFreHistExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<TopKFreHistExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(stored_row.cnt_ != 1 ||
                             aggr_info.param_exprs_.count() != 1) ||
                 OB_ISNULL(aggr_info.param_exprs_.at(0)) ||
                 OB_ISNULL(aggr_info.param_exprs_.at(0)->basic_funcs_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(init_topk_fre_histogram_item(aggr_info, &(extra->topk_fre_hist_)))) {
      } else if (extra->topk_fre_hist_.is_need_merge_topk_hist()) {
        DatumValue value;
        const SqlTypeMeta &meta = aggr_info.param_exprs_.at(0)->obj_meta_;
        if (OB_FAIL(ResultRowView::project_value(StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]), meta,
                ObDatum::get_sql_value_datum_map_type(meta.get_type()), SqlTypeMeta(), value, true))) {
        } else if (OB_FAIL(extra->topk_fre_hist_.merge_distribute_top_k_fre_items(value))) {
        }
      } else {
        common::ObArenaAllocator tmp_alloctor("CalcTopkHist", OB_MALLOC_NORMAL_BLOCK_SIZE);
        const SqlTypeMeta obj_meta = aggr_info.param_exprs_.at(0)->obj_meta_;
        ObDatum new_prev_datum;
        if (!obj_meta.is_lob_storage() &&
            OB_FAIL(shadow_truncate_stored_string_for_hist(obj_meta, stored_row, 0))) {
        } else if (obj_meta.is_lob_storage() &&
                   OB_FAIL(ObHybridHistograms::build_prefix_str_datum_for_lob(tmp_alloctor,
                                                                              obj_meta,
                                                                              StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]),
                                                                              new_prev_datum,
                                                                              datum_access_ctx_))) {
        } else {
          const ObDatum datum = obj_meta.is_lob_storage() ? new_prev_datum : StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]);
          common::ObDatumHashFuncType hash_func = storage_predicate_functions(*aggr_info.param_exprs_.at(0))->murmur_hash_;
          uint64_t datum_value = 0;
          if (OB_FAIL(hash_func(datum, datum_value, datum_value, datum_access_ctx_))) {
          } else if (OB_FAIL(extra->topk_fre_hist_.add_top_k_frequency_item(datum_value, datum, obj_meta))) {
          }
        }
      }
      break;
    }
    case T_FUN_SYS_BIT_AND:
    case T_FUN_SYS_BIT_OR:
    case T_FUN_SYS_BIT_XOR: {
      if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("curr_row_results count is not 1", K(stored_row));
      } else if (!stored_row.stored_cells()[0].is_null()) {
        aggr_cell.set_tiny_num_uint(StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]).get_uint());
      } else {
        // if first value is null, set agg result to default value
        uint64_t init_val = aggr_fun == T_FUN_SYS_BIT_AND ? UINT_MAX_VAL[ObUInt64Type] : 0;
        aggr_cell.set_tiny_num_uint(init_val);
      }
      aggr_cell.set_tiny_num_used();
      break;
    }
    case T_FUN_SUM_OPNSIZE: {
      int64_t size = 0;
      ObDatum input;
      Datum native_input;
      if (OB_UNLIKELY(stored_row.cnt_ != 1) ||
          OB_ISNULL(param_exprs) ||
          OB_UNLIKELY(param_exprs->count() != 1)) {
        ret = OB_INVALID_ARGUMENT;
      } else if (FALSE_IT(input = StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]))) {
      } else if (OB_FAIL(param_exprs->at(0)->get_datum_copy_plan().import_legacy(input, native_input))) {
      } else if (OB_FAIL(ObExprSysOpOpnsize::calc_sys_op_opnsize(param_exprs->at(0),
                                                                 &native_input,
                                                                 size))) {
      } else {
        aggr_cell.set_tiny_num_int(size);
        aggr_cell.set_tiny_num_used();
      }
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::process_aggr_batch_result(
    const ObIArray<ObExpr *> *param_exprs,
    AggrCell &aggr_cell,
    const ObAggrInfo &aggr_info,
    const T &selector)
{
  int ret = OB_SUCCESS;
  const ObItemType aggr_fun = aggr_info.get_expr_type();
  if (NULL != param_exprs && param_exprs->empty()) {
    if (T_FUN_COUNT == aggr_fun) {
      //count asterisk(*) case: null is counted
      uint16_t row_count = 0;
      for (auto it = selector.begin(); it < selector.end(); selector.next(it)) {
        ++row_count;
      }
      aggr_cell.add_row_count(row_count);
    } else if (T_FUN_GROUP_ID == aggr_fun) {
      // do nothing
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  } else {
    switch (aggr_fun) {
    case T_FUN_COUNT: {
      uint16_t row_count = 0;
      for (auto it = selector.begin(); it < selector.end(); selector.next(it)) {
        bool has_null = false;
        for (int64_t nth_param = 0; !has_null && nth_param < param_exprs->count(); ++nth_param) {
          DatumVector aggr_input_datums = param_exprs->at(nth_param)->locate_expr_datumvector(eval_ctx_);
          has_null = aggr_input_datums.at(selector.get_batch_index(it))->is_null();
        }
        if (!has_null) {
          ++row_count;
        }
      }
      aggr_cell.add_row_count(row_count);
      break;
    }
    case T_FUN_MAX: {
      LegacyAggrBatch aggr_input_datums;
      if (OB_FAIL(aggr_input_datums.prepare_selected(*param_exprs->at(0), eval_ctx_, selector))) { break; }
      ret = max_calc_batch(aggr_cell,
                      aggr_input_datums,
                      storage_predicate_functions(*aggr_info.expr_)->null_first_cmp_,
                      aggr_info.param_exprs_.at(0)->get_sql_type(), selector);
      break;
    }
    case T_FUN_MIN: {
      LegacyAggrBatch aggr_input_datums;
      if (OB_FAIL(aggr_input_datums.prepare_selected(*param_exprs->at(0), eval_ctx_, selector))) { break; }
      ret = min_calc_batch(aggr_cell,
                      aggr_input_datums,
                      storage_predicate_functions(*aggr_info.expr_)->null_first_cmp_,
                      aggr_info.param_exprs_.at(0)->get_sql_type(), selector);
      break;
    }
    case T_FUN_AVG: {
      LOG_WARN("should not reach here, T_FUN_AVG should be transformed to T_FUN_SUM/T_FUN_COUNT",
                K(aggr_fun));
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    case T_FUN_SUM:
    case T_FUN_COUNT_SUM: {
      LegacyAggrBatch aggr_input_datums;
      if (OB_FAIL(aggr_input_datums.prepare_selected(*param_exprs->at(0), eval_ctx_, selector))) { break; }
      ret = add_calc_batch(aggr_input_datums,
                            aggr_cell, aggr_info, selector);
      break;
    }
    case T_FUN_GROUPING: {
      if (aggr_info.hash_rollup_info_!= nullptr && !aggr_cell.get_is_evaluated()) {
        if (OB_FAIL(grouping_calc_batch(aggr_info, aggr_cell, selector))) {
        }
      }
      break;
    }
    case T_FUN_GROUPING_ID: {
      if (aggr_info.hash_rollup_info_ != nullptr && !aggr_cell.get_is_evaluated()) {
        if (OB_FAIL(grouping_id_calc_batch(aggr_info, aggr_cell, selector))) {
        }
      }
      break;
    }
    case T_FUN_APPROX_COUNT_DISTINCT:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS: {
      ObDatum llc_bitmap;
      if (!aggr_cell.get_is_evaluated()) {
        // init value firstly
        if (OB_FAIL(llc_init(aggr_cell))) {
        } else {
          aggr_cell.set_is_evaluated(true);
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(export_hll_iteration(aggr_cell, llc_bitmap))) {
      } else if (OB_SUCC(ret)) { ret = approx_count_calc_batch(llc_bitmap, param_exprs, selector); }
      break;
    }
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE: {
      if (1 != param_exprs->count()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("The count of APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE is not 1",
          K(param_exprs->count()));
      } else {
        LegacyAggrBatch arg_datums;
        if (OB_FAIL(arg_datums.prepare_selected(*param_exprs->at(0), eval_ctx_, selector))) { break; }
        ObDatum bitmap;
        if (OB_FAIL(export_hll_iteration(aggr_cell, bitmap))) {
        } else { ret = approx_count_merge_calc_batch(bitmap, aggr_cell, arg_datums, selector); }
      }
      break;
    }
    case T_FUN_GROUP_CONCAT:
    case T_FUN_MEDIAN:
    case T_FUN_GROUP_PERCENTILE_CONT:
    case T_FUN_GROUP_PERCENTILE_DISC:
    case T_FUN_WM_CONCAT:
    case T_FUN_PL_AGG_UDF:
    case T_FUN_JSON_ARRAYAGG:
    case T_FUN_ORA_JSON_ARRAYAGG:
    case T_FUN_JSON_OBJECTAGG:
    case T_FUN_ORA_JSON_OBJECTAGG: 
    case T_FUN_SYS_ST_ASMVT:
    case T_FUNC_SYS_ARRAY_AGG:
    {
      GroupConcatExtraResult *extra_info = NULL;
      if (OB_ISNULL(extra_info = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        if (OB_FAIL(group_extra_aggr_calc_batch(param_exprs, aggr_cell, aggr_info, extra_info, selector))) {
        } else if (aggr_fun == T_FUN_JSON_ARRAYAGG || aggr_fun == T_FUN_JSON_OBJECTAGG) {
          int64_t len = param_exprs->count();
          if (OB_FAIL(extra_info->reserve_bool_mark_count(len))) {
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < len; i++) {
            ObExpr *tmp = NULL;
            if (OB_FAIL(param_exprs->at(i, tmp))) {
            } else {
              bool is_bool = (tmp->is_boolean_ == 1);
              if (OB_FAIL(extra_info->set_bool_mark(i, is_bool))) {
              }
            }
          }
        }
      }
      break;
    }
    case T_FUN_HYBRID_HIST: {
      HybridHistExtraResult *extra_info = NULL;
      if (OB_ISNULL(extra_info = static_cast<HybridHistExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(selector.add_batch(param_exprs, extra_info, eval_ctx_))) {
      }
      break;
    }
    case T_FUN_TOP_FRE_HIST: {
      TopKFreHistExtraResult *extra_info = NULL;
      if (OB_ISNULL(extra_info = static_cast<TopKFreHistExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        LegacyAggrBatch arg_datums;
        if (OB_FAIL(arg_datums.prepare_selected(*param_exprs->at(0), eval_ctx_, selector))) { break; }
        if (!aggr_cell.get_is_evaluated()) {
          if (OB_FAIL(init_topk_fre_histogram_item(aggr_info, &extra_info->topk_fre_hist_))) {
          } else {
            aggr_cell.set_is_evaluated(true);
          }
        }
        if (OB_SUCC(ret)) { ret = top_fre_hist_calc_batch(aggr_info, extra_info, arg_datums, selector); }
      }
      break;
    }
    case T_FUN_SYS_BIT_AND:
    case T_FUN_SYS_BIT_OR:
    case T_FUN_SYS_BIT_XOR: {
      LegacyAggrBatch aggr_input_datums;
      if (OB_FAIL(aggr_input_datums.prepare_selected(*param_exprs->at(0), eval_ctx_, selector))) { break; }
      ret = bitwise_calc_batch(aggr_input_datums, aggr_cell, aggr_fun, selector);
      break;
    }
    case T_FUN_SUM_OPNSIZE: {
      if (OB_ISNULL(param_exprs) ||
          OB_UNLIKELY(param_exprs->count() != 1)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        DatumVector aggr_input_datums = param_exprs->at(0)->locate_expr_datumvector(eval_ctx_);
        for (uint16_t it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
          uint64_t nth_row = selector.get_batch_index(it);
          int64_t size = 0;
          if (OB_FAIL(ObExprSysOpOpnsize::calc_sys_op_opnsize(param_exprs->at(0),
                                                              aggr_input_datums.at(nth_row),
                                                              size))) {
          } else {
            int64_t origin_size = aggr_cell.get_tiny_num_int();
            int64_t new_size = origin_size + size;
            aggr_cell.set_tiny_num_int(new_size);
            aggr_cell.set_tiny_num_used();
          }
        }
      }
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }
  return ret;
}

int ObAggregateProcessor::process_aggr_result(const ObChunkDatumStore::StoredRow &stored_row,
    const ObIArray<ObExpr *> *param_exprs, AggrCell &aggr_cell, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  const ObItemType aggr_fun = aggr_info.get_expr_type();
  if (NULL != param_exprs && param_exprs->empty()) {
    if (T_FUN_COUNT == aggr_fun) {
      if (!removal_info_.is_inv_aggr_) {
        aggr_cell.inc_row_count();
      } else {
        aggr_cell.dec_row_count();
      }
    } else if (T_FUN_GROUP_ID == aggr_fun) {
      //do nothing
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  } else {
    switch (aggr_fun) {
    case T_FUN_COUNT: {
      bool has_null = false;
      for (int64_t i = 0; !has_null && i < stored_row.cnt_; ++i) {
        has_null = stored_row.stored_cells()[i].is_null();
      }
      if (!has_null) {
        if (!removal_info_.is_inv_aggr_) {
          aggr_cell.inc_row_count();
        } else {
          aggr_cell.dec_row_count();
        }
      }
      break;
    }
    case T_FUN_MAX: {
      if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("curr_row_results count is not 1", K(stored_row));
      } else if (!stored_row.stored_cells()[0].is_null()) {
        ret = max_calc(aggr_cell,
                       StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]),
                       storage_predicate_functions(*aggr_info.expr_)->null_first_cmp_,
                       aggr_info.param_exprs_.at(0)->get_sql_type());
      }
      break;
    }
    case T_FUN_MIN: {
      if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("curr_row_results count is not 1", K(stored_row));
      } else if (!stored_row.stored_cells()[0].is_null()) {
        ret = min_calc(aggr_cell,
                       StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]),
                       storage_predicate_functions(*aggr_info.expr_)->null_first_cmp_,
                       aggr_info.param_exprs_.at(0)->get_sql_type());
      }
      break;
    }
    case T_FUN_AVG: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    case T_FUN_SUM:
    case T_FUN_COUNT_SUM: {
      if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("curr_row_results count is not 1", K(stored_row));
      } else if (!stored_row.stored_cells()[0].is_null()) {
        if (!removal_info_.is_inv_aggr_) {
          ret = add_calc(StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]), aggr_cell, aggr_info);
        } else {
          ret = sub_calc(StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]), aggr_cell, aggr_info);
        }
      } else {
        if (!removal_info_.is_inv_aggr_) {
          removal_info_.null_cnt_++;
        } else {
          removal_info_.null_cnt_--;
        }
      }
      break;
    }
    case T_FUN_GROUPING: {
      //do nothing
      break;
    }
    case T_FUN_GROUPING_ID: {
      //do nothing
      break;
    }
    case T_FUN_APPROX_COUNT_DISTINCT:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS: {
      bool has_null_cell = false;
      ObDatum llc_bitmap;
      uint64_t hash_value = 0;
      if (OB_FAIL(export_hll_iteration(aggr_cell, llc_bitmap))) {
      } else if (OB_FAIL(llc_calc_hash_value(stored_row,
                                      aggr_info.param_exprs_,
                                      has_null_cell,
                                      hash_value))) {
      } else if (has_null_cell) {
       /*do nothing*/
      } else {
        ret = llc_add_value(hash_value, llc_bitmap.get_string());
      }
      break;
    }
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE: {
      if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("curr_row_results count is not 1", K(stored_row));
      } else {
        ObDatum bitmap;
        if (OB_FAIL(export_hll_iteration(aggr_cell, bitmap))) {
        } else { ret = llc_add(bitmap, StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0])); }
      }
      break;
    }
    case T_FUN_GROUP_CONCAT:
    case T_FUN_MEDIAN:
    case T_FUN_GROUP_PERCENTILE_CONT:
    case T_FUN_GROUP_PERCENTILE_DISC:
    case T_FUN_WM_CONCAT:
    case T_FUN_PL_AGG_UDF:
    case T_FUN_JSON_ARRAYAGG:
    case T_FUN_ORA_JSON_ARRAYAGG:
    case T_FUN_JSON_OBJECTAGG:
    case T_FUN_ORA_JSON_OBJECTAGG: 
    case T_FUN_SYS_ST_ASMVT:
    case T_FUNC_SYS_ARRAY_AGG:
    {
      GroupConcatExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (param_exprs == NULL && OB_FAIL(extra->add_row(stored_row))) {
      } else if (param_exprs != NULL && OB_FAIL(extra->add_row(*param_exprs, eval_ctx_))) {
      } else {
        LOG_DEBUG("succ to add row", K(stored_row), KPC(extra));
      }
      break;
    }
    case T_FUN_HYBRID_HIST: {
      HybridHistExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<HybridHistExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (param_exprs == NULL && OB_FAIL(extra->add_sort_row(stored_row))) {
        LOG_WARN("fail to add row");
      } else if (param_exprs != NULL && OB_FAIL(extra->add_sort_row(*param_exprs, eval_ctx_))) {
        LOG_WARN("fail to add row");
      } else {
        LOG_DEBUG("succ to add row", K(stored_row), KPC(extra));
      }
      break;
    }
    case T_FUN_TOP_FRE_HIST: {
      TopKFreHistExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<TopKFreHistExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(stored_row.cnt_ != 1 ||
                             aggr_info.param_exprs_.count() != 1 ||
                 OB_ISNULL(aggr_info.param_exprs_.at(0))) ||
                 OB_ISNULL(aggr_info.param_exprs_.at(0)->basic_funcs_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (extra->topk_fre_hist_.is_need_merge_topk_hist()) {
        DatumValue value;
        const SqlTypeMeta &meta = aggr_info.param_exprs_.at(0)->obj_meta_;
        if (OB_FAIL(ResultRowView::project_value(StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]), meta,
                ObDatum::get_sql_value_datum_map_type(meta.get_type()), SqlTypeMeta(), value, true))) {
        } else if (OB_FAIL(extra->topk_fre_hist_.merge_distribute_top_k_fre_items(value))) {
        }
      } else {
        common::ObArenaAllocator tmp_alloctor("CalcTopkHist", OB_MALLOC_NORMAL_BLOCK_SIZE);
        const SqlTypeMeta obj_meta = aggr_info.param_exprs_.at(0)->obj_meta_;
        ObDatum new_prev_datum;
        if (!obj_meta.is_lob_storage() &&
            OB_FAIL(shadow_truncate_stored_string_for_hist(obj_meta, stored_row, 0))) {
        } else if (obj_meta.is_lob_storage() &&
                   OB_FAIL(ObHybridHistograms::build_prefix_str_datum_for_lob(tmp_alloctor,
                                                                              obj_meta,
                                                                              StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]),
                                                                              new_prev_datum,
                                                                              datum_access_ctx_))) {
        } else {
          const ObDatum datum = obj_meta.is_lob_storage() ? new_prev_datum : StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]);
          common::ObDatumHashFuncType hash_func = storage_predicate_functions(*aggr_info.param_exprs_.at(0))->murmur_hash_;
          uint64_t datum_value = 0;
          if (OB_FAIL(hash_func(datum, datum_value, datum_value, datum_access_ctx_))) {
          } else if (OB_FAIL(extra->topk_fre_hist_.add_top_k_frequency_item(datum_value, datum, obj_meta))) {
          }
        }
      }
      break;
    }
    case T_FUN_SYS_BIT_AND:
    case T_FUN_SYS_BIT_OR:
    case T_FUN_SYS_BIT_XOR: {
      if (OB_UNLIKELY(stored_row.cnt_ != 1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("curr_row_results count is not 1", K(stored_row));
      } else if (!stored_row.stored_cells()[0].is_null()) {
        uint64_t left_uint = aggr_cell.get_tiny_num_uint();
        uint64_t right_uint = StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]).get_uint();
        uint64_t res_uint = 0;
        if (aggr_fun == T_FUN_SYS_BIT_AND) {
          res_uint = left_uint & right_uint;
        } else if (aggr_fun == T_FUN_SYS_BIT_OR) {
          res_uint = left_uint | right_uint;
        } else if (aggr_fun == T_FUN_SYS_BIT_XOR) {
          res_uint = left_uint ^ right_uint;
        }
        aggr_cell.set_tiny_num_uint(res_uint);
        aggr_cell.set_tiny_num_used();
      }
      break;
    }
    case T_FUN_SUM_OPNSIZE: {
      int64_t size = 0;
      ObDatum input;
      Datum native_input;
      if (OB_UNLIKELY(stored_row.cnt_ != 1) ||
          OB_ISNULL(param_exprs) ||
          OB_UNLIKELY(param_exprs->count() != 1)) {
        ret = OB_INVALID_ARGUMENT;
      } else if (FALSE_IT(input = StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[0]))) {
      } else if (OB_FAIL(param_exprs->at(0)->get_datum_copy_plan().import_legacy(input, native_input))) {
      } else if (OB_FAIL(ObExprSysOpOpnsize::calc_sys_op_opnsize(param_exprs->at(0),
                                                                 &native_input,
                                                                 size))) {
      } else {
        int64_t origin_size = aggr_cell.get_tiny_num_int();
        int64_t new_size = origin_size + size;
        aggr_cell.set_tiny_num_int(new_size);
        aggr_cell.set_tiny_num_used();
      }
      break;
    }
    default:
      LOG_WARN("unknown aggr function type", K(aggr_fun), K(ret));
      break;
    }
  }
  return ret;
}

int ObAggregateProcessor::extend_concat_str_buf(
  const ObString &pad_str,
  const ObCollationType cs_type,
  const int64_t pos,
  const int64_t group_concat_cur_row_num,
  int64_t &append_len,
  bool &buf_is_full)
{
  int ret = OB_SUCCESS;
  int64_t tmp_max_len = pad_str.length() + pos;
  if (0 == cur_concat_buf_len_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (tmp_max_len > cur_concat_buf_len_) {
    if (cur_concat_buf_len_ < concat_str_max_len_) {
      // From 1024 start, then double each time to expand, and it cannot be less than the total memory size after current padding
      cur_concat_buf_len_ = cur_concat_buf_len_ * 2 < tmp_max_len ?
                            next_pow2(tmp_max_len) :
                            cur_concat_buf_len_ * 2;
      if (cur_concat_buf_len_ > concat_str_max_len_) {
        cur_concat_buf_len_ = concat_str_max_len_;
      }
      char *tmp_concat_str_buf = nullptr;
      if (OB_ISNULL(tmp_concat_str_buf = static_cast<char *>(aggr_alloc_.alloc(cur_concat_buf_len_)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        if (0 < pos) {
          MEMCPY(tmp_concat_str_buf, concat_str_buf_, pos);
        }
        concat_str_buf_ = tmp_concat_str_buf;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (pad_str.length() + pos > concat_str_max_len_) {
      append_len = concat_str_max_len_ - pos;
      buf_is_full = true;
      int64_t well_formed_len = 0;
      int32_t well_formed_error = 0;
      if (OB_FAIL(ObCharset::well_formed_len(cs_type,
                                              pad_str.ptr(),
                                              append_len,
                                              well_formed_len,
                                              well_formed_error))) {
      } else {
        append_len = well_formed_len;
        LOG_USER_WARN(OB_ERR_CUT_VALUE_GROUP_CONCAT, group_concat_cur_row_num + 1);
      }
    }
  }
  return ret;
}
int ObAggregateProcessor::collect_aggr_result(
  AggrCell &aggr_cell, const ObExpr *diff_expr,
  const ObAggrInfo &aggr_info, const int64_t cur_group_id,
  const int64_t max_group_cnt/* =INT64_MIN*/)
{
  int ret = OB_SUCCESS;
  alignas(8) char result_buffer[ObNumber::MAX_CALC_BYTE_LEN + 64];
  common::ObDatum result;
  result.ptr_ = result_buffer;
  bool has_lob_header = aggr_info.expr_->obj_meta_.has_lob_header();
  const ObItemType aggr_fun = aggr_info.get_expr_type();
  bool result_published = false;
  switch (aggr_fun) {
    case T_FUN_COUNT: {
      result.set_int(aggr_cell.get_row_count());
      break;
    }
    case T_FUN_COUNT_SUM:
    case T_FUN_SUM: {
      const SqlTypeClass tc = sql_type_class(aggr_info.get_first_child_type());
      if (OB_FAIL(aggr_cell.collect_result(tc, eval_ctx_, aggr_info))) {
      } else {
      }
      break;
    }
    case T_FUN_AVG: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
    case T_FUN_GROUPING: {
      bool null_result = false;
      int64_t new_value = aggr_cell.get_tiny_num_int();
      if (aggr_info.hash_rollup_info_) {
        if (OB_UNLIKELY(!aggr_cell.get_is_evaluated())) {
          null_result = true;
        }
      } else {
        if (diff_expr != NULL && diff_expr == aggr_info.param_exprs_.at(0)) {
          new_value = 1;
          aggr_cell.set_tiny_num_int(new_value);
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_UNLIKELY(null_result)) {
        result.set_null();
      } else {
        result.set_int(new_value);
      }
      break;
    }
    case T_FUN_GROUPING_ID: {
      if (aggr_info.hash_rollup_info_) {
        if (OB_UNLIKELY(!aggr_cell.get_is_evaluated())) {
          result.set_null();
        } else {
          result.set_int(aggr_cell.get_tiny_num_int());
        }
      } else {
        uint64_t new_value = aggr_cell.get_tiny_num_uint();
        if (cur_group_id == max_group_cnt) {
          // last rollup, should calc it manually.
          // normal query only. Batch rollup shouldn't reach here
          new_value = 0;
          for (int64_t i = 0; i < aggr_info.grouping_idxs_.count(); i++) {
            new_value = new_value << 1;
            int64_t grouping_idx = aggr_info.grouping_idxs_.at(i);
            if (grouping_idx >= cur_group_id) { new_value++; }
          }
        }
        if (OB_FAIL(ret)) {
        } else {
          result.set_int(new_value);
        }
      }
      break;
    }
    case T_FUN_GROUP_ID: {
      uint64_t new_value = aggr_cell.get_tiny_num_uint();
      if (cur_group_id == max_group_cnt) {
        //last rollup, should calc it manually
        bool match = false;
        for (int64_t i = 0; !match && i < aggr_info.group_idxs_.count(); i++) {
          if (cur_group_id == aggr_info.group_idxs_.at(i)) {
            new_value++;
            match = true;
          }
        }
        if (!match) {
          new_value = 0;
        }
      }
      result.set_int(new_value);
      break;
    }
    case T_FUN_MAX:
    case T_FUN_MIN:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS:
    case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE: {
      ret = aggr_cell.export_iter(semantic_iteration_type(aggr_info),
          result, result_buffer, sizeof(result_buffer));
      break;
    }
    case T_FUN_APPROX_COUNT_DISTINCT: {
      int64_t tmp_result = OB_INVALID_COUNT;
      ObDatum bitmap;
      if (OB_FAIL(export_hll_iteration(aggr_cell, bitmap))) { break; }
      ObExprEstimateNdv::llc_estimate_ndv(tmp_result, bitmap.get_string());
      if (tmp_result >= 0) {
        result.set_int(tmp_result);
      }
      break;
    }

    case T_FUN_WM_CONCAT: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_wm_concat_result(aggr_info, extra, result))) {
      } else {
      }
      break;
    }
    case T_FUN_JSON_ARRAYAGG: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_json_arrayagg_result(aggr_info, extra, result))) {
      } else {
      }
      break;
    }

    case T_FUN_ORA_JSON_ARRAYAGG: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_ora_json_arrayagg_result(aggr_info, extra, result))) {
      } else {
      }
      break;
    }

    case T_FUN_JSON_OBJECTAGG: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_json_objectagg_result(aggr_info, extra, result))) {
      } else {
      }
      break;
    }

    case T_FUN_ORA_JSON_OBJECTAGG: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_ora_json_objectagg_result(aggr_info, extra, result))) {
      } else {
      }
      break;
    }

    case T_FUN_SYS_ST_ASMVT: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_asmvt_result(aggr_info, extra, result))) {
      } else {
      }
      break;      
    }
    case T_FUNC_SYS_ARRAY_AGG: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_array_agg_result(aggr_info, extra, result))) {
      } else {
      }
      break;      
    }
    case T_FUN_GROUP_CONCAT: {
      GroupConcatExtraResult *extra = NULL;
      ObString sep_str;
      if (NULL == concat_str_buf_) {
        uint64_t concat_str_max_len = (OB_DEFAULT_GROUP_CONCAT_MAX_LEN);
        if (OB_FAIL(eval_ctx_.exec_ctx_.get_my_session()->get_group_concat_max_len(concat_str_max_len))) {
        } else if (0 != cur_concat_buf_len_) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("first time concat buf len is not 0", K(cur_concat_buf_len_));
        } else {
          // Iterate from 1024
          cur_concat_buf_len_ = concat_str_max_len > 1024 ? 1024 : concat_str_max_len;
          if (OB_ISNULL(concat_str_buf_ = static_cast<char *>(aggr_alloc_.alloc(cur_concat_buf_len_)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          } else {
            concat_str_max_len_ = concat_str_max_len;
          }
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(extra->empty())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
        // Group concat row may be iterated in rollup_process(), rewind here.
      } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
      } else {
        if (aggr_info.separator_expr_ != NULL) {
          if (!extra->has_separator()) {
            if (OB_UNLIKELY(aggr_info.separator_expr_->is_const_expr())) {
              ret = OB_ERR_UNEXPECTED;
            } else if (cur_group_id == max_group_cnt) {//last rollup
              LegacyAggrValue separator_storage;
              ObDatum *separator_result = nullptr;
              if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
                ret = OB_ERR_UNEXPECTED;
              } else if (OB_FAIL(eval_legacy_aggr(*aggr_info.separator_expr_, eval_ctx_, separator_storage, separator_result))) {
              } else {
                sep_str = separator_result->get_string();
              }
            } else {
              sep_str = ObString::make_empty_string();
            }
          } else {
            alignas(8) char separator_scratch[64];
            ObDatum separator_view;
            if (OB_FAIL(extra->export_separator(aggr_info.separator_expr_->get_datum_copy_plan(),
                separator_view, separator_scratch, sizeof(separator_scratch)))) {
            } else {
              // String spans borrow the retained owner; no scalar scratch escapes.
              sep_str = separator_view.get_string();
            }
          }
        } else {
          // Default is comma
          sep_str = ObCharsetUtils::get_const_str(aggr_info.expr_->datum_meta_.cs_type_, ',');
        }
      }

      if (OB_SUCC(ret)) {
        int64_t pos = 0;
        bool buf_is_full = false;
        const ObChunkDatumStore::StoredRow *storted_row = NULL;
        int64_t group_concat_cur_row_num = 0;
        bool first_item_printed = false;
        const int64_t group_concat_param_count = aggr_info.group_concat_param_count_;
        const ObCollationType cs_type = aggr_info.expr_->datum_meta_.cs_type_;
        while(OB_SUCC(ret)
              && !buf_is_full
              && OB_SUCC(extra->get_next_row(storted_row))) {
          bool should_skip = false;
          for (int64_t i = 0; !should_skip && i < group_concat_param_count; ++i) {
            // As long as one cell is null, the entire item is skipped
            should_skip = storted_row->stored_cells()[i].is_null();
          }


          if (!should_skip) {
            if (group_concat_cur_row_num != 0) {
              int64_t append_len = sep_str.length();
              if (OB_FAIL(extend_concat_str_buf(
                  sep_str, cs_type, pos, group_concat_cur_row_num, append_len, buf_is_full))) {
              } else if (cur_concat_buf_len_ < append_len + pos) {
                ret = OB_ERR_UNEXPECTED;
              } else if (0 < append_len) {
                MEMCPY(concat_str_buf_ + pos, sep_str.ptr(), append_len);
                pos += append_len;
              }
            }
            for (int64_t i = 0; OB_SUCC(ret) && !buf_is_full && i < group_concat_param_count; ++i) {
              const ObString cell_string = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[i]).get_string();
              int64_t append_len = cell_string.length();
              if (OB_FAIL(extend_concat_str_buf(cell_string, cs_type, pos,
                  group_concat_cur_row_num, append_len, buf_is_full))) {
              } else if (cur_concat_buf_len_ < append_len + pos) {
                ret = OB_ERR_UNEXPECTED;
              } else if (0 < append_len) {
                MEMCPY(concat_str_buf_ + pos, cell_string.ptr(), append_len);
                pos += append_len;
              }
            }//end of for

            if (OB_SUCC(ret)) {
              ++group_concat_cur_row_num;
              first_item_printed |= (!first_item_printed);
            }
          }
        }//end of while

        if (ret != OB_ITER_END && ret != OB_SUCCESS) {
        } else {
          ret = OB_SUCCESS;
          ObDatum concat_result;
          if (first_item_printed) {
            concat_result.set_string(concat_str_buf_, pos);
          } else {
            concat_result.set_null(); // all were skipped, then set to null
          }
          OX(LOG_DEBUG("group concat finish ", K(group_concat_cur_row_num), K(pos),
              K(concat_result), KPC(extra)));
          result = concat_result;
        }
      }
      break;
    }
    case T_FUN_GROUP_PERCENTILE_CONT:
    case T_FUN_GROUP_PERCENTILE_DISC:
    case T_FUN_MEDIAN: {
      GroupConcatExtraResult *extra = NULL;
      if (OB_ISNULL(extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(extra->empty())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
        // Group concat row may be iterated in rollup_process(), rewind here.
      } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
      } else if (1 != aggr_info.sort_collations_.count() ||
                 aggr_info.sort_collations_.at(0).field_idx_ >=
                 aggr_info.param_exprs_.count()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const int64_t param_idx = 0;
        const int64_t obj_idx = aggr_info.sort_collations_.at(0).field_idx_;
        const ObChunkDatumStore::StoredRow *storted_row = NULL;
        const int64_t total_row_count = extra->get_row_count();
        char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
        ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
        ObChunkDatumStore::LastStoredRow prev_row(aggr_alloc_);
        ObNumber factor;
        bool need_linear_inter = false;
        int64_t not_null_start_loc = 0;
        int64_t dest_loc = 0;
        int64_t row_cnt = 0;
        while (OB_SUCC(ret) && 0 == not_null_start_loc
                            && OB_SUCC(extra->get_next_row(storted_row))) {
          if (OB_ISNULL(storted_row)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            ++ row_cnt;
            if (!storted_row->stored_cells()[obj_idx].is_null()) {
              not_null_start_loc = row_cnt;
            }
          }
        }
        if (ret != OB_ITER_END && ret != OB_SUCCESS) {
        } else if (ret == OB_ITER_END) {
          /* do nothing */
        } else if (T_FUN_MEDIAN == aggr_fun) {
            if (1 == (total_row_count - not_null_start_loc) % 2) {
              need_linear_inter = true;
              if (OB_FAIL(factor.from(ObNumber::get_positive_zero_dot_five(),
                                      allocator))) {
              }
            }
            dest_loc = not_null_start_loc + (total_row_count - not_null_start_loc) / 2;
        } else if (OB_FAIL(get_percentile_param(aggr_info, StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[param_idx]),
                                                not_null_start_loc, total_row_count,
                                                dest_loc, need_linear_inter,
                                                factor, allocator))) {
        }
        while (OB_SUCC(ret) && row_cnt < dest_loc
                            && OB_SUCC(extra->get_next_row(storted_row))) {
          if (OB_ISNULL(storted_row)) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            ++ row_cnt;
          }
        }
        if (ret == OB_ITER_END && 0 == not_null_start_loc) {
          ret = OB_SUCCESS;
          result.set_null();
        } else if (OB_FAIL(ret)) {
        } else if (OB_FAIL(prev_row.save_store_row(*storted_row))) {
        } else if (need_linear_inter) {
          ObDatum tmp_datum;
          if (OB_FAIL(extra->get_next_row(storted_row))) {
          } else if (OB_ISNULL(storted_row)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(linear_inter_calc(aggr_info,
                                               StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[obj_idx]),
                                               StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[obj_idx]),
                                               factor, tmp_datum))) {
            LOG_WARN("failed to calc linear inter", K(ret),
                                                    K(StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[obj_idx])),
                                                    K(StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[obj_idx])),
                                                    K(factor));
          } else {
            result = tmp_datum;
            LOG_DEBUG("get median result", K(factor),
                                           K(StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[obj_idx])),
                                           K(StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[obj_idx])),
                                           K(not_null_start_loc),
                                           K(dest_loc),
                                           K(need_linear_inter),
                                           K(result));
          }
        } else if (FALSE_IT(result = StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[obj_idx]))) {
          LOG_WARN("clone cell failed", K(ret), K(StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[obj_idx])));
        } else {
          LOG_DEBUG("get median result", K(StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[obj_idx])),
                                         K(not_null_start_loc),
                                         K(dest_loc),
                                         K(need_linear_inter),
                                         K(result));
        }
        if (OB_SUCC(ret)) {
          ret = publish_legacy_aggr(*aggr_info.expr_, eval_ctx_, result);
          result_published = OB_SUCC(ret);
        }
      }
      break;
    }
    case T_FUN_TOP_FRE_HIST: {
      TopKFreHistExtraResult *extra = static_cast<TopKFreHistExtraResult *>(aggr_cell.get_extra());
      if (OB_ISNULL(extra)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(get_top_k_fre_hist_result(extra->topk_fre_hist_,
                                                   aggr_info.param_exprs_.at(0)->obj_meta_,
                                                   has_lob_header,
                                                   result))) {
      } else {
      }
      break;
    }
    case T_FUN_PL_AGG_UDF: {
      GroupConcatExtraResult *extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(get_pl_agg_udf_result(aggr_info, extra, result))) {
      } else {
        LOG_TRACE("succeed to get pl agg udf result");
      }
      break;
    }
    case T_FUN_HYBRID_HIST: {
      HybridHistExtraResult *extra = static_cast<HybridHistExtraResult *>(aggr_cell.get_extra());
      if (OB_FAIL(compute_hybrid_hist_result(aggr_info, extra, result))) {
      } else {
        LOG_TRACE("succeed to get pl agg udf result");
      }
      break;
    }
    case T_FUN_SYS_BIT_AND:
    case T_FUN_SYS_BIT_OR:
    case T_FUN_SYS_BIT_XOR: {
      result.set_uint(aggr_cell.get_tiny_num_uint());
      break;
    }
    case T_FUN_SUM_OPNSIZE: {
      int64_t size = aggr_cell.get_tiny_num_int();
      if (size > 0) {
        result.set_int(size);
      }
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown aggr function type", K(aggr_fun));
      break;
  } // end switch

  if (OB_SUCC(ret) && !result_published && aggr_fun != T_FUN_SUM && aggr_fun != T_FUN_COUNT_SUM) {
    ret = publish_legacy_aggr(*aggr_info.expr_, eval_ctx_, result);
  }
  if (OB_SUCC(ret)) { aggr_info.expr_->set_evaluated_projected(eval_ctx_); }
  return ret;
}
//
// Find dst_op, filter out any added sys cast,
//
//
//fill linear_inter_expr input parameters and calculate
// Calculate res = factor * (right - left) + left
//
int ObAggregateProcessor::linear_inter_calc(const ObAggrInfo &aggr_info,
                                            const ObDatum &prev_datum,
                                            const ObDatum &curr_datum,
                                            const ObNumber &factor,
                                            ObDatum &res)
{
  int ret = OB_SUCCESS;
  auto ctx = static_cast<LinearInterAggrFuncCtx *>(get_aggr_func_ctx(aggr_info));
  ObExpr *order_expr = aggr_info.param_exprs_.at(aggr_info.sort_collations_.at(0).field_idx_);
  res.set_null();
  if (OB_UNLIKELY(T_QUESTIONMARK == order_expr->type_)) {
    res.set_datum(curr_datum);
  } else if (NULL == ctx) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (NULL == ctx->linear_inter_) {
      // setup runtime datum arithmetic
      ObRTDatumArith *arith = OB_NEWx(ObRTDatumArith, (&eval_ctx_.exec_ctx_.get_allocator()),
                                      eval_ctx_.exec_ctx_, *eval_ctx_.exec_ctx_.get_my_session());
      if (NULL == arith) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        ObDatumMeta factor_meta;
        factor_meta.type_ = ObNumberType;
        factor_meta.cs_type_ = CS_TYPE_BINARY;
        factor_meta.scale_
            = ObAccuracy::DDL_DEFAULT_ACCURACY2[0][ObNumberType].get_scale();
        factor_meta.precision_
            = ObAccuracy::DDL_DEFAULT_ACCURACY2[0][ObNumberType].get_precision();
        if (OB_FAIL(arith->setup_datum_metas(factor_meta /* factor meta */,
                                             order_expr->datum_meta_ /* prev meta */,
                                             order_expr->datum_meta_ /* cur meta */))) {
        } else {
          auto factor_item = arith->ref(0);
          auto prev_item = arith->ref(1);
          auto cur_item = arith->ref(2);
          if (OB_FAIL(arith->generate(factor_item * (cur_item - prev_item) + prev_item))) {
          } else if (NULL == arith->get_expr()) {
            ret = OB_ERR_UNEXPECTED;
          } else if (order_expr->datum_meta_.type_ != arith->get_expr()->datum_meta_.type_) {
            ret = OB_ERR_UNEXPECTED;
          }
        }

        if (OB_SUCC(ret)) {
          ctx->linear_inter_ = arith;
        } else {
          if (NULL != arith) {
            arith->~ObRTDatumArith();
          }
        }
      }
    }

    if (OB_SUCC(ret)) {
      char factor_nmb_buf[OBJ_DATUM_NUMBER_RES_SIZE];
      ObDatum factor_datum;
      factor_datum.ptr_ = factor_nmb_buf;
      factor_datum.set_number(factor);

      Datum native_factor, native_prev, native_curr;
      Datum *eval_res = nullptr;
      if (OB_FAIL(native_factor.set_number_shallow(*reinterpret_cast<const number::ObCompactNumber *>(factor_datum.ptr_)))) {
      } else if (OB_FAIL(order_expr->get_datum_copy_plan().import_legacy(prev_datum, native_prev))) {
      } else if (OB_FAIL(order_expr->get_datum_copy_plan().import_legacy(curr_datum, native_curr))) {
      } else if (OB_FAIL(ctx->linear_inter_->eval(eval_res, native_factor, native_prev, native_curr))) {
      } else if (OB_ISNULL(eval_res)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = ctx->linear_inter_->get_expr()->get_datum_copy_plan().legacy_view(
            *eval_res, res, ctx->legacy_result_scratch_, sizeof(ctx->legacy_result_scratch_));
      }
    }
  }
  return ret;
}
//
// Calculate percentile_cont/percentile_disc output row position, if interpolation is needed calculate interpolation factor fator
//
int ObAggregateProcessor::get_percentile_param(const ObAggrInfo &aggr_info,
                                               const ObDatum &param,
                                               const int64_t not_null_start_loc,
                                               const int64_t total_row_count,
                                               int64_t &dest_loc,
                                               bool &need_linear_inter,
                                               ObNumber &factor,
                                               ObDataBuffer &allocator)
{
  int ret = OB_SUCCESS;
  need_linear_inter = false;
  int64_t scale = 0;
  int64_t int64_value = 0;
  char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN * 5];
  ObDataBuffer local_allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN * 5);
  const ObDatumMeta param_datum_meta = aggr_info.param_exprs_.at(0)->datum_meta_;
  ObNumber percentile;
  if (!ob_is_number_tc(param_datum_meta.type_)) {//type deduction has already been converted to number}
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(percentile.from(ObNumber(param.get_number()), local_allocator))) {
  } else if (percentile.is_negative()
             || 0 < percentile.compare(ObNumber::get_positive_one())) {//judge greater than 1 or less than 0}
    ret = OB_ERR_PERCENTILE_VALUE_INVALID;
  } else if (T_FUN_GROUP_PERCENTILE_DISC == aggr_info.get_expr_type()) {
    //
    //  PERCENTILE_DISC calculation process
    //  rows sorted by null first, ignore null during calculation
    //  percentile value (P) and the number of rows (N)
    //  dest_loc = ceil(N * P) + not_null_start_loc - 1
    //  Special case: When P = 0, directly read from not_null_start_loc
    //
    ObNumber row_count, rn;
    if (percentile.is_zero()) {
      dest_loc = not_null_start_loc;
    } else if (OB_FAIL(row_count.from(total_row_count - not_null_start_loc + 1,
                                      local_allocator))) {
    } else if (OB_FAIL(percentile.mul(row_count, rn, local_allocator, false))) {
    } else if (OB_FAIL(rn.ceil(scale))) {
    } else if (!rn.is_int64()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(rn.cast_to_int64(int64_value))) {
    } else {
      dest_loc = not_null_start_loc + int64_value - 1;
    }
  } else if (T_FUN_GROUP_PERCENTILE_CONT == aggr_info.get_expr_type()) {
    //
    //  PERCENTILE_CONT calculation process
    //  rows sorted by null first, ignore null during calculation
    //  percentile value (P) and the number of rows (N), row number RN = (1 + (P * (N - 1))
    //  CRN = CEILING(RN) and FRN = FLOOR(RN)
    //  If (CRN = FRN = RN) then the result is
    //    (value of expression from row at RN)
    //  Otherwise the result is
    //    (CRN - RN) * (value of expression for row at FRN) +
    //    (RN - FRN) * (value of expression for row at CRN)
    //    = (RN - FRN) * (row at CRN - row at FRN) + row at FRN
    //
    //  The program sets factor = (RN - FRN)
    //  FRN position dest_loc = not_null_start_loc + RN - 1
    //  Need interpolation when calc_linear_inter function calculates factor * (obj2 - obj1) + obj1
    //
    ObNumber row_count, rn, frn, res;
    if (OB_FAIL(row_count.from(total_row_count - not_null_start_loc, local_allocator))) {
    } else if (OB_FAIL(percentile.mul(row_count, rn, local_allocator, false))) {
    } else if (OB_FAIL(frn.from(rn, local_allocator))) {
    } else if (OB_FAIL(frn.floor(scale))) {
    } else if (OB_FAIL(rn.sub(frn, res, local_allocator))) {
    } else if (!frn.is_int64()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(frn.cast_to_int64(int64_value))) {
    } else if (OB_FAIL(factor.from(res, allocator))) {
    } else if (rn.is_integer()) {
      dest_loc = not_null_start_loc + int64_value;
    } else {
      need_linear_inter = true;
      dest_loc = not_null_start_loc + int64_value;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObAggregateProcessor::max_calc(AggrCell &aggr_cell, const ObDatum &other,
    ObDatumCmpFuncType cmp_func, const SqlType &input_type)
{
  int ret = OB_SUCCESS;
  alignas(8) char base_scratch[64];
  ObDatum base;
  if (OB_FAIL(aggr_cell.export_semantic_iter(input_type, base, base_scratch, sizeof(base_scratch)))) {
    return ret;
  }
  if (OB_ISNULL(cmp_func)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!base.is_null() && !other.is_null()) {
    int cmp_ret = 0;
    if (OB_FAIL(cmp_func(base, other, cmp_ret, datum_access_ctx_))) {
    } else if (cmp_ret < 0) {
      ret = clone_aggr_cell(aggr_cell, other, input_type);
      removal_info_.is_index_change_ = true;
    }
  } else if (/*base.is_null() &&*/ !other.is_null()) {
    // base must be null!
    // if base is not null, the first 'if' will be match, not this 'else if'.
    ret = clone_aggr_cell(aggr_cell, other, input_type);
    removal_info_.is_index_change_ = true;
  } else {
    // nothing.
  }
  return ret;
}

int ObAggregateProcessor::min_calc(AggrCell &aggr_cell, const ObDatum &other,
    ObDatumCmpFuncType cmp_func, const SqlType &input_type)
{
  int ret = OB_SUCCESS;
  alignas(8) char base_scratch[64];
  ObDatum base;
  if (OB_FAIL(aggr_cell.export_semantic_iter(input_type, base, base_scratch, sizeof(base_scratch)))) {
    return ret;
  }
  if (OB_ISNULL(cmp_func)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!base.is_null() && !other.is_null()) {
    int cmp_ret = 0;
    if (OB_FAIL(cmp_func(base, other, cmp_ret, datum_access_ctx_))) {
    } else if (cmp_ret > 0) {
      ret = clone_aggr_cell(aggr_cell, other, input_type);
      removal_info_.is_index_change_ = true;
    }
  } else if (/*base.is_null() &&*/ !other.is_null()) {
    // base must be null!
    // if base is not null, the first 'if' will be match, not this 'else if'.
    ret = clone_aggr_cell(aggr_cell, other, input_type);
    removal_info_.is_index_change_ = true;
  } else {
    // nothing.
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::max_calc_batch(
  AggrCell &aggr_cell,
  const ObDatumVector &src, ObDatumCmpFuncType cmp_func, const SqlType &input_type,
  const T &selector)
{
  int ret = OB_SUCCESS;
  alignas(8) char base_scratch[64];
  ObDatum dst;
  if (OB_FAIL(aggr_cell.export_semantic_iter(input_type, dst, base_scratch, sizeof(base_scratch)))) {
    return ret;
  }
  if (OB_UNLIKELY(OB_ISNULL(cmp_func))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!selector.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObDatum *max = nullptr;
    uint16_t i = 0; // row num in a batch
    int cmp_ret = 0;
    for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
      i = selector.get_batch_index(it);
      if (max && !src.at(i)->is_null()) {
        if (OB_FAIL(cmp_func(*max, *src.at(i), cmp_ret, datum_access_ctx_))) {
        } else if (cmp_ret < 0) {
          max = src.at(i);
        }
      } else if (!src.at(i)->is_null()) {
        if (dst.is_null()) {
          max = src.at(i);
        } else if (OB_FAIL(cmp_func(dst, *src.at(i), cmp_ret, datum_access_ctx_))) {
        } else if (cmp_ret < 0) {
          max = src.at(i);
        }
      }
    }
    if (OB_SUCC(ret) && max) {
      ret = clone_aggr_cell(aggr_cell, *(max), input_type);
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::min_calc_batch(
    AggrCell &aggr_cell,
    const ObDatumVector &src, ObDatumCmpFuncType cmp_func, const SqlType &input_type,
    const T &selector)
{
  int ret = OB_SUCCESS;
  alignas(8) char base_scratch[64];
  ObDatum dst;
  if (OB_FAIL(aggr_cell.export_semantic_iter(input_type, dst, base_scratch, sizeof(base_scratch)))) {
    return ret;
  }
  if (OB_UNLIKELY(OB_ISNULL(cmp_func))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(!selector.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObDatum *min = nullptr;
    uint16_t i = 0; // row num in a batch
    int cmp_ret = 0;
    for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
      i = selector.get_batch_index(it);
      if (min && !src.at(i)->is_null()) {
        if (OB_FAIL(cmp_func(*min, *src.at(i), cmp_ret, datum_access_ctx_))) {
        } else if (cmp_ret > 0) {
          min = src.at(i);
        }
      } else if (!src.at(i)->is_null()) {
        if (dst.is_null()) {
          min = src.at(i);
        } else if (OB_FAIL(cmp_func(dst, *src.at(i), cmp_ret, datum_access_ctx_))) {
        } else if (cmp_ret > 0) {
          min = src.at(i);
        }
      }
    }
    if (OB_SUCC(ret) && min) {
      ret = clone_aggr_cell(aggr_cell, *(min), input_type);
    }
  }
  return ret;
}

int ObAggregateProcessor::prepare_add_calc(
  const ObDatum &first_value, AggrCell &aggr_cell, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  const SqlTypeClass column_tc = sql_type_class(aggr_info.get_first_child_type());
  alignas(8) char iter_scratch[64];
  ObDatum result_datum;
  if (OB_FAIL(aggr_cell.export_iter(semantic_iteration_type(aggr_info),
      result_datum, iter_scratch, sizeof(iter_scratch)))) { return ret; }
  switch (column_tc) {
    case ObIntTC: {
      aggr_cell.set_tiny_num_int(first_value.get_int());
      aggr_cell.set_tiny_num_used();
      break;
    }
    case ObUIntTC: {
      aggr_cell.set_tiny_num_uint(first_value.get_uint());
      aggr_cell.set_tiny_num_used();
      break;
    }
    case ObFloatTC:
    case ObDoubleTC: {
      ret = clone_kernel_cell(aggr_cell, first_value,
          IterKernelLayout(column_tc == ObFloatTC ? IterValueDomain::Float32 : IterValueDomain::Float64),
          &result_datum, iter_scratch, sizeof(iter_scratch));
      break;
    }
    case ObCollectionSQLTC: {
      ret = clone_vector_cell(first_value, aggr_cell,
          aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
      break;
    }
    case ObNumberTC: {
      ret = clone_kernel_cell(aggr_cell, first_value,
          IterKernelLayout(IterValueDomain::Number), &result_datum);
      break;
    }
    case ObDecimalIntTC: {
      DecIntAggFuncCtx *ctx = nullptr;
      if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const int64_t need_size = sizeof(int512_t);
        const int16_t scale = aggr_info.get_first_child_datum_scale();
        if (OB_FAIL(reserve_kernel_cell(aggr_cell,
            IterKernelLayout(IterValueDomain::DecimalCoefficient, sizeof(int512_t)), need_size, result_datum))) {
        } else if (OB_FAIL(ctx->store_func(result_datum,
                                           first_value.get_decimal_int(), scale))) {
        } else {
          ret = clone_kernel_cell(aggr_cell, result_datum,
              ctx->number_result_ ? IterKernelLayout(IterValueDomain::Number)
                  : IterKernelLayout(IterValueDomain::DecimalCoefficient, result_datum.len_), &result_datum);
        }
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("bot support now", K(column_tc));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "rollup contain agg udfs");
    }
  }
  return ret;
}

// if use tiny_num, final result is tiny_num add iter_result.
// left from tiny_num, right from iter_value, result_datum from iter_result.
int ObAggregateProcessor::add_calc(
  const ObDatum &iter_value, AggrCell &aggr_cell, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  const SqlTypeClass column_tc = sql_type_class(aggr_info.get_first_child_type());
  alignas(8) char iter_scratch[64];
  ObDatum result_datum;
  if (OB_FAIL(aggr_cell.export_iter(semantic_iteration_type(aggr_info),
      result_datum, iter_scratch, sizeof(iter_scratch)))) { return ret; }
  switch (column_tc) {
    case ObIntTC: {
      int64_t left_int = aggr_cell.get_tiny_num_int();
      int64_t right_int = iter_value.get_int();
      int64_t sum_int = left_int + right_int;
      if (ObExprAdd::is_int_int_out_of_range(left_int, right_int, sum_int)) {
        if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
          ret = calc_overflow_res_to_decimal_int<int64_t>(result_datum, aggr_cell, precision,
                                                          left_int, right_int);
        } else { // number
          char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          ObNumber result_nmb;
          if (!result_datum.is_null()) {
            ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                    result_datum.get_number());
            result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
          }
          if (OB_FAIL(result_nmb.add(left_int, right_int, result_nmb, allocator))) {
          } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell, &result_datum))) {
          }
        }
        aggr_cell.set_tiny_num_int(0);
      } else {
        aggr_cell.set_tiny_num_int(sum_int);
      }
      aggr_cell.set_tiny_num_used();
      break;
    }
    case ObUIntTC: {
      uint64_t left_uint = aggr_cell.get_tiny_num_uint();
      uint64_t right_uint = iter_value.get_uint();
      uint64_t sum_uint = left_uint + right_uint;
      if (ObExprAdd::is_uint_uint_out_of_range(left_uint, right_uint, sum_uint)) {
        if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
          ret = calc_overflow_res_to_decimal_int<uint64_t>(result_datum, aggr_cell, precision,
                                                           left_uint, right_uint);
        } else { // number
          char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          ObNumber result_nmb;
          if (!result_datum.is_null()) {
            ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                            result_datum.get_number());
            result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
          }
          if (OB_FAIL(result_nmb.add(left_uint, right_uint, result_nmb, allocator))) {
          } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell, &result_datum))) {
          }
        }
        aggr_cell.set_tiny_num_uint(0);
      } else {
        aggr_cell.set_tiny_num_uint(sum_uint);
      }
      aggr_cell.set_tiny_num_used();
      break;
    }
    case ObFloatTC: {
      if (result_datum.is_null()) {
        ret = clone_kernel_cell(aggr_cell, iter_value,
            IterKernelLayout(IterValueDomain::Float32), &result_datum, iter_scratch, sizeof(iter_scratch));
      } else {
        float left_f = result_datum.get_float();
        float right_f = iter_value.get_float();
        if (OB_UNLIKELY(ObArithExprOperator::is_float_out_of_range(left_f + right_f))) {
          ret = OB_OPERATE_OVERFLOW;
          char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
          int64_t pos = 0;
          databuff_printf(expr_str,
                          OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                          pos,
                          "'(%e + %e)'", left_f, right_f);
          LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BINARY_FLOAT", expr_str);
          LOG_WARN("float out of range", K(left_f), K(right_f));
        } else {
          result_datum.set_float(left_f + right_f);
          ret = clone_kernel_cell(aggr_cell, result_datum,
              IterKernelLayout(IterValueDomain::Float32));
        }
      }
      break;
    }
    case ObDoubleTC: {
      if (result_datum.is_null()) {
        ret = clone_kernel_cell(aggr_cell, iter_value,
            IterKernelLayout(IterValueDomain::Float64), &result_datum, iter_scratch, sizeof(iter_scratch));
      } else {
        double left_d = result_datum.get_double();
        double right_d = iter_value.get_double();
        result_datum.set_double(left_d + right_d);
        ret = clone_kernel_cell(aggr_cell, result_datum,
              IterKernelLayout(IterValueDomain::Float64));
      }
      break;
    }
    case ObNumberTC: {
      ObNumber right_nmb(iter_value.get_number());
      removal_info_.is_out_of_range_ = right_nmb.fast_sum_agg_may_overflow();
      if (result_datum.is_null()) {
        ret = clone_kernel_cell(aggr_cell, iter_value,
            IterKernelLayout(IterValueDomain::Number), &result_datum);
      } else {
        ObNumber left_nmb(result_datum.get_number());
        removal_info_.is_out_of_range_ = removal_info_.is_out_of_range_ || left_nmb.fast_sum_agg_may_overflow();
        char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
        ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
        const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
        ObNumber result_nmb;
        if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, allocator, strict_mode))) {
        } else {
          ret = clone_number_cell(result_nmb, aggr_cell, &result_datum);
        }
      }
      break;
    }
    case ObDecimalIntTC: {
      DecIntAggFuncCtx *ctx = nullptr;
      const int16_t scale = aggr_info.get_first_child_datum_scale();
      if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(result_datum.is_null())) {
        const int64_t need_size = sizeof(int512_t);
        if (OB_FAIL(reserve_kernel_cell(aggr_cell,
            IterKernelLayout(IterValueDomain::DecimalCoefficient, sizeof(int512_t)), need_size, result_datum))) {
        } else if (OB_FAIL(ctx->store_func(result_datum, iter_value.get_decimal_int(), scale))) {
        } else {
          ret = clone_kernel_cell(aggr_cell, result_datum,
              ctx->number_result_ ? IterKernelLayout(IterValueDomain::Number)
                  : IterKernelLayout(IterValueDomain::DecimalCoefficient, result_datum.len_), &result_datum);
        }
      } else if (OB_FAIL(ctx->add_func(result_datum, iter_value.get_decimal_int(), scale))) {
      } else {
        ret = clone_kernel_cell(aggr_cell, result_datum,
            ctx->number_result_ ? IterKernelLayout(IterValueDomain::Number)
                : IterKernelLayout(IterValueDomain::DecimalCoefficient, result_datum.len_), &result_datum);
      }
      break;
    }
    case ObCollectionSQLTC: {
      if (result_datum.is_null()) {
        ret = clone_vector_cell(iter_value, aggr_cell,
            aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
      } else {
        ObString blob_res = result_datum.get_string();
        ObLobLocatorV2 locator(blob_res, true/*has_lob_header*/);
        bool is_outrow = !locator.has_inrow_data();
        ObDatum tmp_datum;
        if (OB_FAIL(add_legacy_aggr_vector(eval_ctx_.exec_ctx_, *aggr_info.param_exprs_.at(0),
            result_datum, iter_value, aggr_alloc_,
            is_outrow ? &tmp_datum : nullptr))) {
        } else {
          ret = clone_vector_cell(is_outrow ? tmp_datum : result_datum, aggr_cell,
              aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

template<typename T, bool IS_ADD>
int ObAggregateProcessor::calc_overflow_res_to_decimal_int(ObDatum &result_datum,
                                                           AggrCell &aggr_cell,
                                                           const int16_t precision,
                                                           const T val1,
                                                           const T val2)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(result_datum.is_null())) {
    // init first datum for agg cell
    const int64_t need_size = sizeof(int256_t);
    if (OB_FAIL(reserve_kernel_cell(aggr_cell,
        IterKernelLayout(IterValueDomain::DecimalCoefficient, sizeof(int256_t)), need_size, result_datum))) {
    } else {
      const char *buf = result_datum.ptr_;
      MEMSET(const_cast<char *> (buf), 0, sizeof(int256_t)); // set to zero
    }
  }
  if (OB_SUCC(ret)) {
    if (IS_ADD) {
      ret = DecIntAggFuncCtx::add_int(result_datum, precision, val1, val2);
    } else {
      ret = DecIntAggFuncCtx::sub_int(result_datum, precision, val1);
    }
  }
  if (OB_SUCC(ret)) {
    ret = clone_kernel_cell(aggr_cell, result_datum,
        IterKernelLayout(IterValueDomain::DecimalCoefficient, result_datum.len_), &result_datum);
  }
  return ret;
}

// if use tiny_num, final result is tiny_num add iter_result.
// left_value from tiny_num, right_value from iter_value, result_datum from iter_result.
int ObAggregateProcessor::sub_calc(
  const ObDatum &iter_value, AggrCell &aggr_cell, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  const SqlTypeClass column_tc = sql_type_class(aggr_info.get_first_child_type());
  alignas(8) char iter_scratch[64];
  ObDatum result_datum;
  if (OB_FAIL(aggr_cell.export_iter(semantic_iteration_type(aggr_info),
      result_datum, iter_scratch, sizeof(iter_scratch)))) { return ret; }
  switch (column_tc) {
    case ObIntTC: {
      // If out of range, use result_datum sub right_value, not change left_value.
      // If not out of range, use left_value sub right_value, not change result_datum.
      int64_t left_int = aggr_cell.get_tiny_num_int();
      int64_t right_int = iter_value.get_int();
      int64_t dif_int = left_int - right_int;
      if (ObExprMinus::is_int_int_out_of_range(left_int, right_int, dif_int)) {
        if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
          ret = calc_overflow_res_to_decimal_int<int64_t, false>(result_datum, aggr_cell, precision,
                                                                 right_int);
        } else { // number
          char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          char int_buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          ObDataBuffer int_allocator(int_buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          ObNumber result_nmb;
          ObNumber right_num;
          if (!result_datum.is_null()) {
            ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                    result_datum.get_number());
            result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
          }
          if (OB_FAIL(right_num.from(right_int, int_allocator))) {
          } else if (OB_FAIL(result_nmb.sub_v3(right_num, result_nmb, allocator))) {
          } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell, &result_datum))) {
          } else {
            // maintain tiny num int
          }
        }
      } else {
        aggr_cell.set_tiny_num_int(dif_int);
      }
      aggr_cell.set_tiny_num_used();
      break;
    }
    case ObUIntTC: {
      // If out of range, get ULONG_MAX from result_datum add to left_value.
      //    Then left_value will turn to left_value + ULONG_MAX - right.
      // If not out of range, use left_value sub right_value, not change result_datum.
      uint64_t left_uint = aggr_cell.get_tiny_num_uint();
      uint64_t right_uint = iter_value.get_uint();
      uint64_t dif_uint = left_uint - right_uint;
      if (ObExprMinus::is_uint_uint_out_of_range(left_uint, right_uint, dif_uint)) {
        if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
          const uint64_t uint_max = (uint64_t)ULONG_MAX;
          ret = calc_overflow_res_to_decimal_int<uint64_t, false>(result_datum, aggr_cell,
                                                                  precision, uint_max);
        } else { // number
          char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          char uint_buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          ObDataBuffer uint_allocator(uint_buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          const bool strict_mode = false;
          ObNumber result_nmb;
          ObNumber uint_max_nmb;
          if (OB_UNLIKELY(result_datum.is_null())) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                            result_datum.get_number());
            result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(uint_max_nmb.from((uint64_t)ULONG_MAX, uint_allocator))) {
          } else if (OB_FAIL(result_nmb.sub_v3(uint_max_nmb, result_nmb, allocator, strict_mode))) {
          } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell, &result_datum))) {
          }
        }
        aggr_cell.set_tiny_num_uint((uint64_t)ULONG_MAX - right_uint + left_uint);
      } else {
        aggr_cell.set_tiny_num_uint(dif_uint);
      }
      aggr_cell.set_tiny_num_used();
      break;
    }
    case ObNumberTC: {
      ObNumber right_nmb(iter_value.get_number());
      removal_info_.is_out_of_range_ = right_nmb.fast_sum_agg_may_overflow();
      if (result_datum.is_null()) {
        ret = clone_kernel_cell(aggr_cell, iter_value,
            IterKernelLayout(IterValueDomain::Number), &result_datum);
      } else {
        ObNumber left_nmb(result_datum.get_number());
        removal_info_.is_out_of_range_ = removal_info_.is_out_of_range_ || left_nmb.fast_sum_agg_may_overflow();
        char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
        ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
        const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
        ObNumber result_nmb;
        if (OB_FAIL(left_nmb.sub_v3(right_nmb, result_nmb, allocator, strict_mode))) {
        } else {
          ret = clone_number_cell(result_nmb, aggr_cell, &result_datum);
        }
      }
      break;
    }
    case ObDecimalIntTC: {
      DecIntAggFuncCtx *ctx = nullptr;
      const int16_t scale = aggr_info.get_first_child_datum_scale();
      if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ctx->sub_func(result_datum, iter_value.get_decimal_int(), scale))) {
      } else {
        ret = clone_kernel_cell(aggr_cell, result_datum,
            ctx->number_result_ ? IterKernelLayout(IterValueDomain::Number)
                : IterKernelLayout(IterValueDomain::DecimalCoefficient, result_datum.len_), &result_datum);
      }
      break;
    }
    case ObCollectionSQLTC: {
      if (result_datum.is_null()) {
        ret = clone_vector_cell(iter_value, aggr_cell,
            aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
      } else {
        ObString blob_res = result_datum.get_string();
        ObLobLocatorV2 locator(blob_res, true/*has_lob_header*/);
        bool is_outrow = !locator.has_inrow_data();
        ObDatum tmp_datum;
        if (OB_FAIL(add_legacy_aggr_vector(eval_ctx_.exec_ctx_, *aggr_info.param_exprs_.at(0),
            result_datum, iter_value, aggr_alloc_,
            is_outrow ? &tmp_datum : nullptr, true /*negative*/))) {
        } else {
          ret = clone_vector_cell(is_outrow ? tmp_datum : result_datum, aggr_cell,
              aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::init_group_extra_aggr_info(
  AggrCell &aggr_cell,
  const ObAggrInfo &aggr_info,
  const T &selector
)
{
  int ret = OB_SUCCESS;
  GroupConcatExtraResult *extra = NULL;
  if (OB_ISNULL(extra = static_cast<GroupConcatExtraResult *>(aggr_cell.get_extra()))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    extra->reuse_self();
    if (aggr_info.separator_expr_ != NULL) {
      Datum *separator_result = nullptr;
      if (aggr_info.separator_expr_->is_const_expr()) {
        if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(aggr_info.separator_expr_->eval(eval_ctx_, separator_result))) {
        } else {
          // Capture before collect: the expression may already hold the next group then.
          ret = extra->copy_separator(*separator_result,
              aggr_info.separator_expr_->get_datum_copy_plan());
        }
      } else if (!aggr_info.separator_expr_->is_const_expr()) {
        if (OB_UNLIKELY(!aggr_info.separator_expr_->obj_meta_.is_string_type() ||
                        selector.begin() >= selector.end())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const uint16_t batch_idx = selector.get_batch_index(selector.begin());
          ret = extra->copy_separator(aggr_info.separator_expr_->locate_expr_datum(eval_ctx_, batch_idx),
              aggr_info.separator_expr_->get_datum_copy_plan());
        }
      }
    } else {
      /* nothing to do*/
    }
  }
  return ret;
}

int ObAggregateProcessor::ObBatchRowsSlice::add_batch(
  const ObIArray<ObExpr *> *param_exprs,
  ExtraResult *ad_result,
  GroupConcatExtraResult *extra_info,
  ObEvalCtx &eval_ctx
) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ad_result)) {
    int64_t stored_rows_count = 0;
    HashBasedDistinctExtraResult *extra_result = static_cast<HashBasedDistinctExtraResult *>(ad_result);
    if (!extra_result->is_inited()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (nullptr != extra_result->unique_sort_op_) {
      if (OB_FAIL(extra_result->unique_sort_op_->add_batch(
          *param_exprs, *brs_->skip_, end_pos_, begin_pos_, &stored_rows_count))) {
      }
    } else {
      if (OB_FAIL(extra_result->insert_row_for_batch(
          *param_exprs, end_pos_, brs_->skip_, begin_pos_))) {
      }
    }
  } else if (OB_NOT_NULL(extra_info->sort_op_)) {
    int64_t stored_rows_count = 0;
    if (OB_FAIL(extra_info->sort_op_->add_batch(
        *param_exprs, *brs_->skip_, end_pos_, begin_pos_, &stored_rows_count))) {
    } else {
      extra_info->row_count_ += stored_rows_count;
    }
  } else {
    int64_t stored_rows_count = 0;
    if (OB_FAIL(extra_info->row_store_.add_batch(
        *param_exprs, eval_ctx, *brs_->skip_, end_pos_, stored_rows_count, nullptr, begin_pos_))) {
    } else {
      extra_info->row_count_ += stored_rows_count;
    }
  }
  return ret;
}

int ObAggregateProcessor::ObBatchRowsSlice::add_batch(
  const ObIArray<ObExpr *> *param_exprs,
  HybridHistExtraResult *extra_info,
  ObEvalCtx &eval_ctx
) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(extra_info->sort_op_)) {
    int64_t stored_rows_count = 0;
    if (OB_FAIL(extra_info->sort_op_->add_batch(
        *param_exprs, *brs_->skip_, end_pos_, begin_pos_, &stored_rows_count))) {
    } else {
      extra_info->sort_row_count_ += stored_rows_count;
    }
  }
  return ret;
}

int ObAggregateProcessor::ObSelector::add_batch(
  const ObIArray<ObExpr *> *param_exprs,
  ExtraResult *ad_result,
  GroupConcatExtraResult *extra_info,
  ObEvalCtx &eval_ctx
) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ad_result)) {
    HashBasedDistinctExtraResult *extra_result = static_cast<HashBasedDistinctExtraResult *>(ad_result);
    if (!extra_result->is_inited()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (nullptr != extra_result->unique_sort_op_) {
      if (OB_FAIL(extra_result->unique_sort_op_->add_batch(
          *param_exprs, *brs_->skip_, brs_->size_, selector_array_, count_))) {
      }
    } else {
      if (OB_FAIL(extra_result->insert_row_for_batch(
          *param_exprs, brs_->size_, brs_->skip_))) {
      }
    } 
  } else if (OB_NOT_NULL(extra_info->sort_op_)) {
    if (OB_FAIL(extra_info->sort_op_->add_batch(
        *param_exprs, *brs_->skip_, brs_->size_, selector_array_, count_))) {
    } else {
      extra_info->row_count_ += count_;
    }
  } else {
    int64_t stored_rows_count = 0;
    if (OB_FAIL(extra_info->row_store_.add_batch(
        *param_exprs, eval_ctx, *brs_->skip_, brs_->size_, selector_array_, count_, nullptr))) {
    } else {
      extra_info->row_count_ += count_;
    }
  }
  return ret;
}

int ObAggregateProcessor::ObSelector::add_batch(
  const ObIArray<ObExpr *> *param_exprs,
  HybridHistExtraResult *extra_info,
  ObEvalCtx &eval_ctx
) const
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(extra_info->sort_op_)) {
    if (OB_FAIL(extra_info->sort_op_->add_batch(
        *param_exprs, *brs_->skip_, brs_->size_, selector_array_, count_))) {
    } else {
      extra_info->sort_row_count_ += count_;
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::top_fre_hist_calc_batch(
  const ObAggrInfo &aggr_info,
  TopKFreHistExtraResult *extra_info,
  const ObDatumVector &arg_datums,
  const T &selector
)
{
  int ret = OB_SUCCESS;
  const SqlTypeMeta obj_meta = aggr_info.param_exprs_.at(0)->obj_meta_;
  for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
    uint64_t nth_row = selector.get_batch_index(it);
    ObDatum *datum = arg_datums.at(nth_row);
    int32_t origin_str_len = 0;
    common::ObArenaAllocator tmp_alloctor("BatchTopkHist", OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObDatum new_prev_datum;
    if (datum->is_null()) {
      continue;
    }
    if (extra_info->topk_fre_hist_.is_need_merge_topk_hist()) {//merge
      DatumValue value;
      if (OB_FAIL(ResultRowView::project_value(*datum, obj_meta,
              ObDatum::get_sql_value_datum_map_type(obj_meta.get_type()), SqlTypeMeta(), value, true))) {
      } else if (OB_FAIL(extra_info->topk_fre_hist_.merge_distribute_top_k_fre_items(value))) {
      }
    } else if (!obj_meta.is_lob_storage() &&
               OB_FAIL(shadow_truncate_string_for_hist(obj_meta, *datum, &origin_str_len))) {
    } else if (obj_meta.is_lob_storage() &&
               OB_FAIL(ObHybridHistograms::build_prefix_str_datum_for_lob(tmp_alloctor,
                                                                          obj_meta,
                                                                          *datum,
                                                                          new_prev_datum,
                                                                          datum_access_ctx_))) {
    } else {
      datum = obj_meta.is_lob_storage() ? &new_prev_datum : datum;
      common::ObDatumHashFuncType hash_func = storage_predicate_functions(*aggr_info.param_exprs_.at(0))->murmur_hash_;
      uint64_t datum_value = 0;
      if (OB_FAIL(hash_func(*datum, datum_value, datum_value, datum_access_ctx_))) {
      } else if (OB_FAIL(extra_info->topk_fre_hist_.add_top_k_frequency_item(datum_value, *datum, obj_meta))) {
      } else if (origin_str_len > 0) {
        if (OB_UNLIKELY(!ObColumnStatParam::is_valid_opt_col_type(obj_meta.get_type()) ||
                        !obj_meta.is_string_type())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObString &str = datum->get_string();
          datum->set_string(str.ptr(), static_cast<uint32_t>(origin_str_len));
        }
      }
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::group_extra_aggr_calc_batch(
  const ObIArray<ObExpr *> *param_exprs,
  AggrCell &aggr_cell,
  const ObAggrInfo &aggr_info,
  GroupConcatExtraResult *extra_info,
  const T &selector
)
{
  int ret = OB_SUCCESS;
  if (!aggr_cell.get_is_evaluated()) {
    if (OB_FAIL(init_group_extra_aggr_info(aggr_cell, aggr_info, selector))) {
    } else {
      aggr_cell.set_is_evaluated(true);
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(selector.add_batch(param_exprs, nullptr, extra_info, eval_ctx_))) {
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::approx_count_merge_calc_batch(
  ObDatum &dst,
  AggrCell &aggr_cell,
  const ObDatumVector &arg_datums,
  const T &selector
)
{
  int ret = OB_SUCCESS;
  for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
    uint64_t nth_row = selector.get_batch_index(it);
    if (arg_datums.at(nth_row)->is_null()) {
      continue;
    }
    if (!aggr_cell.get_is_evaluated()) {
      // init firstly
      ret = clone_kernel_cell(aggr_cell, *arg_datums.at(nth_row),
          IterKernelLayout(IterValueDomain::HllRegisters, 0, arg_datums.at(nth_row)->len_), &dst);
      aggr_cell.set_is_evaluated(true);
    } else {
      ret = llc_add(dst, *arg_datums.at(nth_row));
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::approx_count_calc_batch(
  ObDatum &dst,
  const ObIArray<ObExpr *> *param_exprs,
  const T &selector
)
{
  int ret = OB_SUCCESS;
  int64_t nth_row = 0;
  for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
    uint64_t hash_value = 0;
    bool has_null_cell = false;
    for (int64_t nth_arg = 0; OB_SUCC(ret) && !has_null_cell && nth_arg < param_exprs->count(); ++nth_arg) {
      ObExpr *expr = param_exprs->at(nth_arg);
      DatumVector arg_datums = expr->locate_expr_datumvector(eval_ctx_);
      nth_row = selector.get_batch_index(it);
      if (arg_datums.at(nth_row)->is_null()) {
        has_null_cell = true;
      }
      OB_ASSERT(NULL != expr->basic_funcs_);
      ObExprHashFuncType hash_func = expr->basic_funcs_->murmur_hash_;
      if (OB_FAIL(hash_func(
              *arg_datums.at(nth_row), hash_value, hash_value, datum_access_ctx_))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (!has_null_cell) {
        ret = llc_add_value(hash_value, dst.get_string());
      }
    }
  }
  return ret;
}

template <typename T>
int ObAggregateProcessor::add_calc_batch(
    const ObDatumVector &src, AggrCell &aggr_cell, const ObAggrInfo &aggr_info,
    const T &selector)
{
  int ret = OB_SUCCESS;
  const SqlTypeClass column_tc = sql_type_class(aggr_info.get_first_child_type());
  alignas(8) char iter_scratch[64];
  ObDatum result_datum;
  if (OB_FAIL(aggr_cell.export_iter(semantic_iteration_type(aggr_info),
      result_datum, iter_scratch, sizeof(iter_scratch)))) { return ret; }
  switch (column_tc) {
    case ObIntTC: {
      char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
      int64_t left_int  = 0;
      int64_t right_int = 0;
      int64_t sum_int   = 0;
      ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
      uint16_t i = 0; // row num in a batch
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (src.at(i)->is_null()) {
          continue;
        }
        left_int  = aggr_cell.get_tiny_num_int();
        right_int = src.at(i)->get_int();
        sum_int   = left_int + right_int;
        ObNumber result_nmb;
        if (ObExprAdd::is_int_int_out_of_range(left_int, right_int, sum_int)) {
          if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
            const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
            ret = calc_overflow_res_to_decimal_int<int64_t>(result_datum, aggr_cell, precision,
                                                            left_int, right_int);
          } else { // number
            if (!result_datum.is_null()) {
              ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                      result_datum.get_number());
              result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
            }
            if (OB_FAIL(result_nmb.add(left_int, right_int, result_nmb, allocator))) {
            } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell, &result_datum))) {
            } else {
              allocator.free();
            }
          }
          aggr_cell.set_tiny_num_int(0);
        } else {
          aggr_cell.set_tiny_num_int(sum_int);
          aggr_cell.set_tiny_num_used();
        }
      }
      break;
    }
    case ObUIntTC: {
      char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
      ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
      uint64_t left_uint  = 0;
      uint64_t right_uint = 0;
      uint64_t sum_uint   = 0;
      ObNumber result_nmb;
      uint16_t i = 0; // row num in a batch
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (src.at(i)->is_null()) {
          continue;
        }
        left_uint  = aggr_cell.get_tiny_num_uint();
        right_uint = src.at(i)->get_uint();
        sum_uint   = left_uint + right_uint;
        if (ObExprAdd::is_uint_uint_out_of_range(left_uint, right_uint, sum_uint)) {
          if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
            const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
            ret = calc_overflow_res_to_decimal_int<uint64_t>(result_datum, aggr_cell, precision,
                                                             left_uint, right_uint);
          } else { // number
            if (!result_datum.is_null()) {
              ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                              result_datum.get_number());
              result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
            }
            if (OB_FAIL(result_nmb.add(left_uint, right_uint, result_nmb, allocator))) {
            } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell, &result_datum))) {
            } else {
              allocator.free();
            }
          }
          aggr_cell.set_tiny_num_uint(0);
        } else {
          aggr_cell.set_tiny_num_uint(sum_uint);
          aggr_cell.set_tiny_num_used();
        }
      }
      break;
    }
    case ObFloatTC: {
      float left_f  = 0.0;
      float right_f = 0.0;
      uint16_t i = 0; // row num in a batch
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (src.at(i)->is_null()) {
          continue;
        }
        if (result_datum.is_null()) {
          ret = clone_kernel_cell(aggr_cell, *src.at(i),
              IterKernelLayout(IterValueDomain::Float32), &result_datum, iter_scratch, sizeof(iter_scratch));
        } else {
          left_f  = result_datum.get_float();
          right_f = src.at(i)->get_float();
          if (OB_UNLIKELY(ObArithExprOperator::is_float_out_of_range(left_f + right_f))) {
            ret = OB_OPERATE_OVERFLOW;
            char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
            int64_t pos = 0;
            databuff_printf(expr_str,
                            OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                            pos,
                            "'(%e + %e)'", left_f, right_f);
            LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BINARY_FLOAT", expr_str);
            LOG_WARN("float out of range", K(left_f), K(right_f));
          } else {
            result_datum.set_float(left_f + right_f);
            ret = clone_kernel_cell(aggr_cell, result_datum,
                IterKernelLayout(IterValueDomain::Float32));
          }
        }
      }
      break;
    }
    case ObDoubleTC: {
      double left_d  = 0.0;
      double right_d = 0.0;
      uint16_t i = 0; // row num in a batch
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (src.at(i)->is_null()) {
          continue;
        }
        if (result_datum.is_null()) {
          ret = clone_kernel_cell(aggr_cell, *src.at(i),
              IterKernelLayout(IterValueDomain::Float64), &result_datum, iter_scratch, sizeof(iter_scratch));
        } else {
          left_d  = result_datum.get_double();
          right_d = src.at(i)->get_double();
          result_datum.set_double(left_d + right_d);
          ret = clone_kernel_cell(aggr_cell, result_datum,
                IterKernelLayout(IterValueDomain::Float64));
        }
      }
      break;
    }
    case ObNumberTC: {
      ObNumber result_nmb;
      char buf_alloc1[ObNumber::MAX_CALC_BYTE_LEN];
      char buf_alloc2[ObNumber::MAX_CALC_BYTE_LEN];
      uint32_t sum_digits_buf[ObNumber::OB_CALC_BUFFER_SIZE];
      MEMSET(sum_digits_buf, 0, ObNumber::MAX_CALC_BYTE_LEN);
      ObDataBuffer allocator1(buf_alloc1, ObNumber::MAX_CALC_BYTE_LEN);
      ObDataBuffer allocator2(buf_alloc2, ObNumber::MAX_CALC_BYTE_LEN);
      bool all_skip = true;
      if (!result_datum.is_null()) {
          result_nmb.assign(result_datum.get_number_desc().desc_,
                      const_cast<uint32_t *>(result_datum.get_number_digits()));
      }
      if (OB_FAIL(number_accumulator(src, allocator1, allocator2, result_nmb,
                                     sum_digits_buf, all_skip, selector))) {
      } else if (!all_skip) {
        ret = clone_number_cell(result_nmb, aggr_cell, &result_datum);
      }
      break;
    }
    case ObDecimalIntTC: {
      DecIntAggFuncCtx *ctx = nullptr;
      const int16_t scale = aggr_info.get_first_child_datum_scale();
      if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ctx->add_batch_func[T::DECIMAL_INT_BATCH_FUNC_IDX](
          result_datum, this, aggr_cell, src, &selector, scale))) {
      } else if (!result_datum.is_null()) {
        ret = clone_kernel_cell(aggr_cell, result_datum,
            ctx->number_result_ ? IterKernelLayout(IterValueDomain::Number)
                : IterKernelLayout(IterValueDomain::DecimalCoefficient, result_datum.len_), &result_datum);
      }
      break;
    }
    case ObCollectionSQLTC: {
      uint16_t i = 0; // row num in a batch
      for (uint16_t it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (src.at(i)->is_null()) {
          continue;
        }
        if (result_datum.is_null()) {
          ret = clone_vector_cell(*src.at(i), aggr_cell,
              aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
        } else {
          ObString blob_res = result_datum.get_string();
          ObLobLocatorV2 locator(blob_res, true/*has_lob_header*/);
          bool is_outrow = !locator.has_inrow_data();
          ObDatum tmp_datum;
          if (OB_FAIL(add_legacy_aggr_vector(eval_ctx_.exec_ctx_, *aggr_info.param_exprs_.at(0),
              result_datum, *src.at(i), aggr_alloc_,
              is_outrow ? &tmp_datum : nullptr))) {
          } else {
            ret = clone_vector_cell(is_outrow ? tmp_datum : result_datum, aggr_cell,
                aggr_info.param_exprs_.at(0)->get_sql_type(), &result_datum, iter_scratch, sizeof(iter_scratch));
          }
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  return ret;
}

template <typename T>
int ObAggregateProcessor::bitwise_calc_batch(
    const ObDatumVector &src,
    AggrCell &aggr_cell,
    const ObItemType &aggr_func,
    const T &selector)
{
  int ret = OB_SUCCESS;
  uint64_t cur_uint = 0;
  uint64_t res_uint = 0;
  // In batch mode, it will not call prepare_aggr_result() function to get initial value.
  // So we do alternative initialization here.
  if (aggr_cell.is_tiny_num_used()) {
    res_uint = aggr_cell.get_tiny_num_uint();
  } else if (aggr_func == T_FUN_SYS_BIT_AND) {
    res_uint = UINT_MAX_VAL[ObUInt64Type];
  }
  uint16_t i = 0; // row num in a batch
  switch (aggr_func) {
    case T_FUN_SYS_BIT_AND: {
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (!src.at(i)->is_null()) {
          cur_uint = src.at(i)->get_uint();
          res_uint &= cur_uint;
        }
      }
      break;
    }
    case T_FUN_SYS_BIT_OR: {
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (!src.at(i)->is_null()) {
          cur_uint = src.at(i)->get_uint();
          res_uint |= cur_uint;
        }
      }
      break;
    }
    case T_FUN_SYS_BIT_XOR: {
      for (auto it = selector.begin(); OB_SUCC(ret) && it < selector.end(); selector.next(it)) {
        i = selector.get_batch_index(it);
        if (!src.at(i)->is_null()) {
          cur_uint = src.at(i)->get_uint();
          res_uint ^= cur_uint;
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown aggr function type", K(aggr_func));
      break;
    }
  }
  aggr_cell.set_tiny_num_uint(res_uint);
  aggr_cell.set_tiny_num_used();
  return ret;
}

template<typename T>
int ObAggregateProcessor::grouping_calc_batch(const ObAggrInfo &aggr_info, AggrCell &aggr_cell, const T &selector)
{
  int ret = OB_SUCCESS;
  ObExpr *rollup_grouping_id = aggr_info.hash_rollup_info_->rollup_grouping_id_;
  DatumVector src = rollup_grouping_id->locate_expr_datumvector(eval_ctx_);
  for (decltype(selector.begin()) it = selector.begin();
       OB_SUCC(ret) && !aggr_cell.get_is_evaluated() && it < selector.end();
       selector.next(it)) {
    uint16_t idx = selector.get_batch_index(it);
    aggr_cell.set_tiny_num_int(share::aggregate::is_grouping(aggr_info, src.at(idx)->get_int()));
    aggr_cell.set_is_evaluated(true);
  }
  return ret;
}

template<typename T>
int ObAggregateProcessor::grouping_id_calc_batch(const ObAggrInfo &aggr_info, AggrCell &aggr_cell, const T &selector)
{
  int ret = OB_SUCCESS;
  ObExpr *rollup_grouping_id = aggr_info.hash_rollup_info_->rollup_grouping_id_;
  DatumVector src = rollup_grouping_id->locate_expr_datumvector(eval_ctx_);
  int64_t res_int = -1;
  for (decltype(selector.begin()) it = selector.begin();
      OB_SUCC(ret) && !aggr_cell.get_is_evaluated() && it < selector.end();
      selector.next(it)) {
    uint16_t idx = selector.get_batch_index(it);
    if (OB_FAIL(share::aggregate::get_grouping_id(aggr_info, src.at(idx)->get_int(), &res_int))) {
    } else {
      aggr_cell.set_tiny_num_int(res_int);
      aggr_cell.set_is_evaluated(true);
    }
  }
  return ret;
}

int ObAggregateProcessor::rollup_add_number_calc(const ObDatum &aggr_result, AggrCell &aggr_cell)
{
  int ret = OB_SUCCESS;
  ObDatum rollup_result;
  if (OB_FAIL(aggr_cell.export_kernel_iter(IterKernelLayout(IterValueDomain::Number),
      rollup_result, nullptr, 0))) { return ret; }
  if (aggr_result.is_null()) {
    //do nothing
  } else if (rollup_result.is_null()) {
    if (OB_FAIL(clone_kernel_cell(aggr_cell, aggr_result,
        IterKernelLayout(IterValueDomain::Number), &rollup_result))) {
    }
  } else {
    char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
    ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
    const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
    ObNumber left_nmb(aggr_result.get_number());
    ObNumber right_nmb(rollup_result.get_number());
    ObNumber result_nmb;
    if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, allocator, strict_mode))) {
    } else if (OB_FAIL(clone_number_cell(result_nmb, aggr_cell))) {
    }
  }
  return ret;
}

int ObAggregateProcessor::rollup_add_decimalint_calc(const ObDatum &aggr_result,
                                                     AggrCell &rollup_cell,
                                                     const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  alignas(8) char target_scratch[64];
  ObDatum rollup_result;
  if (OB_FAIL(rollup_cell.export_iter(nullptr, rollup_result, target_scratch, sizeof(target_scratch)))) {
    return ret;
  }
  DecIntAggFuncCtx *ctx = nullptr;
  if (aggr_result.is_null()) {
    // do nothing
  } else if (rollup_result.is_null()) {
    if (OB_FAIL(clone_kernel_cell(rollup_cell, aggr_result,
        IterKernelLayout(IterValueDomain::DecimalCoefficient, aggr_result.len_), &rollup_result))) {
    }
  } else if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ctx->merge_func(rollup_result, aggr_result.get_decimal_int(), 0 /*unused*/))) {
  } else {
    ret = clone_kernel_cell(rollup_cell, rollup_result,
        ctx->number_result_ ? IterKernelLayout(IterValueDomain::Number)
            : IterKernelLayout(IterValueDomain::DecimalCoefficient, rollup_result.len_));
  }
  return ret;
}

int ObAggregateProcessor::rollup_add_calc(
  const AggrCell &aggr_cell, AggrCell &rollup_cell, const ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  const SqlTypeClass column_tc = sql_type_class(aggr_info.get_first_child_type());
  alignas(8) char source_scratch[64], target_scratch[64];
  ObDatum aggr_result, rollup_result;
  if (OB_FAIL(aggr_cell.export_iter(semantic_iteration_type(aggr_info),
      aggr_result, source_scratch, sizeof(source_scratch)))) { return ret; }
  if (OB_FAIL(rollup_cell.export_iter(semantic_iteration_type(aggr_info),
      rollup_result, target_scratch, sizeof(target_scratch)))) { return ret; }
  switch (column_tc) {
    case ObIntTC: {
      if (aggr_cell.is_tiny_num_used()) {
        int64_t left_int = aggr_cell.get_tiny_num_int();
        int64_t right_int = rollup_cell.get_tiny_num_int();
        int64_t sum_int = left_int + right_int;
        if (ObExprAdd::is_int_int_out_of_range(left_int, right_int, sum_int)) {
          if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
            const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
            ret = calc_overflow_res_to_decimal_int<int64_t>(rollup_result, rollup_cell, precision,
                                                            left_int, right_int);
          } else { // number
            char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
            ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
            ObNumber result_nmb;
            if (!rollup_result.is_null()) {
              ObCompactNumber &cnum = const_cast<ObCompactNumber &>(rollup_result.get_number());
              result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
            }
            if (OB_FAIL(result_nmb.add(left_int, right_int, result_nmb, allocator))) {
            } else if (OB_FAIL(clone_number_cell(result_nmb, rollup_cell, &rollup_result))) {
            }
          }
          rollup_cell.set_tiny_num_int(0);
        } else {
          rollup_cell.set_tiny_num_int(sum_int);
          rollup_cell.set_tiny_num_used();
        }
      }
      if (OB_SUCC(ret)) {
        if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          ret = rollup_add_decimalint_calc(aggr_result, rollup_cell, aggr_info);
        } else {
          ret = rollup_add_number_calc(aggr_result, rollup_cell);
        }
      }
      break;
    }
    case ObUIntTC: {
      if (aggr_cell.is_tiny_num_used()) {
        uint64_t left_uint = aggr_cell.get_tiny_num_uint();
        uint64_t right_uint = rollup_cell.get_tiny_num_uint();
        uint64_t sum_uint = left_uint + right_uint;
        if (ObExprAdd::is_uint_uint_out_of_range(left_uint, right_uint, sum_uint)) {
          if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
            const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
            ret = calc_overflow_res_to_decimal_int<uint64_t>(rollup_result, rollup_cell, precision,
                                                             left_uint, right_uint);
          } else { // number
            char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
            ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
            ObNumber result_nmb;
            if (!rollup_result.is_null()) {
              ObCompactNumber &cnum = const_cast<ObCompactNumber &>(rollup_result.get_number());
              result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
            }
            if (OB_FAIL(result_nmb.add(left_uint, right_uint, result_nmb, allocator))) {
            } else if (OB_FAIL(clone_number_cell(result_nmb, rollup_cell, &rollup_result))) {
            }
          }
          rollup_cell.set_tiny_num_uint(0);
        } else {
          rollup_cell.set_tiny_num_uint(sum_uint);
          rollup_cell.set_tiny_num_used();
        }
      }
      if (OB_SUCC(ret)) {
        if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          ret = rollup_add_decimalint_calc(aggr_result, rollup_cell, aggr_info);
        } else {
          ret = rollup_add_number_calc(aggr_result, rollup_cell);
        }
      }
      break;
    }
    case ObNumberTC: {
      ret = rollup_add_number_calc(aggr_result, rollup_cell);
      break;
    }
    case ObDecimalIntTC: {
      ret = rollup_add_decimalint_calc(aggr_result, rollup_cell, aggr_info);
      break;
    }
    case ObFloatTC: {
      if (aggr_result.is_null()) {
        //do nothing
      } else if (rollup_result.is_null()) {
        ret = clone_kernel_cell(rollup_cell, aggr_result,
            IterKernelLayout(IterValueDomain::Float32), &rollup_result, target_scratch, sizeof(target_scratch));
      } else {
        float left_f = aggr_result.get_float();
        float right_f = rollup_result.get_float();
        if (OB_UNLIKELY(ObArithExprOperator::is_float_out_of_range(left_f + right_f))) {
          ret = OB_OPERATE_OVERFLOW;
          char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
          int64_t pos = 0;
          databuff_printf(expr_str,
                          OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                          pos,
                          "'(%e + %e)'", left_f, right_f);
          LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BINARY_FLOAT", expr_str);
          LOG_WARN("float out of range", K(left_f), K(right_f));
        } else {
          rollup_result.set_float(left_f + right_f);
          ret = clone_kernel_cell(rollup_cell, rollup_result,
              IterKernelLayout(IterValueDomain::Float32));
        }
      }
      break;
    }
    case ObDoubleTC: {
      if (aggr_result.is_null()) {
        //do nothing
      } else if (rollup_result.is_null()) {
        ret = clone_kernel_cell(rollup_cell, aggr_result,
            IterKernelLayout(IterValueDomain::Float64), &rollup_result, target_scratch, sizeof(target_scratch));
      } else {
        double left_d = aggr_result.get_double();
        double right_d = rollup_result.get_double();
        if (OB_UNLIKELY(ObArithExprOperator::is_double_out_of_range(left_d + right_d))) {
          ret = OB_OPERATE_OVERFLOW;
          char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
          int64_t pos = 0;
          databuff_printf(expr_str,
                          OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                          pos,
                          "'(%le + %le)'", left_d, right_d);
          LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DOUBLE", expr_str);
        } else {
          rollup_result.set_double(left_d + right_d);
          ret = clone_kernel_cell(rollup_cell, rollup_result,
              IterKernelLayout(IterValueDomain::Float64));
        }
      }
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObAggregateProcessor::llc_add(ObDatum &result, const ObDatum &new_value)
{
  int ret = OB_SUCCESS;
  ObString res_buf = result.get_string();
  ObString left_buf = result.get_string();
  ObString right_buf = new_value.get_string();;
  if (OB_UNLIKELY(left_buf.length() < get_llc_size())
      || OB_UNLIKELY(right_buf.length() < get_llc_size())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    //TODO::here we can use SSE
    for (int64_t i = 0; i < get_llc_size(); ++i) {
      res_buf.ptr()[i] = std::max(static_cast<uint8_t>(left_buf[i]),
                                  static_cast<uint8_t>(right_buf[i]));
    }
  }
  return ret;
}

int ObAggregateProcessor::get_llc_size()
{
  return sizeof(char ) * LLC_NUM_BUCKETS;
}

int ObAggregateProcessor::llc_add_value(const uint64_t value, const ObString &llc_bitmap_buf)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(llc_add_value(value, const_cast<char*>(llc_bitmap_buf.ptr()), llc_bitmap_buf.length()))) {
  }
  return ret;
}

int ObAggregateProcessor::llc_add_value(const uint64_t value, char *llc_bitmap_buf, int64_t size)
{
  int ret = OB_SUCCESS;
  uint64_t bucket_index = value >> (64 - LLC_BUCKET_BITS);
  uint64_t pmax = 0;
  if (0 == value << LLC_BUCKET_BITS) {
    // do nothing
  } else {
    pmax = ObExprEstimateNdv::llc_leading_zeros(value << LLC_BUCKET_BITS, 64 - LLC_BUCKET_BITS) + 1;
  }
  ObString::obstr_size_t llc_num_buckets = size;
  OB_ASSERT(size == get_llc_size());
  OB_ASSERT(ObExprEstimateNdv::llc_is_num_buckets_valid(llc_num_buckets));
  OB_ASSERT(llc_num_buckets > bucket_index);
  if (pmax > static_cast<uint8_t>(llc_bitmap_buf[bucket_index])) {
    // Theoretically, pmax will not exceed 65.
    llc_bitmap_buf[bucket_index] = static_cast<uint8_t>(pmax);
  }
  return ret;
}

int ObAggregateProcessor::llc_init(AggrCell &aggr_cell)
{
  int ret = OB_SUCCESS;
  char llc_bitmap_buf[sizeof(char ) * LLC_NUM_BUCKETS] = {};
  ObDatum src_datum;
  src_datum.set_string(llc_bitmap_buf, sizeof(char ) * LLC_NUM_BUCKETS);
  ret = clone_kernel_cell(aggr_cell, src_datum,
      IterKernelLayout(IterValueDomain::HllRegisters, 0, get_llc_size()));
  return ret;
}

int ObAggregateProcessor::llc_init_empty(char *&llc_map, int64_t &llc_map_size, common::ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  llc_map_size = get_llc_size();
  OB_ASSERT(llc_map_size > 0);
  if (OB_ISNULL(llc_map = static_cast<char *> (alloc.alloc(llc_map_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMSET(llc_map, 0, llc_map_size);
  }
  return ret;
}

int ObAggregateProcessor::llc_init_empty(ObExpr &expr, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  char *llc_bitmap_buf = NULL;
  const int64_t llc_bitmap_size = get_llc_size();
  if (OB_ISNULL(llc_bitmap_buf = expr.get_str_res_mem(eval_ctx_,
      llc_bitmap_size))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMSET(llc_bitmap_buf, 0, llc_bitmap_size);
    Datum &datum = expr.locate_datum_for_write(eval_ctx);
    expr.set_evaluated_projected(eval_ctx);
    datum.set_string(llc_bitmap_buf, llc_bitmap_size);
  }
  return ret;
}

int ObAggregateProcessor::llc_calc_hash_value(const ObChunkDatumStore::StoredRow &stored_row,
    const ObIArray<ObExpr *> &param_exprs, bool &has_null_cell, uint64_t &hash_value)
{
  int ret = OB_SUCCESS;
  has_null_cell = false;
  hash_value = 0;
  for (int64_t i = 0; !has_null_cell && i < stored_row.cnt_ && OB_SUCC(ret); ++i) {
    const ObExpr &expr = *param_exprs.at(i);
    const ObDatum datum = StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[i]);
    if (datum.is_null()) {
      has_null_cell = true;
    } else {
      OB_ASSERT(NULL != expr.basic_funcs_);
      common::ObDatumHashFuncType hash_func = storage_predicate_functions(expr)->murmur_hash_;
      if (OB_FAIL(hash_func(datum, hash_value, hash_value, datum_access_ctx_))) {
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::check_rows_equal(const ObChunkDatumStore::LastStoredRow &prev_row,
                                           const ObChunkDatumStore::StoredRow &cur_row,
                                           const ObAggrInfo &aggr_info,
                                           bool &is_equal)
{
  int ret = OB_SUCCESS;
  is_equal = false;
  if (aggr_info.sort_collations_.empty()) {
    // %sort_columns_ is empty if order by const value, set is_equal to true directly.
    // pre_sort_columns_.store_row_ is NULL here.
    is_equal = true;
  } else if (OB_ISNULL(prev_row.store_row_) ||
             OB_UNLIKELY(prev_row.store_row_->cnt_ != cur_row.cnt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_equal = true;
    for (int64_t i = 0; OB_SUCC(ret) && is_equal && i < aggr_info.sort_collations_.count(); ++i) {
      uint32_t index = aggr_info.sort_collations_.at(i).field_idx_;
      if (OB_UNLIKELY(index >= prev_row.store_row_->cnt_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int cmp_ret = 0;
        if (OB_FAIL(aggr_info.sort_cmp_funcs_.at(i).cmp_func_(StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[index]),
                                                              StoredDatumCodec::decode_legacy_view(cur_row.stored_cells()[index]),
                                                              cmp_ret,
                                                              datum_access_ctx_))) {
        } else {
          is_equal = 0 == cmp_ret;
        }
      }
    }
  }
  return ret;
}

int ObAggregateCalcFunc::add_calc(const ObDatum &left_value, const ObDatum &right_value,
    ObDatum &result_datum, const SqlTypeClass type, const ObPrecision precision,
    ObIAllocator &out_allocator)
{
  int ret = OB_SUCCESS;
  if (left_value.is_null() && right_value.is_null()) {
    result_datum.set_null();
  } else {
    switch (type) {
      case ObIntTC: {
        if (left_value.is_null()) {
          result_datum.set_int(right_value.get_int());
        } else if (right_value.is_null()) {
          result_datum.set_int(left_value.get_int());
        } else {
          int64_t left_int = left_value.get_int();
          int64_t right_int = right_value.get_int();
          int64_t sum_int = left_int + right_int;
          if (ObExprAdd::is_int_int_out_of_range(left_int, right_int, sum_int)) {
            char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
            ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
            ObNumber result_nmb;
            if (!result_datum.is_null()) {
              ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                      result_datum.get_number());
              result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
            }
            if (OB_FAIL(result_nmb.add(left_int, right_int, result_nmb, allocator))) {
            } else if (OB_FAIL(clone_number_cell(result_nmb, result_datum, out_allocator))) {
            }
          } else {
            result_datum.set_int(sum_int);
          }
        }
        break;
      }
      case ObUIntTC: {
        if (left_value.is_null()) {
          result_datum.set_uint(right_value.get_uint());
        } else if (right_value.is_null()) {
          result_datum.set_uint(left_value.get_uint());
        } else {
          uint64_t left_uint = left_value.get_uint();
          uint64_t right_uint = right_value.get_uint();
          uint64_t sum_uint = left_uint + right_uint;
          if (ObExprAdd::is_uint_uint_out_of_range(left_uint, right_uint, sum_uint)) {
            char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
            ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
            ObNumber result_nmb;
            if (!result_datum.is_null()) {
              ObCompactNumber &cnum = const_cast<ObCompactNumber &>(
                                              result_datum.get_number());
              result_nmb.assign(cnum.desc_.desc_, cnum.digits_ + 0);
            }
            if (OB_FAIL(result_nmb.add(left_uint, right_uint, result_nmb, out_allocator))) {
            } else if (OB_FAIL(clone_number_cell(result_nmb, result_datum, out_allocator))) {
            }
          } else {
            result_datum.set_uint(sum_uint);
          }
        }
        break;
      }
      case ObFloatTC: {
        if (left_value.is_null()) {
          result_datum.set_float(right_value.get_float());
        } else if (right_value.is_null()) {
          result_datum.set_float(left_value.get_float());
        } else {
          float left_f = left_value.get_float();
          float right_f = right_value.get_float();
          if (OB_UNLIKELY(ObArithExprOperator::is_float_out_of_range(left_f + right_f))) {
            ret = OB_OPERATE_OVERFLOW;
            char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
            int64_t pos = 0;
            databuff_printf(expr_str,
                            OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                            pos,
                            "'(%e + %e)'", left_f, right_f);
            LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "BINARY_FLOAT", expr_str);
            LOG_WARN("float out of range", K(left_f), K(right_f));
          } else {
            result_datum.set_float(left_f + right_f);
          }
        }
        break;
      }
      case ObDoubleTC: {
        if (left_value.is_null()) {
          result_datum.set_double(right_value.get_double());
        } else if (right_value.is_null()) {
          result_datum.set_double(left_value.get_double());
        } else {
          double left_d = left_value.get_double();
          double right_d = right_value.get_double();
          if (OB_UNLIKELY(ObArithExprOperator::is_double_out_of_range(left_d + right_d))) {
            ret = OB_OPERATE_OVERFLOW;
            char expr_str[OB_MAX_TWO_OPERATOR_EXPR_LENGTH];
            int64_t pos = 0;
            databuff_printf(expr_str,
                            OB_MAX_TWO_OPERATOR_EXPR_LENGTH,
                            pos,
                            "'(%le + %le)'", left_d, right_d);
            LOG_USER_ERROR(OB_OPERATE_OVERFLOW, "DOUBLE", expr_str);
          } else {
            result_datum.set_double(left_d + right_d);
          }
        }
        break;
      }
      case ObNumberTC: {
        if (left_value.is_null()) {
          if (OB_FAIL(clone_number_cell(right_value.get_number(),
              result_datum, out_allocator))) {
          }
        } else if (right_value.is_null()) {
          if (OB_FAIL(clone_number_cell(left_value.get_number(),
              result_datum, out_allocator))) {
          }
        } else {
          char buf_alloc[ObNumber::MAX_CALC_BYTE_LEN];
          ObDataBuffer allocator(buf_alloc, ObNumber::MAX_CALC_BYTE_LEN);
          const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
          ObNumber left_nmb(left_value.get_number());
          ObNumber right_nmb(right_value.get_number());
          ObNumber result_nmb;
          if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, allocator, strict_mode))) {
          } else {
            ret = clone_number_cell(result_nmb, result_datum, out_allocator);
          }
        }
        break;
      }
      case ObDecimalIntTC: {
        ret = add_decimalint_with_same_precision(left_value, right_value, precision, result_datum);
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
  }
  return ret;
}

int ObAggregateCalcFunc::clone_number_cell(const ObNumber &src_number,
    ObDatum &target_cell, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  int64_t need_size = ObNumber::MAX_BYTE_LEN;
  char *buff_ptr = NULL;
  if (OB_ISNULL(buff_ptr = static_cast<char*>(allocator.alloc(need_size)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
  }

  if (OB_SUCC(ret)) {
    target_cell.ptr_ = buff_ptr;
    target_cell.set_number(src_number);
  }
  return ret;
}

int ObAggregateCalcFunc::add_decimalint_with_same_precision(const ObDatum &left_value,
                                                            const ObDatum &right_value,
                                                            const ObPrecision precision,
                                                            ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  if (left_value.is_null()) {
    result_datum.set_decimal_int(right_value.get_decimal_int(), right_value.len_);
  } else if (right_value.is_null()) {
    result_datum.set_decimal_int(left_value.get_decimal_int(), left_value.len_);
  } else if (precision <= MAX_PRECISION_DECIMAL_INT_32) {
    const int32_t result = left_value.get_decimal_int32() + right_value.get_decimal_int32();
    result_datum.set_decimal_int(result);
  } else if (precision <= MAX_PRECISION_DECIMAL_INT_64) {
    const int64_t result = left_value.get_decimal_int64() + right_value.get_decimal_int64();
    result_datum.set_decimal_int(result);
  } else if (precision <= MAX_PRECISION_DECIMAL_INT_128) {
    const int128_t result = left_value.get_decimal_int128() + right_value.get_decimal_int128();
    result_datum.set_decimal_int(result);
  } else if (precision <= MAX_PRECISION_DECIMAL_INT_256) {
    const int256_t result = left_value.get_decimal_int256() + right_value.get_decimal_int256();
    result_datum.set_decimal_int(result);
  } else {
    const int512_t result = left_value.get_decimal_int512() + right_value.get_decimal_int512();
    result_datum.set_decimal_int(result);
  }
  return ret;
}

// only for window function aggr cell
int ObAggregateProcessor::clone_cell_for_wf(Datum &target_cell, const Datum &src_cell,
    const DatumCopyPlan &plan, char *&buffer, int64_t &capacity, const bool is_number/*false*/)
{
  return copy_final_result(src_cell, plan, aggr_alloc_, target_cell, buffer, capacity,
      is_number ? number::ObNumber::MAX_BYTE_LEN : 0);
}

int ObAggregateProcessor::get_wm_concat_result(const ObAggrInfo &aggr_info,
                                               GroupConcatExtraResult *&extra,
                                               ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GET_MY_SESSION(eval_ctx_.exec_ctx_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else {
    ObArenaAllocator tmp_alloc(aggr_alloc_.get_label(), common::OB_MALLOC_NORMAL_BLOCK_SIZE,
                               ObCtxIds::WORK_AREA);
    // Default is comma
    ObString sep_str = ObCharsetUtils::get_const_str(aggr_info.expr_->datum_meta_.cs_type_, ',');
    const ObChunkDatumStore::StoredRow *storted_row = NULL;
    int64_t str_len = 0;
    char *buf = NULL;
    int64_t buf_len = 0;
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      if (OB_ISNULL(storted_row)) {
        ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret)) {
        if (storted_row->stored_cells()[0].is_null()) {
        } else {
          const ObDatum datum = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[0]);
          const ObDatumMeta &datum_meta = aggr_info.expr_->args_[0]->datum_meta_;
          const bool has_lob_header = aggr_info.expr_->args_[0]->obj_meta_.has_lob_header();
          ObString cell_string;
          ObTextStringIter text_iter(datum_meta.type_, datum_meta.cs_type_, datum.get_string(), has_lob_header);
          if (OB_FAIL(ObTextStringHelper::build_text_iter(
                  text_iter, eval_ctx_.exec_ctx_, &tmp_alloc))) {
          } else if (OB_FAIL(text_iter.get_full_data(cell_string))) {
          } else if (cell_string.length() > 0) {
            int64_t append_len = cell_string.length() + sep_str.length() + str_len;
            if (OB_UNLIKELY(append_len > OB_MAX_PACKET_LENGTH)) {
              ret = OB_ERR_TOO_LONG_STRING_IN_CONCAT;
            } else if (buf_len < append_len) {
              char *tmp_buf = NULL;
              buf_len = append_len * 2;
              if (OB_ISNULL(tmp_buf = static_cast<char*>(tmp_alloc.alloc(buf_len)))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
              } else {
                // Note: buf may be nullptr, but str_len = 0 at that time, NO ISSUE
                MEMCPY(tmp_buf, buf, str_len);
                buf = tmp_buf;
              }
            }
            if (OB_SUCC(ret)) {
              MEMCPY(buf + str_len, cell_string.ptr(), cell_string.length());
              str_len += cell_string.length();
              MEMCPY(buf + str_len, sep_str.ptr(), sep_str.length());
              str_len += sep_str.length();
            }
          }
        }
      }
    }//end of while
    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      str_len = str_len == 0 ? str_len : str_len - sep_str.length();
      if (str_len > 0) {
        ObString res_str;
        res_str.assign(buf, str_len);
        Datum native_result;
        ObTextStringDatumResult text_result(ObLongTextType, aggr_info.expr_, &eval_ctx_, &native_result);
        if (OB_FAIL(text_result.init(str_len))) {
        } else if (OB_FAIL(text_result.append(buf, str_len))) {
        } else {
          text_result.set_result();
          LegacyAggrValue legacy;
          OZ(legacy.prepare(*aggr_info.expr_, native_result));
          OX(concat_result = legacy.value_);
        }
      } else {
        concat_result.set_null(); // all were skipped, then set to null
      }
    }
  }
  return ret;
}

static int get_param_int_val(ObExpr *expr, Datum *datum, int64_t &val)
{
  int ret = OB_SUCCESS;
  if (expr->obj_meta_.is_integer_type()) {
    val = datum->get_int();
  } else if (expr->obj_meta_.is_decimal_int()) {
    common::ObDatum coefficient;
    alignas(8) char scratch[64];
    if (OB_FAIL(expr->get_datum_copy_plan().get_decimal(*datum, coefficient, scratch, sizeof(scratch)))) {
      return ret;
    }
    ret = ObExprUtil::trunc_decint2int64(coefficient.get_decimal_int(), coefficient.get_int_bytes(),
                                         expr->datum_meta_.scale_, val);
  } else if (expr->obj_meta_.is_number()) {
    ret = ObExprUtil::trunc_num2int64(*datum, val);
  }
  return ret;
}
int ObAggregateProcessor::init_topk_fre_histogram_item(
  const ObAggrInfo &aggr_info,
  ObTopKFrequencyHistograms *topk_fre_hist)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_FUN_TOP_FRE_HIST != aggr_info.get_expr_type()) ||
      OB_ISNULL(aggr_info.window_size_param_expr_) ||
      OB_ISNULL(aggr_info.item_size_param_expr_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    Datum *window_size_result = NULL;
    Datum *item_size_result = NULL;
    Datum *max_disuse_cnt_result = NULL;
    int64_t window_size = 0;
    int64_t item_size = 0;
    int64_t max_disuse_cnt = 0;
    if (OB_UNLIKELY(!aggr_info.window_size_param_expr_->obj_meta_.is_numeric_type() ||
                    !aggr_info.item_size_param_expr_->obj_meta_.is_numeric_type() ||
                    (aggr_info.max_disuse_param_expr_ != NULL &&
                     !aggr_info.max_disuse_param_expr_->obj_meta_.is_numeric_type()))) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(aggr_info.window_size_param_expr_->eval(eval_ctx_, window_size_result)) ||
               OB_FAIL(aggr_info.item_size_param_expr_->eval(eval_ctx_, item_size_result)) ||
               (aggr_info.max_disuse_param_expr_ != NULL &&
                OB_FAIL(aggr_info.max_disuse_param_expr_->eval(eval_ctx_, max_disuse_cnt_result)))) {
    } else if (OB_ISNULL(window_size_result) ||
               OB_ISNULL(item_size_result)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(get_param_int_val(aggr_info.window_size_param_expr_, window_size_result,
                                         window_size))
               || OB_FAIL(get_param_int_val(aggr_info.item_size_param_expr_,
                                            item_size_result, item_size))
               || (aggr_info.max_disuse_param_expr_ != NULL
                   && OB_FAIL(get_param_int_val(aggr_info.max_disuse_param_expr_,
                                                max_disuse_cnt_result, max_disuse_cnt)))) {
      LOG_WARN("failed to get int param val", K(*window_size_result), K(window_size),
                                              K(*item_size_result), K(item_size),
                                              KPC(max_disuse_cnt_result), K(max_disuse_cnt), K(ret));;
    } else {
      topk_fre_hist->set_window_size(window_size);
      topk_fre_hist->set_item_size(item_size);
      topk_fre_hist->set_is_topk_hist_need_des_row(aggr_info.is_need_deserialize_row_);
      topk_fre_hist->set_max_disuse_cnt(max_disuse_cnt);
    }
  }
  return ret;
}

int ObAggregateProcessor::get_pl_agg_udf_result(const ObAggrInfo &aggr_info,
                                                GroupConcatExtraResult *&extra,
                                                ObDatum &result)
{
  int ret = OB_SUCCESS;
  ObPlAggUdfFunction pl_agg_udf_func;
  DatumParameter pl_agg_udf_obj;
  if (OB_ISNULL(extra) || OB_ISNULL(aggr_info.expr_) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else if (OB_FAIL(pl_agg_udf_func.init(eval_ctx_.exec_ctx_.get_my_session(),
                                          &(eval_ctx_.exec_ctx_.get_allocator()),
                                          &eval_ctx_.exec_ctx_,
                                          aggr_info.pl_agg_udf_type_id_,
                                          aggr_info.pl_agg_udf_params_type_,
                                          aggr_info.pl_result_type_,
                                          pl_agg_udf_obj))) {
  } else {
    const ObChunkDatumStore::StoredRow *stored_row = NULL;
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(stored_row))) {
      ObSEArray<DatumParameter, 4> datum_params;
      if (OB_ISNULL(stored_row)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_UNLIKELY(stored_row->cnt_ != aggr_info.param_exprs_.count())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < stored_row->cnt_; ++i) {
          const ObDatum datum = StoredDatumCodec::decode_legacy_view(stored_row->stored_cells()[i]);
          SqlTypeMeta meta = aggr_info.param_exprs_.at(i)->obj_meta_;
          DatumParameter param;
          if (datum.is_null()) {
            // Preserve legacy projection behavior: a SQL NULL carries NULL type while
            // retaining the declared collation attributes.
            meta.set_null();
            if (!aggr_info.param_exprs_.at(i)->obj_meta_.is_null()) {
              meta.set_collation(aggr_info.param_exprs_.at(i)->obj_meta_);
            }
          } else if (meta.is_decimal_int() && meta.get_scale() < 0) {
            ret = OB_ERR_UNEXPECTED;
          } else if (meta.is_ext() && datum.is_native_extend()) {
            meta.set_ext();
            meta.set_extend_type(datum.get_extend_type());
            meta.set_cs_level(datum.get_extend_collation_level());
            meta.set_cs_type(datum.get_extend_collation());
          } else if (meta.is_ext() && datum.has_ext_value()) {
            // Legacy Datum ext payloads carry their exact ObjMeta in the
            // compatibility descriptor, just as the legacy projection restored it.
            static_assert(sizeof(SqlTypeMeta) == 4, "legacy Ext metadata is four bytes");
            MEMCPY(&meta, datum.ptr_, sizeof(SqlTypeMeta));
          }
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(param.assign_legacy_borrowed(datum, meta))) {
          } else if (OB_FAIL(datum_params.push_back(param))) {
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(pl_agg_udf_func.process_calc_pl_agg_udf(
                pl_agg_udf_obj, datum_params.get_data(), stored_row->cnt_))) {
        }
      }
    }
    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      DatumParameter result_param;
      if (OB_FAIL(pl_agg_udf_func.process_get_pl_agg_udf_result(pl_agg_udf_obj, result_param))) {
      } else {
        if (!is_lob_storage(result_param.get_type())) {
          // This accumulator still returns an explicit old Datum. Its scalar
          // bytes must belong to the real aggregate owner, not a local slot.
          ret = result_param.copy_legacy(result, aggr_alloc_);
        } else {
          Datum native_origin, native_result;
          LegacyAggrValue legacy_result;
          alignas(8) char scratch[64];
          ObDatum physical;
          if (OB_FAIL(result_param.export_legacy(physical, scratch, sizeof(scratch)))) {
          } else if (OB_FAIL(aggr_info.expr_->get_datum_copy_plan().import_legacy(physical, native_origin))) {
          } else if (OB_FAIL(ob_adjust_lob_datum(eval_ctx_.exec_ctx_, native_origin,
                                    result_param.get_type_meta(), aggr_info.expr_->obj_meta_,
                                    eval_ctx_.exec_ctx_.get_allocator(), native_result))) {
          } else if (OB_FAIL(legacy_result.prepare(*aggr_info.expr_, native_result))) {
          } else {
            // A LOB is a reference span owned by the adjustment allocator.
            result = legacy_result.value_;
          }
        }
        if (OB_SUCC(ret)) {
          LOG_TRACE("succeed to get pl agg udf result", K(result_param), K(result));
        }
      }
      if (result_param.is_pl_extend()) {
        int tmp_ret = OB_SUCCESS;
        if (OB_ISNULL(eval_ctx_.exec_ctx_.get_pl_ctx())) {
          tmp_ret = eval_ctx_.exec_ctx_.init_pl_ctx();
        }
        if (OB_SUCCESS == tmp_ret && OB_NOT_NULL(eval_ctx_.exec_ctx_.get_pl_ctx())) {
          tmp_ret = eval_ctx_.exec_ctx_.get_pl_ctx()->add(result_param, eval_ctx_.exec_ctx_.get_allocator());
        }
        if (OB_SUCCESS != tmp_ret) {
        }
        ret = OB_SUCCESS == ret ? tmp_ret : ret;
      }
    }
  }
  int tmp_ret = OB_SUCCESS;
  if ((tmp_ret = pl::ObUserDefinedType::destruct_obj(pl_agg_udf_obj, eval_ctx_.exec_ctx_.get_my_session())) != OB_SUCCESS) {
  }
  return ret;
}

int ObAggregateProcessor::get_top_k_fre_hist_result(ObTopKFrequencyHistograms &top_k_fre_hist,
                                                    const SqlTypeMeta &obj_meta,
                                                    bool has_lob_header,
                                                    ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  if (top_k_fre_hist.has_bucket() ||
      (top_k_fre_hist.is_by_pass() && !top_k_fre_hist.is_need_merge_topk_hist())) {
    //sort and reserve topk item
    if (OB_FAIL(top_k_fre_hist.create_topk_fre_items(&obj_meta))) {
    } else {
      char *buf = NULL;
      int64_t buf_size = top_k_fre_hist.get_serialize_size();
      int64_t buf_pos = 0;
      ObTextStringResult new_tmp_lob(ObLongTextType, has_lob_header, &aggr_alloc_);
      if (OB_FAIL(new_tmp_lob.init(buf_size))) {
      } else if (OB_FAIL(new_tmp_lob.get_reserved_buffer(buf, buf_size))) {
      } else if (OB_FAIL(top_k_fre_hist.serialize(buf, buf_size, buf_pos))) {
      } else if (OB_FAIL(new_tmp_lob.lseek(buf_pos, 0))) {
      } else {
        ObString lob_loc_str;
        new_tmp_lob.get_result_buffer(lob_loc_str);
        result_datum.set_string(lob_loc_str);
      }
    }
  } else {
    result_datum.set_null();
  }
  return ret;
}

int ObAggregateProcessor::convert_datum_to_values(const ObAggrInfo &aggr_info,
                                                  const ObChunkDatumStore::StoredRow &stored_row,
                                                  DatumValue *values,
                                                  int64_t value_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(value_cnt != stored_row.cnt_ || value_cnt != aggr_info.param_exprs_.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < stored_row.cnt_; ++i) {
      new (&values[i]) DatumValue();
      ret = values[i].assign_legacy_borrowed(
          StoredDatumCodec::decode_legacy_view(stored_row.stored_cells()[i]),
          aggr_info.param_exprs_.at(i)->obj_meta_);
    }
  }
  return ret;
}

int ObAggregateProcessor::compute_hybrid_hist_result(const ObAggrInfo &aggr_info,
                                                     HybridHistExtraResult *&extra,
                                                     ObDatum &result)
{
  int ret = OB_SUCCESS;
  ObHybridHistograms hybrid_hist;
  Datum *bucket_num_result = NULL;
  int64_t bucket_num = 0;
  int64_t num_distinct = 0;
  int64_t null_count = 0;
  int64_t total_count = 0;
  int64_t pop_count = 0;
  int64_t pop_freq = 0;
  if (OB_ISNULL(extra) || OB_ISNULL(aggr_info.bucket_num_param_expr_) ||
     OB_UNLIKELY(extra->get_sort_row_count() == 0 ||
                 aggr_info.param_exprs_.count() != 1 ||
                 aggr_info.sort_collations_.count() != 1 ||
                 !aggr_info.bucket_num_param_expr_->obj_meta_.is_numeric_type())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(aggr_info.bucket_num_param_expr_->eval(eval_ctx_, bucket_num_result))) {
  } else if (OB_ISNULL(bucket_num_result)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObExprUtil::get_int_param_val(
               bucket_num_result, aggr_info.bucket_num_param_expr_,
               bucket_num))) {
  } else if (bucket_num <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(extra->finish_add_sort_row())) {
  } else {
    ObChunkDatumStore::LastStoredRow prev_row(aggr_alloc_);
    const int64_t extra_size = sizeof(BucketDesc);
    int64_t repeat_count = 0;
    const ObChunkDatumStore::StoredRow *stored_row = NULL;
    const ObChunkDatumStore::StoredRow *mat_stored_row = NULL;
    const SqlTypeMeta &obj_meta = aggr_info.param_exprs_.at(0)->obj_meta_;
    // get null count 
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row_from_sort(stored_row))) {
      if (OB_ISNULL(stored_row) || OB_UNLIKELY(stored_row->cnt_ != 1)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (stored_row->stored_cells()[0].is_null()) {
        ++ null_count;
      } else if (OB_FAIL(shadow_truncate_stored_string_for_hist(obj_meta, *stored_row, 0))) {
      } else if (OB_FAIL(prev_row.save_store_row(*stored_row))) {
      } else {
        repeat_count = 1;
        ++ num_distinct;
        break;
      }
    }
    total_count = extra->get_sort_row_count() - null_count;
    int64_t pop_threshold = total_count / bucket_num;
    // get all bucket node and store them into chunk datum store
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row_from_sort(stored_row))) {
      bool is_equal = false;
      if (OB_ISNULL(stored_row) || OB_UNLIKELY(stored_row->cnt_ != 1)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(shadow_truncate_stored_string_for_hist(obj_meta, *stored_row, 0))) {
      } else if (!obj_meta.is_lob_storage() &&
                 OB_FAIL(check_rows_equal(prev_row, *stored_row, aggr_info, is_equal))) {
      } else if (obj_meta.is_lob_storage() &&
                 OB_FAIL(check_rows_prefix_str_equal_for_hybrid_hist(prev_row, *stored_row, aggr_info, obj_meta, is_equal))) {
      } else if (is_equal) {
        ++ repeat_count;
      } else if (OB_ISNULL(prev_row.store_row_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(extra->add_material_row(*prev_row.store_row_,
                                                 extra_size, mat_stored_row))) {
      } else if (OB_FAIL(prev_row.save_store_row(*stored_row))) {
      } else if (OB_ISNULL(mat_stored_row)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        BucketDesc *desc = reinterpret_cast<BucketDesc*>(mat_stored_row->get_extra_payload());
        desc->ep_count_ = repeat_count;
        desc->is_pop_ = repeat_count > pop_threshold;
        if (desc->is_pop_) {
          pop_freq += repeat_count;
          ++ pop_count;
        }
        repeat_count = 1;
        ++ num_distinct;
      }
    }
    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      bool has_lob_header = aggr_info.expr_->obj_meta_.has_lob_header();
      if (prev_row.store_row_ != nullptr) {
        if (OB_ISNULL(prev_row.store_row_)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(extra->add_material_row(*prev_row.store_row_,
                                                   extra_size, mat_stored_row))) {
        } else if (OB_ISNULL(mat_stored_row)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          BucketDesc *desc = reinterpret_cast<BucketDesc*>(mat_stored_row->get_extra_payload());
          desc->ep_count_ = repeat_count;
          desc->is_pop_ = repeat_count > pop_threshold;
          if (desc->is_pop_) {
            pop_freq += repeat_count;
            ++ pop_count;
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(extra->finish_add_material_row())) {
      } else if (OB_FAIL(hybrid_hist.build_hybrid_hist(extra, &aggr_alloc_, bucket_num, total_count,
                                                       num_distinct, pop_count, pop_freq,
                                                       aggr_info.param_exprs_.at(0)->obj_meta_,
                                                       datum_access_ctx_))) {
      } else if (OB_FAIL(get_hybrid_hist_result(&hybrid_hist, has_lob_header, result))) {
      } else {/*do nothing*/}
    }
  }
  return ret;
}

int ObAggregateProcessor::shadow_truncate_stored_string_for_hist(const SqlTypeMeta obj_meta,
    const ObChunkDatumStore::StoredRow &row, const int64_t column)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(column < 0 || column >= row.cnt_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObDatum datum = StoredDatumCodec::decode_legacy_view(row.stored_cells()[column]);
    if (OB_FAIL(shadow_truncate_string_for_hist(obj_meta, datum))) {
    } else {
      // The old path deliberately shortened the stored descriptor. Subsequent
      // equality, LastStoredRow copies and materialized endpoints must retain
      // that change; the row's payload/allocated size are not rewritten.
      const_cast<StoredDatumCell &>(row.stored_cells()[column]) =
          StoredDatumCodec::encode_legacy_view(datum);
    }
  }
  return ret;
}

int ObAggregateProcessor::shadow_truncate_string_for_hist(const SqlTypeMeta obj_meta,
                                                          ObDatum &datum,
                                                          int32_t *origin_str_len/*default null*/)
{
  int ret = OB_SUCCESS;
  if (ObColumnStatParam::is_valid_opt_col_type(obj_meta.get_type()) && obj_meta.is_string_type() && !datum.is_null()) {
    if (!obj_meta.is_lob_storage()) {
      const ObString &str = datum.get_string();
      int64_t truncated_str_len = ObDbmsStatsUtils::get_truncated_str_len(str, obj_meta.get_collation_type());
      if (OB_UNLIKELY(truncated_str_len < 0)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        datum.set_string(str.ptr(), static_cast<uint32_t>(truncated_str_len));
        if (origin_str_len != NULL && static_cast<int32_t>(truncated_str_len) < str.length()) {
          *origin_str_len = str.length();
        }
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::get_hybrid_hist_result(ObHybridHistograms *hybrid_hist,
                                                 bool has_lob_header,
                                                 ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(hybrid_hist)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (hybrid_hist->get_buckets().count() > 0) {
    char *buf = NULL;
    int64_t buf_size = hybrid_hist->get_serialize_size();
    int64_t buf_pos = 0;
    ObTextStringResult new_tmp_lob(ObLongTextType, has_lob_header, &aggr_alloc_);
    if (OB_FAIL(new_tmp_lob.init(buf_size))) {
    } else if (OB_FAIL(new_tmp_lob.get_reserved_buffer(buf, buf_size))) {
    } else if (OB_FAIL(hybrid_hist->serialize(buf, buf_size, buf_pos))) {
    } else if (OB_FAIL(new_tmp_lob.lseek(buf_pos, 0))) {
    } else {
      ObString lob_loc_str;
      new_tmp_lob.get_result_buffer(lob_loc_str);
      result_datum.set_string(lob_loc_str);
    }
  } else {
    result_datum.set_null();
  }
  return ret;
}

int ObAggregateProcessor::get_json_arrayagg_result(const ObAggrInfo &aggr_info,
                                                   GroupConcatExtraResult *&extra,
                                                   ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_back(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_arr(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStringBuffer value(&res_alloc_arr);
  const common::ObLobReadOptions *lob_options = nullptr;
  if (OB_FAIL(eval_ctx_.exec_ctx_.get_lob_read_options(lob_options))) {
  } else if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else {
    const ObChunkDatumStore::StoredRow *storted_row = NULL;
    bool is_bool = false;
    if (OB_FAIL(extra->get_bool_mark(0, is_bool))) {
    }
    
    // get type
    ObJsonBinAggSerializer bin_agg(&res_alloc, static_cast<uint8_t>(ObJsonNodeType::J_ARRAY), &res_alloc_back, &res_alloc_arr);
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      SqlTypeMeta value_meta;
      if (OB_ISNULL(storted_row)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        if (OB_UNLIKELY(storted_row->cnt_ != aggr_info.param_exprs_.count())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          // Keep the old row conversion's error checks for every cell while
          // retaining only the metadata actually needed by JSON aggregation.
          for (int64_t i = 0; OB_SUCC(ret) && i < storted_row->cnt_; ++i) {
            const ObDatum cell = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[i]);
            const SqlTypeMeta &meta = aggr_info.param_exprs_.at(i)->obj_meta_;
            if (!cell.is_null() && OB_UNLIKELY(!meta.is_valid()
                || (meta.is_decimal_int() && meta.get_scale() < 0))) {
              ret = OB_ERR_UNEXPECTED;
            } else if (i == 0) {
              value_meta = meta;
              if (cell.is_null()) {
                value_meta.set_null();
                if (!meta.is_null()) { value_meta.set_collation(meta); }
              } else if (meta.is_null()) {
                value_meta.set_null();
              } else if (meta.is_ext()) {
                // The legacy projection derives Ext metadata from the stored payload.
                if (cell.is_native_extend()) {
                  value_meta.set_ext();
                  value_meta.set_extend_type(cell.get_extend_type());
                  value_meta.set_cs_level(cell.get_extend_collation_level());
                  value_meta.set_cs_type(cell.get_extend_collation());
                } else {
                  static_assert(sizeof(SqlTypeMeta) == 4, "legacy Ext metadata is four bytes");
                  MEMCPY(&value_meta, cell.ptr_, 4);
                }
              }
            }
          }
        }
        if (OB_FAIL(ret)) {
        } else if (storted_row->cnt_ < 1) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          SqlValueType val_type = value_meta.get_type();
          ObCollationType cs_type = value_meta.get_collation_type();
          ObScale scale = value_meta.get_scale();
          scale = (val_type == ObBitType) ? aggr_info.param_exprs_.at(0)->datum_meta_.length_semantics_ : scale;
          ObIJsonBase *json_val = NULL;
          ObDatum converted_datum;
          converted_datum.set_datum(StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[0]));
          bool has_lob_header = value_meta.has_lob_header();
          // convert string charset if needed
          if (ob_is_string_type(val_type) 
              && (ObCharset::charset_type_by_coll(cs_type) != CHARSET_UTF8MB4)) {
            ObString origin_str = converted_datum.get_string();
            ObString converted_str;
            if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(
              eval_ctx_.exec_ctx_, &tmp_alloc,
              val_type, cs_type, value_meta.has_lob_header(), origin_str))) {
            } else if (OB_FAIL(ObExprUtil::convert_string_collation(origin_str, cs_type, converted_str, 
                                                             CS_TYPE_UTF8MB4_BIN, tmp_alloc))) {
            } else {
              has_lob_header = false;
              converted_datum.set_string(converted_str);
              cs_type = CS_TYPE_UTF8MB4_BIN;
            }
          }

          // get json value
          if (OB_FAIL(ret)) {
          } else if (is_bool) {
            void *json_node_buf = tmp_alloc.alloc(sizeof(ObJsonBoolean));
            if (OB_ISNULL(json_node_buf)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else {
              ObJsonBoolean *bool_node = (ObJsonBoolean*)new(json_node_buf)ObJsonBoolean(converted_datum.get_bool());
              json_val = bool_node;
              if (OB_FAIL(ObJsonBaseFactory::transform(&tmp_alloc, json_val, ObJsonInType::JSON_BIN, json_val))) {
              }
            }
          } else if (ObJsonExprHelper::is_convertible_to_json(val_type)) {
            if (OB_FAIL(ObJsonExprHelper::transform_convertible_2jsonBase(converted_datum, val_type,
                                                                          &tmp_alloc, cs_type,
                                                                          json_val, ObConv2JsonParam(true,
                                                                          has_lob_header,
                                                                          true),
                                                                          lob_options))) {
            }
          } else {
            if (OB_FAIL(ObJsonExprHelper::transform_scalar_2jsonBase(converted_datum, val_type,
                                                                    &tmp_alloc, scale,
                                                                    eval_ctx_.exec_ctx_.get_my_session()->get_timezone_info(),
                                                                    eval_ctx_.exec_ctx_.get_my_session(),
                                                                    json_val, true))) {
            }
          }

          ObString key;
          ObJsonBin *jb_node = nullptr;
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(jb_node = static_cast<ObJsonBin*>(json_val))) {
            ret = OB_ERR_UNDEFINED;
          } else if (OB_FAIL(bin_agg.append_key_and_value(key, value, jb_node))) {
          } else if (bin_agg.get_approximate_length() > OB_MAX_PACKET_LENGTH * 2) {
            ret = OB_ERR_TOO_LONG_STRING_IN_CONCAT;
          } else {
            tmp_alloc.reset();
          }
        }
      }
    }//end of while
    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      if (OB_FAIL(bin_agg.serialize())) {
      } else {
        ObStringBuffer *buff = bin_agg.get_buffer();
        Datum native_result;
        ObTextStringDatumResult text_result(ObJsonType, aggr_info.expr_->obj_meta_.has_lob_header(), &native_result);
        if (OB_FAIL(text_result.init(buff->length(), &aggr_alloc_))) {
        } else if (OB_FAIL(text_result.append(buff->ptr(), buff->length()))) {
        } else {
          text_result.set_result();
          LegacyAggrValue legacy;
          OZ(legacy.prepare(*aggr_info.expr_, native_result));
          OX(concat_result = legacy.value_);
        }
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::get_ora_json_arrayagg_result(const ObAggrInfo &aggr_info,
                                                       GroupConcatExtraResult *&extra,
                                                       ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_back(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_arr(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStringBuffer value(&res_alloc_arr);
  const common::ObLobReadOptions *lob_options = nullptr;
  if (OB_FAIL(eval_ctx_.exec_ctx_.get_lob_read_options(lob_options))) {
  } else if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else if (aggr_info.param_exprs_.count() < 1) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObChunkDatumStore::StoredRow *storted_row = nullptr;
    SqlTypeMeta data_meta = aggr_info.param_exprs_.at(0)->obj_meta_;
    bool is_format_json = aggr_info.format_json_;
    bool is_absent_on_null = !aggr_info.absent_on_null_;
    bool is_strict = aggr_info.strict_json_;
    ObJsonBinAggSerializer bin_agg(&res_alloc, static_cast<uint8_t>(ObJsonNodeType::J_ARRAY), &res_alloc_back, &res_alloc_arr);

    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      if (OB_ISNULL(storted_row) || storted_row->cnt_ < 1) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const ObDatum datum = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[0]);
        SqlValueType val_type = data_meta.get_type();
        ObScale scale = data_meta.get_scale();
        ObCollationType cs_type = data_meta.get_collation_type();
        ObIJsonBase *json_val = nullptr;
        ObJsonBin *jb_node = nullptr;
        ObString key;

        if (OB_FAIL(ObJsonExprHelper::datum_to_json_val(&datum, data_meta, &tmp_alloc,
            eval_ctx_.exec_ctx_.get_my_session(), json_val, false, is_format_json, is_strict, true,
            lob_options))) {
        } else if (is_absent_on_null 
                   && (val_type == ObNullType || json_val->json_type() == ObJsonNodeType::J_NULL)) {
          // do nothing , continue
        } else if (OB_ISNULL(jb_node = static_cast<ObJsonBin*>(json_val))) {
          ret = OB_ERR_UNDEFINED;
        } else if (OB_FAIL(bin_agg.append_key_and_value(key, value, jb_node))) {
        } else if (bin_agg.get_approximate_length() > OB_MAX_PACKET_LENGTH * 2) {
          ret = OB_ERR_TOO_LONG_STRING_IN_CONCAT;
        } else {
          tmp_alloc.reset();
        }
      }
    } //end of while

    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      ParseNode parse_node;
      parse_node.value_ = aggr_info.returning_type_;

      SqlValueType rsp_type = static_cast<SqlValueType>(parse_node.int16_values_[OB_NODE_CAST_TYPE_IDX]);
      ObCollationType rsp_cs_type = static_cast<ObCollationType>(parse_node.int16_values_[OB_NODE_CAST_COLL_IDX]);
      int32_t rsp_len = parse_node.int32_values_[OB_NODE_CAST_C_LEN_IDX];

      if (!parse_node.value_) {
        rsp_type = ObJsonType;
        rsp_cs_type = CS_TYPE_UTF8MB4_BIN;
        rsp_len = (ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length();
      }

      ObJsonBuffer string_buffer(&tmp_alloc);
      if (OB_FAIL(bin_agg.serialize()))  {
      } else if (ob_is_string_type(rsp_type)) {
        ObIJsonBase *j_base = NULL;
        ObStringBuffer *buff = bin_agg.get_buffer();
        if (OB_FAIL(ObJsonBaseFactory::get_json_base(&tmp_alloc, 
                                                      buff->string(), 
                                                      ObJsonInType::JSON_BIN, 
                                                      ObJsonInType::JSON_BIN, 
                                                      j_base, 0,
                                                      ObJsonExprHelper::get_json_max_depth_config()))) {
        } else if (OB_FAIL(j_base->print(string_buffer, true, buff->length(), false))) {
        } else if (rsp_type == ObVarcharType && string_buffer.length() > rsp_len) {
          char res_ptr[OB_MAX_DECIMAL_PRECISION] = {0};
          if (OB_ISNULL(ObCharset::lltostr(rsp_len, res_ptr, 10, 1))) {
          } 
          ret = OB_OPERATE_OVERFLOW;
          LOG_USER_ERROR(OB_OPERATE_OVERFLOW, res_ptr, "json_arrayagg");
        } else if (OB_FAIL(pack_legacy_aggr_json(*aggr_info.expr_, eval_ctx_, concat_result,
                                                               string_buffer.string(), &aggr_alloc_))) {
        }
      } else if (ob_is_json(rsp_type)) {
        ObStringBuffer *buff = bin_agg.get_buffer();
        if (OB_FAIL(pack_legacy_aggr_json(*aggr_info.expr_, eval_ctx_, concat_result, buff->string(), &aggr_alloc_))) {
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::get_json_objectagg_result(const ObAggrInfo &aggr_info,
                                                    GroupConcatExtraResult *&extra,
                                                    ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  const int col_num = 2;
  common::ObArenaAllocator tmp_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_back(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_arr(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStringBuffer value(&res_alloc_arr);
  const common::ObLobReadOptions *lob_options = nullptr;
  if (OB_FAIL(eval_ctx_.exec_ctx_.get_lob_read_options(lob_options))) {
  } else if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else {
    const ObChunkDatumStore::StoredRow *storted_row = NULL;
    DatumValue key_value;
    bool is_bool = false;
    if (OB_FAIL(extra->get_bool_mark(1, is_bool))) {
    }
    ObJsonBinAggSerializer bin_agg(&res_alloc, static_cast<uint8_t>(ObJsonNodeType::J_OBJECT), &res_alloc_back, &res_alloc_arr);
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      if (OB_ISNULL(storted_row)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (storted_row->cnt_ != col_num) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        // Preserve the declared key metadata alongside its stored Datum.
        SqlTypeMeta value_meta;
        if (OB_UNLIKELY(storted_row->cnt_ != aggr_info.param_exprs_.count())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          ret = key_value.assign_legacy_borrowed(
              StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[0]),
              aggr_info.param_exprs_.at(0)->obj_meta_);
          const ObDatum value_datum = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[1]);
          const SqlTypeMeta &meta = aggr_info.param_exprs_.at(1)->obj_meta_;
          if (OB_FAIL(ret)) {
          } else if (!value_datum.is_null() && OB_UNLIKELY(!meta.is_valid()
              || (meta.is_decimal_int() && meta.get_scale() < 0))) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            value_meta = meta;
            if (value_datum.is_null()) {
              value_meta.set_null();
              if (!meta.is_null()) { value_meta.set_collation(meta); }
            } else if (meta.is_null()) {
              value_meta.set_null();
            } else if (meta.is_ext()) {
              if (value_datum.is_native_extend()) {
                value_meta.set_ext();
                value_meta.set_extend_type(value_datum.get_extend_type());
                value_meta.set_cs_level(value_datum.get_extend_collation_level());
                value_meta.set_cs_type(value_datum.get_extend_collation());
              } else {
                static_assert(sizeof(SqlTypeMeta) == 4, "legacy Ext metadata is four bytes");
                MEMCPY(&value_meta, value_datum.ptr_, 4);
              }
            }
          }
        }
        if (OB_FAIL(ret)) {
        } else if (key_value.is_null()) {
          ret = OB_ERR_JSON_DOCUMENT_NULL_KEY;
        } else if (ob_is_string_type(key_value.get_type())
                   && key_value.get_collation_type() == CS_TYPE_BINARY) {
          // not support binary charset as mysql 
          LOG_WARN("unsuport json string type with binary charset", 
              K(key_value.get_type()), K(key_value.get_collation_type()));
          ret = OB_ERR_INVALID_JSON_CHARSET;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_CHARSET);
        } else if (NULL == key_value.get_string().ptr()) {
          ret = OB_ERR_NULL_VALUE;
        } else {
          SqlValueType val_type0 = key_value.get_type();
          ObCollationType cs_type0 = key_value.get_collation_type();
          SqlValueType val_type1 = value_meta.get_type();
          ObScale scale1 = value_meta.get_scale();
          bool has_lob_header1 = value_meta.has_lob_header();
          scale1 = (val_type1 == ObBitType) ? aggr_info.param_exprs_.at(1)->datum_meta_.length_semantics_ : scale1;
          ObCollationType cs_type1 = value_meta.get_collation_type();
          ObString key_string = key_value.get_string();
          if (OB_FAIL(ObTextStringHelper::read_real_string_data(
                  eval_ctx_.exec_ctx_, &tmp_alloc, val_type0, cs_type0,
                  key_value.has_lob_header(), key_string))) {
          } else if (ObCharset::charset_type_by_coll(cs_type0) != CHARSET_UTF8MB4) {
            ObString converted_key_str;
            if (OB_FAIL(ObExprUtil::convert_string_collation(key_string, cs_type0, converted_key_str, 
                                                                  CS_TYPE_UTF8MB4_BIN, tmp_alloc))) {
            } else {
              key_string = converted_key_str;
            }
          }

          // get key and value, and append to json_object
          ObString key_data;
          ObIJsonBase *json_val = NULL;
          if (OB_FAIL(ret)) {
          } else if (OB_FAIL(deep_copy_ob_string(tmp_alloc, key_string, key_data))) {
          } else {
            ObDatum converted_datum;
            converted_datum.set_datum(StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[1]));
            if (ob_is_string_type(val_type1) 
            && (ObCharset::charset_type_by_coll(cs_type1) != CHARSET_UTF8MB4)) {
              ObString origin_str = converted_datum.get_string();
              ObString converted_str;
              if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(
                                                                          eval_ctx_.exec_ctx_,
                                                                          &tmp_alloc,
                                                                          val_type1, 
                                                                          cs_type1,
                                                                          key_value.has_lob_header(),
                                                                          origin_str))) {
              } else if (OB_FAIL(ObExprUtil::convert_string_collation(origin_str, cs_type1, converted_str, 
                                                              CS_TYPE_UTF8MB4_BIN, tmp_alloc))) {
              } else {
                converted_datum.set_string(converted_str);
                cs_type1 = CS_TYPE_UTF8MB4_BIN;
                has_lob_header1 = false;
              }
            }

            if (OB_FAIL(ret)) {
            } else if (is_bool) {
              void *json_node_buf = tmp_alloc.alloc(sizeof(ObJsonBoolean));
              if (OB_ISNULL(json_node_buf)) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
              } else {
                ObJsonBoolean *bool_node = (ObJsonBoolean*)new(json_node_buf)ObJsonBoolean(StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[1]).get_bool());
                json_val = bool_node;
                if (OB_FAIL(ObJsonBaseFactory::transform(&tmp_alloc, json_val, ObJsonInType::JSON_BIN, json_val))) {
                }
              }
            } else if (ObJsonExprHelper::is_convertible_to_json(val_type1)) {
              if (OB_FAIL(ObJsonExprHelper::transform_convertible_2jsonBase(converted_datum, val_type1,
                                                                            &tmp_alloc, cs_type1,
                                                                            json_val, ObConv2JsonParam(true,
                                                                            has_lob_header1, true),
                                                                            lob_options))) {
              }
            } else {
              if (OB_FAIL(ObJsonExprHelper::transform_scalar_2jsonBase(converted_datum, val_type1,
                                                                      &tmp_alloc, scale1,
                                                                      eval_ctx_.exec_ctx_.get_my_session()->get_timezone_info(),
                                                                      eval_ctx_.exec_ctx_.get_my_session(),
                                                                      json_val, true))) {
              }
            }

            ObJsonBin *jb_node = nullptr;

            if (OB_FAIL(ret)) {
            } else if (OB_ISNULL(jb_node = static_cast<ObJsonBin *>(json_val))) {
              ret = OB_ERR_UNDEFINED;
            } else if (OB_FAIL(bin_agg.append_key_and_value(key_data, value, jb_node))) {
            } else if (bin_agg.get_approximate_length() > OB_MAX_PACKET_LENGTH * 2) {
              ret = OB_ERR_TOO_LONG_STRING_IN_CONCAT;
            } else {
              tmp_alloc.reset();
            }

          }
        }
      }
    }//end of while
    if (ret == OB_ERR_JSON_DOCUMENT_NULL_KEY) {
      LOG_USER_ERROR(OB_ERR_JSON_DOCUMENT_NULL_KEY);
    }
    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      bin_agg.set_sort_and_unique();
      // output res
      if (OB_FAIL(bin_agg.serialize())) {
      } else {
        ObStringBuffer *buff = bin_agg.get_buffer();
        Datum native_result;
        ObTextStringDatumResult text_result(ObJsonType, aggr_info.expr_->obj_meta_.has_lob_header(), &native_result);
        if (OB_FAIL(text_result.init(buff->length(), &aggr_alloc_))) {
        } else if (OB_FAIL(text_result.append(buff->ptr(), buff->length()))) {
        } else {
          text_result.set_result();
          LegacyAggrValue legacy;
          OZ(legacy.prepare(*aggr_info.expr_, native_result));
          OX(concat_result = legacy.value_);
        }
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::get_ora_json_objectagg_result(const ObAggrInfo &aggr_info,
                                                        GroupConcatExtraResult *&extra,
                                                        ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_back(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObArenaAllocator res_alloc_arr(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObStringBuffer value(&res_alloc_arr);
  const common::ObLobReadOptions *lob_options = nullptr;

  if (OB_FAIL(eval_ctx_.exec_ctx_.get_lob_read_options(lob_options))) {
  } else if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else if (aggr_info.param_exprs_.count() < 2) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObChunkDatumStore::StoredRow *storted_row = nullptr;
    SqlTypeMeta meta_key = aggr_info.param_exprs_.at(0)->obj_meta_;
    SqlTypeMeta meta_value = aggr_info.param_exprs_.at(1)->obj_meta_;

    bool is_format_json = aggr_info.format_json_;
    bool is_absent_on_null = (aggr_info.absent_on_null_ >= 1);
    bool is_strict = aggr_info.strict_json_;
    bool is_with_unique_keys = aggr_info.with_unique_keys_;

    ObJsonBinAggSerializer bin_agg(&res_alloc, static_cast<uint8_t>(ObJsonNodeType::J_OBJECT), &res_alloc_back, &res_alloc_arr);

    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      if (OB_ISNULL(storted_row) || storted_row->cnt_ < 2) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        SqlValueType type_key = meta_key.get_type();
        ObCollationType cs_type_key = meta_key.get_collation_type();

        if (type_key == ObNullType) {
          ret = OB_ERR_JSON_DOCUMENT_NULL_KEY;
          LOG_USER_ERROR(OB_ERR_JSON_DOCUMENT_NULL_KEY);
        } else if (!ob_is_string_type(type_key)) {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
          LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_OP, "CHAR", sql_value_type_to_string(type_key));
        } else if (ob_is_string_type(type_key) && cs_type_key == CS_TYPE_BINARY) {
          ret = OB_ERR_INVALID_JSON_CHARSET;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_CHARSET);
        } else {
          const ObDatum datum_key = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[0]);
          const ObDatum datum_value = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[1]);

          SqlValueType type_value = meta_value.get_type();
          ObCollationType cs_type_value = meta_value.get_collation_type();
          ObScale scale = meta_value.get_scale();

          ObString key_string = datum_key.get_string();
          ObIJsonBase *json_val = nullptr;

          bool need_key_string_convert = (ObCharset::charset_type_by_coll(cs_type_key) != CHARSET_UTF8MB4);

          if (OB_ISNULL(key_string.ptr()) || key_string.length() ==  0) {
            ret = OB_ERR_NULL_VALUE;
          } else if (is_absent_on_null && ob_is_null(type_value)) {
            continue;
          } else if (need_key_string_convert
            && OB_FAIL(ObExprUtil::convert_string_collation(key_string, cs_type_key, key_string, 
                                                            CS_TYPE_UTF8MB4_BIN, tmp_alloc))) {
          } else if (!need_key_string_convert && OB_FAIL(deep_copy_ob_string(tmp_alloc, key_string, key_string))) {
          } else if (OB_FAIL(ObJsonExprHelper::datum_to_json_val(&datum_value, meta_value, &tmp_alloc,
              eval_ctx_.exec_ctx_.get_my_session(), json_val, false, is_format_json, is_strict, true,
              lob_options))) {
          } else if (is_absent_on_null && json_val->json_type() == ObJsonNodeType::J_NULL) {
            // do nothing , continue
          } else {
            ObJsonBin *jb_node = nullptr;
            if (OB_ISNULL(jb_node = static_cast<ObJsonBin *>(json_val))) {
              ret = OB_ERR_UNDEFINED;
            } else if (OB_FAIL(bin_agg.append_key_and_value(key_string, value, jb_node))) {
            } else if (bin_agg.get_approximate_length() > OB_MAX_PACKET_LENGTH * 2) {
              ret = OB_ERR_TOO_LONG_STRING_IN_CONCAT;
            } else {
              tmp_alloc.reset();
            }

          }
        }
      }
    } // end of while
    
    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      ParseNode parse_node;
      bool is_default_type = false;

      parse_node.value_ = aggr_info.returning_type_;
      SqlValueType rsp_type = static_cast<SqlValueType>(parse_node.int16_values_[OB_NODE_CAST_TYPE_IDX]);
      ObCollationType rsp_cs_type = static_cast<ObCollationType>(parse_node.int16_values_[OB_NODE_CAST_COLL_IDX]);
      int32_t rsp_len = parse_node.int32_values_[OB_NODE_CAST_C_LEN_IDX];
      //int64_t result_likely_size = j_obj.get_serialize_size();

      if (!parse_node.value_) {
        is_default_type = true;
        rsp_type = ObJsonType;
        rsp_cs_type = CS_TYPE_UTF8MB4_BIN;
        rsp_len = (ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length();
      }

      ObJsonBuffer string_buffer(&tmp_alloc);
      ObStringBuffer *buff = nullptr;

      
      if (is_strict || (ob_is_json(rsp_type) && !is_default_type) || is_with_unique_keys) {
        bin_agg.set_sort_and_unique();
      }

      if (OB_FAIL(bin_agg.serialize())) {
      } else if ((is_with_unique_keys || ob_is_json(rsp_type) || is_strict)
                  && (bin_agg.get_key_info_count() > bin_agg.get_last_count())) {
        ret = OB_ERR_DUPLICATE_KEY;
      } else if (OB_FALSE_IT(buff = bin_agg.get_buffer())) {
      } else if (ob_is_string_type(rsp_type)) {
        ObIJsonBase *j_base = NULL;
        if (OB_FAIL(ObJsonBaseFactory::get_json_base(&tmp_alloc, 
                                                      buff->string(), 
                                                      ObJsonInType::JSON_BIN, 
                                                      ObJsonInType::JSON_BIN, 
                                                      j_base, 0,
                                                      ObJsonExprHelper::get_json_max_depth_config()))) {
        } else if (OB_FAIL(j_base->print(string_buffer, true, buff->length(), false))) {
        } else if (rsp_type == ObVarcharType && string_buffer.length() > rsp_len) {
          char res_ptr[OB_MAX_DECIMAL_PRECISION] = {0};
          if (OB_ISNULL(ObCharset::lltostr(rsp_len, res_ptr, 10, 1))) {
          } 
          ret = OB_OPERATE_OVERFLOW;
          LOG_USER_ERROR(OB_OPERATE_OVERFLOW, res_ptr, "json_objectagg");
        } else if (OB_FAIL(pack_legacy_aggr_json(*aggr_info.expr_, eval_ctx_, concat_result,
                                                               string_buffer.string(), &aggr_alloc_))) {
        }
      } else if (ob_is_json(rsp_type)) {
        if (OB_FAIL(pack_legacy_aggr_json(*aggr_info.expr_, eval_ctx_, concat_result, buff->string(), &aggr_alloc_))) {
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::single_row_agg(GroupRow &group_row, ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (!support_fast_single_row_agg_) {
    group_row.reuse();
    if (OB_FAIL(prepare(group_row))) {
    } else if (OB_FAIL(collect_group_row(&group_row))) {
    }
  } else if (OB_FAIL(fast_single_row_agg(eval_ctx, aggr_infos_))) {
  }
  return ret;
}

int ObAggregateProcessor::single_row_agg_batch(GroupRow **group_row, ObEvalCtx &eval_ctx, const int64_t batch_size, const ObBitVector *skip)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(group_row) && OB_NOT_NULL(skip));
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
  batch_info_guard.set_batch_size(batch_size);
  if (OB_FAIL(ret)) {
  } else if (!support_fast_single_row_agg_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip->at(i)) {
        continue;
      }
      if (OB_ISNULL(group_row[i])) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        group_row[i]->reuse();
        batch_info_guard.set_batch_idx(i);
        if (OB_FAIL(prepare(*group_row[i]))) {
        } else if (OB_FAIL(collect_group_row(group_row[i]))) {
        }
      }
    }
  } else if (OB_FAIL(fast_single_row_agg_batch(eval_ctx, batch_size, skip))) {
  }
  return ret;
}

int ObAggregateProcessor::fast_single_row_agg(
    ObEvalCtx &eval_ctx, ObAggrInfo &aggr_info)
{
  int ret = OB_SUCCESS;
  if (!aggr_info.is_implicit_first_aggr()) {
    for (int64_t j = 0; OB_SUCC(ret) && j < aggr_info.param_exprs_.count(); ++j) {
      Datum *tmp_res = nullptr;
      if (OB_FAIL(aggr_info.param_exprs_.at(j)->eval(eval_ctx, tmp_res))) {
      }
    }
    if (OB_SUCC(ret)) {
      const ObItemType aggr_fun = aggr_info.get_expr_type();
      alignas(8) char result_buffer[ObNumber::MAX_CALC_BYTE_LEN + 64];
      common::ObDatum result;
      result.ptr_ = result_buffer;
      LegacyAggrValue input;
      if (!aggr_info.param_exprs_.empty()) {
        OZ(input.prepare(*aggr_info.param_exprs_.at(0), aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx)));
      }
      if (OB_SUCC(ret)) switch (aggr_fun) {
        case T_FUN_COUNT: {
          bool has_null = false;
          for (int64_t j = 0; !has_null && j < aggr_info.param_exprs_.count(); ++j) {
            has_null = aggr_info.param_exprs_.at(j)->locate_expr_datum(eval_ctx).is_null();
          }
          result.set_int(has_null ? 0 : 1);
          break;
        }
        case T_FUN_SUM: {
          const SqlTypeClass tc = sql_type_class(aggr_info.get_first_child_type());
          if ((ObIntTC == tc || ObUIntTC == tc) && !aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx).is_null()) {
            if (ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
              const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
              if (ObIntTC == tc) {
                const int64_t val = aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx).get_int();
                if (OB_FAIL(DecIntAggFuncCtx::int_to_decimalint(val, precision, result))) {
                }
              } else {
                const uint64_t val = aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx).get_uint();
                if (OB_FAIL(DecIntAggFuncCtx::int_to_decimalint(val, precision, result))) {
                }
              }
            } else {
              ObNumStackAllocator<2> tmp_alloc;
              ObNumber result_nmb;
              if (ObIntTC == tc) {
                if (OB_FAIL(result_nmb.from(aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx).get_int(), tmp_alloc))) {
                }
              } else {
                if (OB_FAIL(result_nmb.from(aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx).get_uint(), tmp_alloc))) {
                }
              }
              OX (result.set_number(result_nmb));
            }
          } else if (ObDecimalIntTC == tc && !aggr_info.param_exprs_.at(0)->locate_expr_datum(eval_ctx).is_null()) {
            DecIntAggFuncCtx *ctx = nullptr;
            const int16_t scale = aggr_info.get_first_child_datum_scale();
            const ObDecimalInt *arg =
              input.value_.get_decimal_int();
            if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(ctx->store_func(result, arg, scale))) {
            }
          } else {
            result.set_datum(input.value_);
          }
          break;
        }
        case T_FUN_SYS_BIT_AND:
        case T_FUN_SYS_BIT_OR:
        case T_FUN_SYS_BIT_XOR:
        case T_FUN_MAX:
        case T_FUN_MIN: {
          result.set_datum(input.value_);
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
        }
      }
      if (OB_SUCC(ret)) { ret = publish_legacy_aggr(*aggr_info.expr_, eval_ctx, result); }
    }
  }
  return ret;
}

int ObAggregateProcessor::fast_single_row_agg(ObEvalCtx &eval_ctx, ObIArray<ObAggrInfo> &aggr_infos)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos.count(); ++i) {
    ObAggrInfo &aggr_info = aggr_infos.at(i);
    if (OB_FAIL(fast_single_row_agg(eval_ctx, aggr_info))) {
    }
  }
  return ret;
}

int ObAggregateProcessor::fast_single_row_agg_batch(ObEvalCtx &eval_ctx, const int64_t batch_size, const ObBitVector *skip)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(skip));
  for (int64_t i = 0; OB_SUCC(ret) && i < aggr_infos_.count(); ++i) {
    ObAggrInfo &aggr_info = aggr_infos_.at(i);
    if (aggr_info.is_implicit_first_aggr()) {
      continue;
    }
    const ObItemType aggr_fun = aggr_info.get_expr_type();
    switch (aggr_fun) {
      case T_FUN_COUNT: {
        bool has_null[batch_size];
        MEMSET(has_null, false, sizeof(bool) * batch_size);
        Datum *result = aggr_info.expr_->locate_datums_for_update(eval_ctx, batch_size);
        for (int64_t j = 0; j < aggr_info.param_exprs_.count(); ++j) {
          DatumVector param_vec = aggr_info.param_exprs_.at(j)->locate_expr_datumvector(eval_ctx);
          for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
            if (skip->at(batch_idx) || has_null[batch_idx]) {
              continue;
            }
            has_null[batch_idx] = param_vec.at(batch_idx)->is_null();
          }
        }
        for (int64_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
          if (skip->at(batch_idx)) {
            continue;
          }
          result[batch_idx].set_int(has_null[batch_idx] ? 0 : 1);
        }
        break;
      }
      case T_FUN_SUM: {
        const SqlTypeClass tc = sql_type_class(aggr_info.get_first_child_type());
        Datum *result = aggr_info.expr_->locate_datums_for_update(eval_ctx, batch_size);
        DatumVector param_vec = aggr_info.param_exprs_.at(0)->locate_expr_datumvector(eval_ctx);
        alignas(8) char math_buffer[ObNumber::MAX_CALC_BYTE_LEN + 64];
        common::ObDatum math_result;
        math_result.ptr_ = math_buffer;
        ObEvalCtx::BatchInfoScopeGuard result_guard(eval_ctx);
        if (ObIntTC == tc && ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
          for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
            if (skip->at(batch_idx)) {
              continue;
            }
            if (param_vec.at(batch_idx)->is_null()) {
              result[batch_idx].set_null();
            } else if (OB_FAIL(DecIntAggFuncCtx::int_to_decimalint(
                                  param_vec.at(batch_idx)->get_int(), precision, math_result))) {
            } else {
              result_guard.set_batch_idx(batch_idx);
              OZ(publish_legacy_aggr(*aggr_info.expr_, eval_ctx, math_result));
            }
          }
        } else if (ObIntTC == tc) { // number
          for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
            if (skip->at(batch_idx)) {
              continue;
            }
            ObNumStackAllocator<2> tmp_alloc;
            ObNumber result_nmb;
            if (param_vec.at(batch_idx)->is_null()) {
              result[batch_idx].set_null();
            } else if (OB_FAIL(result_nmb.from(param_vec.at(batch_idx)->get_int(), tmp_alloc))) {
            } else {
              OZ(ObExprUtil::set_number(*aggr_info.expr_, eval_ctx, result[batch_idx], result_nmb, batch_idx));
            }
          } 
        } else if (ObUIntTC == tc && ob_is_decimal_int(aggr_info.expr_->datum_meta_.type_)) {
          const int64_t precision = aggr_info.expr_->datum_meta_.precision_;
          for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
            if (skip->at(batch_idx)) {
              continue;
            }
            if (param_vec.at(batch_idx)->is_null()) {
              result[batch_idx].set_null();
            } else if (OB_FAIL(DecIntAggFuncCtx::int_to_decimalint(
                                  param_vec.at(batch_idx)->get_uint64(), precision, math_result))) {
            } else {
              result_guard.set_batch_idx(batch_idx);
              OZ(publish_legacy_aggr(*aggr_info.expr_, eval_ctx, math_result));
            }
          }
        } else if (ObUIntTC == tc) { // number
          for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
            if (skip->at(batch_idx)) {
              continue;
            }
            ObNumStackAllocator<2> tmp_alloc;
            ObNumber result_nmb;
            if (param_vec.at(batch_idx)->is_null()) {
              result[batch_idx].set_null();
            } else if (OB_FAIL(result_nmb.from(param_vec.at(batch_idx)->get_uint64(), tmp_alloc))) {
            } else {
              OZ(ObExprUtil::set_number(*aggr_info.expr_, eval_ctx, result[batch_idx], result_nmb, batch_idx));
            }
          } 
        } else if (ObDecimalIntTC == tc) {
          DecIntAggFuncCtx *ctx = nullptr;
          const int16_t scale = aggr_info.get_first_child_datum_scale();
          if (OB_ISNULL(ctx = get_decint_aggr_func_ctx(aggr_info))) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
              if (skip->at(batch_idx)) {
                continue;
              }
              if (param_vec.at(batch_idx)->is_null()) {
                result[batch_idx].set_null();
              } else {
                LegacyAggrValue input;
                if (OB_FAIL(input.prepare(*aggr_info.param_exprs_.at(0), *param_vec.at(batch_idx)))) {
                } else if (OB_FAIL(ctx->store_func(math_result, input.value_.get_decimal_int(), scale))) {
                } else {
                  result_guard.set_batch_idx(batch_idx);
                  OZ(publish_legacy_aggr(*aggr_info.expr_, eval_ctx, math_result));
                }
              }
            }
          }
        } else {
          for (int64_t batch_idx = 0; OB_SUCC(ret) && batch_idx < batch_size; ++batch_idx) {
            if (skip->at(batch_idx)) {
              continue;
            }
            result[batch_idx].set_datum(*param_vec.at(batch_idx));
          } 
        }
        break;
      }
      case T_FUN_MAX:
      case T_FUN_MIN: {
        Datum *result = aggr_info.expr_->locate_batch_datums(eval_ctx);
        DatumVector param_vec = aggr_info.param_exprs_.at(0)->locate_expr_datumvector(eval_ctx);
        for (int64_t batch_idx = 0; batch_idx < batch_size; ++batch_idx) {
          if (skip->at(batch_idx)) {
            continue;
          }
          result[batch_idx].set_datum(*param_vec.at(batch_idx));
        } 
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::get_asmvt_result(const ObAggrInfo &aggr_info,
                                           GroupConcatExtraResult *&extra,
                                           ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  common::ObArenaAllocator tmp_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  common::ObILobReadService *lob_read_service =
      eval_ctx_.exec_ctx_.get_lob_read_service();
  if (OB_ISNULL(lob_read_service)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else {
    const ObChunkDatumStore::StoredRow *storted_row = NULL;
    bool inited_values = false;
    DatumValue *values = NULL;
    mvt_agg_result mvt_res(tmp_alloc, *lob_read_service);
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      if (OB_ISNULL(storted_row)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        common::ObArenaAllocator single_row_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
        // Keep stored Datum values paired with their expression metadata.
        if (!inited_values
            && OB_ISNULL(values = static_cast<DatumValue*>(tmp_alloc.alloc(sizeof(DatumValue) * storted_row->cnt_)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (!inited_values && FALSE_IT(inited_values = true)) {
        } else if (OB_FAIL(convert_datum_to_values(aggr_info, *storted_row, values, storted_row->cnt_))) {
        } else if (!mvt_res.is_inited()
                   && OB_FAIL(init_asmvt_result(tmp_alloc, aggr_info, values, storted_row->cnt_, mvt_res))) {
        } else if (FALSE_IT(mvt_res.set_tmp_allocator(&single_row_alloc))) {
        } else if (OB_FAIL(mvt_res.generate_feature(values, storted_row->cnt_))) {
        }
      }
    }//end of while

    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      ObString mvt_str;
      if (OB_FAIL(mvt_res.mvt_pack(mvt_str))) {
      } else {
        ObString blob_locator;
        ObExprStrResAlloc expr_res_alloc(*aggr_info.expr_, eval_ctx_);
        ObTextStringResult blob_res(ObLongTextType, true, &expr_res_alloc);
        int64_t total_length = mvt_str.length();
        if (OB_FAIL(ret)) { 
        } else if (OB_FAIL(blob_res.init(total_length))) {
        } else if (OB_FAIL(blob_res.append(mvt_str))) {
        } else {
          blob_res.get_result_buffer(blob_locator);
          concat_result.set_string(blob_locator.ptr(), blob_locator.length());
        }
      }
    }
  }
  return ret;
}

int ObAggregateProcessor::init_asmvt_result(ObIAllocator &allocator, 
                                            const ObAggrInfo &aggr_info,
                                            const DatumValue *values,
                                            uint32_t value_cnt,
                                            mvt_agg_result &mvt_res)
{
  int ret = OB_SUCCESS;
  bool is_param_done = false;
  int param_cnt = 0;
  mvt_res.inited_ = true;
  // try get first geom column
  for (uint32_t i = 0; i < aggr_info.param_exprs_.count() && OB_SUCC(ret); i++) {
    if (i == 0) {
      param_cnt = values[i].get_int();
      mvt_res.column_offset_ = param_cnt + 1;
    } 
    ObExpr *expr = aggr_info.param_exprs_.at(i);
    if (i >= mvt_res.column_offset_ && expr->type_ == T_REF_COLUMN) {
      is_param_done = true;
      if (i + 1 >= value_cnt) {
        ret = OB_ERR_UNEXPECTED;
      } else if (expr->obj_meta_.is_geometry() && mvt_res.geom_idx_ == UINT32_MAX) {
        if (mvt_res.geom_name_.empty()) {
          mvt_res.geom_idx_ = i;
          if (OB_FAIL(ob_write_string(allocator, values[i + 1].get_string(), mvt_res.geom_name_))) {
            // geo_name
          } 
        } else if (mvt_res.geom_name_.case_compare(values[i + 1].get_string()) == 0) {
          mvt_res.geom_idx_ = i;
        } else if (OB_FAIL(mvt_res.add_key(values[i + 1].get_string()))) {
        }
      } else if (!mvt_res.feature_id_name_.empty() && mvt_res.feat_id_idx_ == UINT32_MAX
                && mvt_res.feature_id_name_.case_compare(values[i + 1].get_string()) == 0
                && ob_is_numeric_type(expr->obj_meta_.get_type())) {
        // feature id column name won't add to keys
        mvt_res.feat_id_idx_ = i;
      } else if (!expr->obj_meta_.is_json() // json type will be expanded
                && !expr->obj_meta_.is_enum_or_set()
                && OB_FAIL(mvt_res.add_key(values[i + 1].get_string()))) {
      }
    } else if (!is_param_done) {
      SqlValueType type = values[i].get_type();
      ObString content;
      if (values[i].is_null()) {
        // do nothing, use default value
      } else if (i == 1) {
        // layer name
        if (!ob_is_string_tc(type) && !ob_is_large_text(type) && !ob_is_geometry_tc(type)) {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
        } else if (ob_is_geometry_tc(type)) {
          ObString cast_string;
          if (OB_FAIL(mvt_res.geometry_to_varchar(values[i], allocator, cast_string))) {
          } else {
            ObString hex_string;
            if (OB_FAIL(ObHexUtilsBase::hex(hex_string, allocator, cast_string.ptr(), cast_string.length()))) {
            } else if (OB_FAIL(ob_write_string(allocator, hex_string, mvt_res.lay_name_, true))) {
            }
          }
        } else {
          content = values[i].get_string();
          if (mvt_agg_result::is_upper_char_exist(content)) {
            ret = OB_ERR_BAD_FIELD_ERROR;
          } else if (OB_FAIL(ob_write_string(allocator, content, mvt_res.lay_name_, true))) {
          }
        }
      } else if (i == 2) {
        // extent
        DatumValue extent_value = values[i];
        if (!ob_is_int_tc(type) && !ob_is_uint_tc(type)) {
          if (expr->is_static_const_) {
            DatumCastParams cast_ctx(&allocator, nullptr, CM_NONE, ObCharset::get_system_collation());
            if (OB_FAIL(DatumValueCaster::to_type(ObInt32Type, cast_ctx, values[i], allocator, extent_value))) {
            } else {
              // DatumValueCaster has already applied the target integer width.
            }
          } else {
            ret = OB_ERR_INVALID_TYPE_FOR_OP;
          }
        } 
        if (OB_FAIL(ret)) {
        } else if (extent_value.get_int() == 0
                   || (ob_is_uint_tc(type)
                       ? extent_value.get_uint64() > static_cast<uint64_t>(INT32_MAX)
                       : extent_value.get_int() < INT32_MIN || extent_value.get_int() > INT32_MAX)) {
          ret = OB_ERR_ARGUMENT_OUT_OF_RANGE;
          LOG_USER_ERROR(OB_ERR_ARGUMENT_OUT_OF_RANGE, extent_value.get_int());
        } else {
          mvt_res.extent_ = extent_value.get_int();
        }
      } else if (i == 3) {
        // geo_name
        if (!ob_is_string_tc(type)) {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
        } else if (values[i].get_string().empty()) {
          ret = OB_ERR_BAD_FIELD_ERROR;
        } else if (mvt_agg_result::is_upper_char_exist(values[i].get_string())) {
          ret = OB_ERR_BAD_FIELD_ERROR;
        } else if (OB_FAIL(ob_write_string(allocator, values[i].get_string(), mvt_res.geom_name_))) {
        }
      } else if (i == 4) {
        // feature id name
        if (!ob_is_string_tc(type)) {
          ret = OB_ERR_INVALID_TYPE_FOR_OP;
        } else if (mvt_agg_result::is_upper_char_exist(values[i].get_string())) {
          ret = OB_ERR_BAD_FIELD_ERROR;
        } else if (OB_FAIL(ob_write_string(allocator, values[i].get_string(), mvt_res.feature_id_name_))) {
        } else if (mvt_res.feature_id_name_.empty()) {
          ret = OB_WRONG_COLUMN_NAME;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (!mvt_res.feature_id_name_.empty() && mvt_res.feat_id_idx_ == UINT32_MAX) {
      // can't find feature id column
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
    } else if (OB_FAIL(mvt_res.init_layer())) {
    }
  }
  return ret;
}

int ObAggregateProcessor::get_array_agg_result(const ObAggrInfo &aggr_info,
                                               GroupConcatExtraResult *&extra,
                                               ObDatum &concat_result)
{
  int ret = OB_SUCCESS;
  ObIArrayType *arr_obj = NULL;
  const uint16_t meta_id = aggr_info.expr_->obj_meta_.get_subschema_id();
  common::ObArenaAllocator tmp_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("ARRAY_AGG"));
  if (OB_ISNULL(extra) || OB_UNLIKELY(extra->empty())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra->is_iterated() && OB_FAIL(extra->rewind())) {
    // Group concat row may be iterated in rollup_process(), rewind here.
  } else if (!extra->is_iterated() && OB_FAIL(extra->finish_add_row())) {
  } else if (OB_FAIL(ObArrayExprUtils::construct_array_obj(tmp_alloc, eval_ctx_, meta_id, arr_obj, false))) {
  } else {
    const ObChunkDatumStore::StoredRow *storted_row = NULL;
    SqlTypeMeta elem_meta = aggr_info.param_exprs_.at(0)->obj_meta_;
    SqlValueType elem_type = elem_meta.get_type();
    while (OB_SUCC(ret) && OB_SUCC(extra->get_next_row(storted_row))) {
      if (OB_ISNULL(storted_row)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        const ObDatum datum_val = StoredDatumCodec::decode_legacy_view(storted_row->stored_cells()[0]);
        if (datum_val.is_null()) {
          if (OB_FAIL(arr_obj->push_null())) {
          }
        } else if (ob_is_collection_sql_type(elem_type)) {
          common::ObArenaAllocator single_row_alloc(ObModIds::OB_SQL_AGGR_FUNC, OB_MALLOC_NORMAL_BLOCK_SIZE);
          ObArrayNested *nest_array = static_cast<ObArrayNested *>(arr_obj);
          Datum native_element;
          if (OB_FAIL(aggr_info.param_exprs_.at(0)->get_datum_copy_plan().import_legacy(datum_val, native_element))) {
          } else if (OB_FAIL(ObArrayExprUtils::add_elem_to_nested_array(single_row_alloc, eval_ctx_, elem_meta.get_subschema_id(),
                                                                 native_element, nest_array))) {
          }
        } else if (OB_FAIL(ObArrayUtil::append(*arr_obj, elem_type, &datum_val))) {
        }
      }
    }//end of while

    if (ret != OB_ITER_END && ret != OB_SUCCESS) {
    } else {
      ret = OB_SUCCESS;
      ObString res_str;
      if (OB_FAIL(ObArrayExprUtils::set_array_res(arr_obj, arr_obj->get_raw_binary_len(), *aggr_info.expr_, eval_ctx_, res_str))) {
      } else {
        concat_result.set_string(res_str);
      }
    }
  }
  return ret;
}


int ObAggregateProcessor::check_rows_prefix_str_equal_for_hybrid_hist(const ObChunkDatumStore::LastStoredRow &prev_row,
                                                                      const ObChunkDatumStore::StoredRow &cur_row,
                                                                      const ObAggrInfo &aggr_info,
                                                                      const SqlTypeMeta &obj_meta,
                                                                      bool &is_equal)
{
  int ret = OB_SUCCESS;
  is_equal = false;
  if (OB_ISNULL(prev_row.store_row_) ||
      OB_UNLIKELY(!obj_meta.is_lob_storage() ||
                  prev_row.store_row_->cnt_ != cur_row.cnt_ ||
                  aggr_info.sort_collations_.count() != cur_row.cnt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_equal = true;
    for (int64_t i = 0; OB_SUCC(ret) && is_equal && i < aggr_info.sort_collations_.count(); ++i) {
      uint32_t index = aggr_info.sort_collations_.at(i).field_idx_;
      common::ObArenaAllocator tmp_alloctor("CalcHybridHist", OB_MALLOC_NORMAL_BLOCK_SIZE);
      ObDatum new_prev_datum;
      ObDatum new_cur_datum;
      if (OB_UNLIKELY(index >= prev_row.store_row_->cnt_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObHybridHistograms::build_prefix_str_datum_for_lob(tmp_alloctor,
                                                                            obj_meta,
                                                                            StoredDatumCodec::decode_legacy_view(prev_row.store_row_->stored_cells()[index]),
                                                                            new_prev_datum,
                                                                            datum_access_ctx_)) ||
                 OB_FAIL(ObHybridHistograms::build_prefix_str_datum_for_lob(tmp_alloctor,
                                                                            obj_meta,
                                                                            StoredDatumCodec::decode_legacy_view(cur_row.stored_cells()[index]),
                                                                            new_cur_datum,
                                                                            datum_access_ctx_))) {
      } else {
        int cmp_ret = 0;
        if (OB_FAIL(aggr_info.sort_cmp_funcs_.at(i).cmp_func_(new_prev_datum,
                                                              new_cur_datum,
                                                              cmp_ret,
                                                              datum_access_ctx_))) {
        } else {
          is_equal = 0 == cmp_ret;
        }
      }
    }
  }
  return ret;
}

ObAggregateProcessor::ObDecIntAggOpFunc ObAggregateProcessor::DEC_INT_OP_FUNCS[DECIMAL_INT_MAX][3][3];
ObAggregateProcessor::ObDecIntAggOpFunc ObAggregateProcessor::DEC_INT_MERGE_FUNCS[DECIMAL_INT_MAX][3];
ObAggregateProcessor::ObDecIntAggOpBatchFunc ObAggregateProcessor::DEC_INT_ADD_BATCH_FUNCS[DECIMAL_INT_MAX][3][2][2];
ObAggregateProcessor::ObDecIntAggOpBatchFunc ObAggregateProcessor::DEC_INT_MERGE_BATCH_FUNCS[DECIMAL_INT_MAX][2];
int ObAggregateProcessor::DecIntAggFuncCtx::init(const ObAggrInfo &aggr_info,
                                                 const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  const int16_t arg_prec = aggr_info.get_first_child_datum_precision();
  const int16_t declared_res_prec = aggr_info.expr_->datum_meta_.precision_;
  // The declared result precision of an aggregate may be inconsistent with the
  // precision of its physically produced input. E.g. for SUM over EXISTS the
  // declared precision is inflated from the integer type max at deduce time,
  // while the materialized partial column of a three-stage aggregation keeps
  // the small boolean precision, so the declared precision (55) exceeds
  // arg_prec (26) by more than OB_DECIMAL_LONGLONG_DIGITS. Such metadata drift
  // only affects the display width: a sum over at most 2^63 input values always
  // fits into arg_prec + OB_DECIMAL_LONGLONG_DIGITS digits. Clamp the effective
  // precision into that range instead of failing the whole query.
  int16_t res_prec = declared_res_prec;
  if (arg_prec >= 0 && (declared_res_prec < arg_prec
                        || declared_res_prec - arg_prec > OB_DECIMAL_LONGLONG_DIGITS)) {
    res_prec = declared_res_prec < arg_prec ? arg_prec
                                            : arg_prec + OB_DECIMAL_LONGLONG_DIGITS;
    LOG_WARN("declared aggregate result precision is inconsistent with the input "
             "precision, clamp it and continue",
             K(arg_prec), K(declared_res_prec), K(res_prec));
  }
  const int arg_type = static_cast<int>(get_decimalint_type(arg_prec));
  const int16_t buffer_prec = get_max_decimalint_precision(arg_prec) - arg_prec;
  const bool need_cast = (buffer_prec < MAX_PRECISION_DECIMAL_INT_64) &&
                           get_scale_factor<int64_t>(buffer_prec) < batch_size;
  if (OB_UNLIKELY(arg_prec < 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (res_prec == arg_prec) { // mysql mode, merge result for decimal int
    add_func = DEC_INT_MERGE_FUNCS[arg_type][0];
    sub_func = DEC_INT_MERGE_FUNCS[arg_type][1];
    store_func = DEC_INT_MERGE_FUNCS[arg_type][2];
    merge_func = DEC_INT_MERGE_FUNCS[arg_type][0];
    add_batch_func = DEC_INT_MERGE_BATCH_FUNCS[arg_type];
    number_result_ = false;
  } else { // mysql mode
    const int res_type = static_cast<int>(get_decimalint_type(res_prec));
    int res_func_idx = 0;
    if (OB_UNLIKELY(res_prec < arg_prec || res_prec - arg_prec > OB_DECIMAL_LONGLONG_DIGITS)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      switch (arg_type) {
        case DECIMAL_INT_32:
        case DECIMAL_INT_128:
        case DECIMAL_INT_512:
          res_func_idx = 0;
          break;
        case DECIMAL_INT_64:
          res_func_idx = res_type == DECIMAL_INT_128 ? 0 : 1;
          break;
        case DECIMAL_INT_256:
          res_func_idx = res_type == DECIMAL_INT_256 ? 0 : 1;
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret)) {
        add_func = DEC_INT_OP_FUNCS[arg_type][res_func_idx][0];
        sub_func = DEC_INT_OP_FUNCS[arg_type][res_func_idx][1];
        store_func = DEC_INT_OP_FUNCS[arg_type][res_func_idx][2];
        merge_func = DEC_INT_MERGE_FUNCS[res_type][0];
        add_batch_func = DEC_INT_ADD_BATCH_FUNCS[arg_type][res_func_idx][need_cast];
        number_result_ = (res_func_idx == 2);
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(add_func) || OB_ISNULL(sub_func) || OB_ISNULL(store_func) ||
          (OB_ISNULL(merge_func)) || OB_ISNULL(add_batch_func)) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

template<typename T>
int ObAggregateProcessor::DecIntAggFuncCtx::int_to_decimalint(const T int_val,
                                                              const int16_t to_precision,
                                                              ObDatum &result)
{
  int ret = OB_SUCCESS;
  if (to_precision <= MAX_PRECISION_DECIMAL_INT_128) {
    *const_cast<int128_t *>(result.get_decimal_int()->int128_v_) = int_val;
    result.pack_ = sizeof(int128_t);
  } else {
    *const_cast<int256_t *>(result.get_decimal_int()->int256_v_) = int_val;
    result.pack_ = sizeof(int256_t);
  }
  return ret;
}

template<typename T>
int ObAggregateProcessor::DecIntAggFuncCtx::add_int(ObDatum &result, const int16_t precison,
                                                    const T val1, const T val2)
{
  int ret = OB_SUCCESS;
  if (precison <= MAX_PRECISION_DECIMAL_INT_128) {
    *const_cast<int128_t *>(result.get_decimal_int()->int128_v_) =
      *result.get_decimal_int()->int128_v_ + val1 + val2;
    result.pack_ = sizeof(int128_t);
  } else {
    *const_cast<int256_t *>(result.get_decimal_int()->int256_v_) = 
      *result.get_decimal_int()->int256_v_ + val1 + val2;
    result.pack_ = sizeof(int256_t);
  }
  return ret;
}

template<typename T>
int ObAggregateProcessor::DecIntAggFuncCtx::sub_int(ObDatum &result, const int16_t precison,
                                                    const T val1)
{
  int ret = OB_SUCCESS;
  if (precison <= MAX_PRECISION_DECIMAL_INT_128) {
    *const_cast<int128_t *>(result.get_decimal_int()->int128_v_) = 
      *result.get_decimal_int()->int128_v_ - val1;
    result.pack_ = sizeof(int128_t);
  } else {
   *const_cast<int256_t *>(result.get_decimal_int()->int256_v_) =
      *result.get_decimal_int()->int256_v_ - val1;
    result.pack_ = sizeof(int256_t);
  }
  return ret;
}

void ObAggregateProcessor::check_mysql_decimal_int_overflow(ObDatum &datum)
{
  const int512_t val = datum.get_decimal_int512();
  if (OB_UNLIKELY(val <= wide::ObDecimalIntConstValue::MYSQL_DEC_INT_MIN
        || val >= wide::ObDecimalIntConstValue::MYSQL_DEC_INT_MAX)) {
    int ret = OB_ERR_TRUNCATED_WRONG_VALUE;
    ObString decimal_type_str("DECIMAL");
    char buf[MAX_PRECISION_DECIMAL_INT_512];
    int64_t pos = 0;
    wide::to_string(wide::ObDecimalIntConstValue::MYSQL_DEC_INT_MAX_AVAILABLE, buf,
                    MAX_PRECISION_DECIMAL_INT_512, pos);
    LOG_USER_WARN(OB_ERR_TRUNCATED_WRONG_VALUE, decimal_type_str.length(), decimal_type_str.ptr(),
                static_cast<int32_t>(pos), buf);
    // overflow, set datum to max available decimal int
    const ObDecimalInt *max_available_val =
      reinterpret_cast<const ObDecimalInt*>(&(wide::ObDecimalIntConstValue::MYSQL_DEC_INT_MAX_AVAILABLE));
    datum.set_decimal_int(max_available_val, sizeof(int512_t));
    ret = OB_SUCCESS; // reset ret to SUCCESS, just log user warnings
  }
}

bool ObAggregateProcessor::has_listagg_non_const_separator() const
{
  bool has_it = false;
  return has_it;
}

template<typename RES_T, typename ARG_T>
struct ObDecIntSumOpFunc
{
  static int add(ObDatum &result, const ObDecimalInt *arg, const int16_t scale)
  {
    UNUSED(scale);
    *const_cast<RES_T *>(reinterpret_cast<const RES_T *>(result.decimal_int_)) +=
      *reinterpret_cast<const ARG_T *>(arg);
    return OB_SUCCESS;
  }

  static int sub(ObDatum &result, const ObDecimalInt *arg, const int16_t scale)
  {
    UNUSED(scale);
    *const_cast<RES_T *>(reinterpret_cast<const RES_T *>(result.decimal_int_)) -=
      *reinterpret_cast<const ARG_T *>(arg);
    return OB_SUCCESS;
  }

  static int store(ObDatum &result, const ObDecimalInt *arg, const int16_t scale)
  {
    UNUSED(scale);
    *const_cast<RES_T *>(reinterpret_cast<const RES_T *>(result.decimal_int_)) =
      *reinterpret_cast<const ARG_T *>(arg);
    result.pack_ = sizeof(RES_T);
    return OB_SUCCESS;
  }
};

template<typename ARG_T>
struct ObDecIntSumOpFunc<ObNumber, ARG_T>
{
  static int add(ObDatum &result, const ObDecimalInt *arg, const int16_t scale)
  {
    int ret = OB_SUCCESS;
    ObNumStackAllocator<2> tmp_alloc;
    number::ObNumber right_nmb;
    if (OB_FAIL(wide::to_number(arg, sizeof(ARG_T), scale, tmp_alloc, right_nmb))) {
    } else {
      ObNumber left_nmb(result.get_number());
      ObNumber result_nmb;
      ObDatum res_datum;
      if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, false))) {
      } else {
        result.set_number(result_nmb);
      }
    }
    return ret;
  }

  static int sub(ObDatum &result, const ObDecimalInt *arg, const int16_t scale)
  {
    int ret = OB_SUCCESS;
    ObNumStackAllocator<2> tmp_alloc;
    number::ObNumber right_nmb;
    if (OB_FAIL(wide::to_number(arg, sizeof(ARG_T), scale, tmp_alloc, right_nmb))) {
    } else {
      ObNumber left_nmb(result.get_number());
      ObNumber result_nmb;
      ObDatum res_datum;
      if (OB_FAIL(left_nmb.sub_v3(right_nmb, result_nmb, tmp_alloc, false))) {
      } else {
        result.set_number(result_nmb);
      }
    }
    return ret;
  }

  static int store(ObDatum &result, const ObDecimalInt *arg, const int16_t scale)
  {
    int ret = OB_SUCCESS;
    ObNumStackAllocator<1> tmp_alloc;
    number::ObNumber right_nmb;
    if (OB_FAIL(wide::to_number(arg, sizeof(ARG_T), scale, tmp_alloc, right_nmb))) {
    } else {
      result.set_number(right_nmb);
    }
    return ret;
  }
};


#define DECIMAL_INT_BATCH_ACCUM                                               \
  uint16_t i = 0;                                                             \
  bool accumulated = false;                                                   \
  CALC_T accum = 0;                                                           \
  const SELECTOR &selector = *static_cast<const SELECTOR *>(sel);             \
  for (auto it = selector.begin(); it < selector.end(); selector.next(it)) {  \
    i = selector.get_batch_index(it);                                         \
    if (src.at(i)->is_null()) {                                               \
      continue;                                                               \
    }                                                                         \
    accum += *reinterpret_cast<const ARG_T *>(src.at(i)->get_decimal_int());  \
    accumulated = true;                                                       \
  }

template<typename RES_T, typename CALC_T, typename ARG_T, typename SELECTOR>
struct ObDecIntSumBatchOpFunc
{
  static int add_batch(ObDatum &result, ObAggregateProcessor *processor,
                       ObAggregateProcessor::AggrCell &aggr_cell, const ObDatumVector &src,
                       const void *sel, const int16_t scale)
  {
    UNUSED(scale);
    int ret = OB_SUCCESS;
    DECIMAL_INT_BATCH_ACCUM;
    if (OB_LIKELY(accumulated)) {
      if (result.is_null()) {
        const int64_t need_size = sizeof(RES_T);
        if (OB_FAIL(processor->reserve_kernel_cell(aggr_cell,
            ObAggregateProcessor::IterKernelLayout(
                ObAggregateProcessor::IterValueDomain::DecimalCoefficient, sizeof(RES_T)), need_size, result))) {
        } else {
          *const_cast<RES_T *>(reinterpret_cast<const RES_T *>(result.decimal_int_)) = accum;
          result.pack_ = sizeof(RES_T);
        }
      } else {
        *const_cast<RES_T *>(reinterpret_cast<const RES_T *>(result.decimal_int_)) += accum;
      }
    }
    return ret;
  }
};

template<typename CALC_T, typename ARG_T, typename SELECTOR>
struct ObDecIntSumBatchOpFunc<ObNumber, CALC_T, ARG_T, SELECTOR>
{
  static int add_batch(ObDatum &result, ObAggregateProcessor *processor,
                       ObAggregateProcessor::AggrCell &aggr_cell, const ObDatumVector &src,
                       const void *sel, const int16_t scale)
  {
    int ret = OB_SUCCESS;
    DECIMAL_INT_BATCH_ACCUM;
    if (OB_LIKELY(accumulated)) {
      if (result.is_null()) {
        ObNumStackAllocator<2> tmp_alloc;
        number::ObNumber right_nmb;
        if (OB_FAIL(wide::to_number(accum, scale, tmp_alloc, right_nmb))) {
        } else if (OB_FAIL(processor->clone_number_cell(right_nmb, aggr_cell, &result))) {
        }
      } else {
        ObNumStackAllocator<2> tmp_alloc;
        number::ObNumber right_nmb;
        if (OB_FAIL(wide::to_number(accum, scale, tmp_alloc, right_nmb))) {
        } else {
          ObNumber left_nmb(result.get_number());
          ObNumber result_nmb;
          ObDatum res_datum;
          if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, false))) {
          } else {
            result.set_number(result_nmb);
          }
        }
      }
    }
    return ret;
  }
};

#undef DECIMAL_INT_BATCH_ACCUM

struct InitDecIntAggFunc
{
  static int init()
  {
    auto &op_funcs = ObAggregateProcessor::DEC_INT_OP_FUNCS; // (from_type, to_type, add/sub/store)
    auto &merge_funcs = ObAggregateProcessor::DEC_INT_MERGE_FUNCS; // (merge_result_type, add or sub)

#define INIT_OP_FUNCS(idx, type) \
    op_funcs[DECIMAL_INT_32][0][idx] = ObDecIntSumOpFunc<int128_t, int32_t>::type; \
    op_funcs[DECIMAL_INT_32][1][idx] = NULL; \
    op_funcs[DECIMAL_INT_32][2][idx] = ObDecIntSumOpFunc<ObNumber, int32_t>::type; \
    op_funcs[DECIMAL_INT_64][0][idx] = ObDecIntSumOpFunc<int128_t, int64_t>::type; \
    op_funcs[DECIMAL_INT_64][1][idx] = ObDecIntSumOpFunc<int256_t, int64_t>::type; \
    op_funcs[DECIMAL_INT_64][2][idx] = ObDecIntSumOpFunc<ObNumber, int64_t>::type; \
    op_funcs[DECIMAL_INT_128][0][idx] = ObDecIntSumOpFunc<int256_t, int128_t>::type; \
    op_funcs[DECIMAL_INT_128][1][idx] = NULL; \
    op_funcs[DECIMAL_INT_128][2][idx] = ObDecIntSumOpFunc<ObNumber, int128_t>::type; \
    op_funcs[DECIMAL_INT_256][0][idx] = ObDecIntSumOpFunc<int256_t, int256_t>::type; \
    op_funcs[DECIMAL_INT_256][1][idx] = ObDecIntSumOpFunc<int512_t, int256_t>::type; \
    op_funcs[DECIMAL_INT_256][2][idx] = ObDecIntSumOpFunc<ObNumber, int256_t>::type; \
    op_funcs[DECIMAL_INT_512][0][idx] = ObDecIntSumOpFunc<int512_t, int512_t>::type; \
    op_funcs[DECIMAL_INT_512][1][idx] = NULL; \
    op_funcs[DECIMAL_INT_512][2][idx] = ObDecIntSumOpFunc<ObNumber, int512_t>::type; \
    merge_funcs[DECIMAL_INT_32][idx] = ObDecIntSumOpFunc<int32_t, int32_t>::type; \
    merge_funcs[DECIMAL_INT_64][idx] = ObDecIntSumOpFunc<int64_t, int64_t>::type; \
    merge_funcs[DECIMAL_INT_128][idx] = ObDecIntSumOpFunc<int128_t, int128_t>::type; \
    merge_funcs[DECIMAL_INT_256][idx] = ObDecIntSumOpFunc<int256_t, int256_t>::type; \
    merge_funcs[DECIMAL_INT_512][idx] = ObDecIntSumOpFunc<int512_t, int512_t>::type;

    INIT_OP_FUNCS(0, add);
    INIT_OP_FUNCS(1, sub);
    INIT_OP_FUNCS(2, store);
#undef INIT_OP_FUNCS

    // (from_type, to_type, need_cast, selector)
    auto &add_batch_funcs = ObAggregateProcessor::DEC_INT_ADD_BATCH_FUNCS;
    auto &merge_batch_funcs = ObAggregateProcessor::DEC_INT_MERGE_BATCH_FUNCS; // (merge_type, selector)

#define INIT_BATCH_OP_FUNCS(idx, selector) \
    add_batch_funcs[DECIMAL_INT_32][0][0][idx] = ObDecIntSumBatchOpFunc<int128_t, int32_t, int32_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_32][1][0][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_32][2][0][idx] = ObDecIntSumBatchOpFunc<ObNumber, int32_t, int32_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_64][0][0][idx] = ObDecIntSumBatchOpFunc<int128_t, int64_t, int64_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_64][1][0][idx] = ObDecIntSumBatchOpFunc<int256_t, int64_t, int64_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_64][2][0][idx] = ObDecIntSumBatchOpFunc<ObNumber, int64_t, int64_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_128][0][0][idx] = ObDecIntSumBatchOpFunc<int256_t, int128_t, int128_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_128][1][0][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_128][2][0][idx] = ObDecIntSumBatchOpFunc<ObNumber, int128_t, int128_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_256][0][0][idx] = ObDecIntSumBatchOpFunc<int256_t, int256_t, int256_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_256][1][0][idx] = ObDecIntSumBatchOpFunc<int512_t, int256_t, int256_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_256][2][0][idx] = ObDecIntSumBatchOpFunc<ObNumber, int256_t, int256_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_512][0][0][idx] = ObDecIntSumBatchOpFunc<int512_t, int512_t, int512_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_512][1][0][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_512][2][0][idx] = ObDecIntSumBatchOpFunc<ObNumber, int512_t, int512_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_32][0][1][idx] = ObDecIntSumBatchOpFunc<int128_t, int64_t, int32_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_32][1][1][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_32][2][1][idx] = ObDecIntSumBatchOpFunc<ObNumber, int64_t, int32_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_64][0][1][idx] = ObDecIntSumBatchOpFunc<int128_t, int128_t, int64_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_64][1][1][idx] = ObDecIntSumBatchOpFunc<int256_t, int128_t, int64_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_64][2][1][idx] = ObDecIntSumBatchOpFunc<ObNumber, int128_t, int64_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_128][0][1][idx] = ObDecIntSumBatchOpFunc<int256_t, int256_t, int128_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_128][1][1][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_128][2][1][idx] = ObDecIntSumBatchOpFunc<ObNumber, int256_t, int128_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_256][0][1][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_256][1][1][idx] = ObDecIntSumBatchOpFunc<int512_t, int512_t, int256_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_256][2][1][idx] = ObDecIntSumBatchOpFunc<ObNumber, int512_t, int256_t, selector>::add_batch; \
    add_batch_funcs[DECIMAL_INT_512][0][1][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_512][1][1][idx] = NULL; \
    add_batch_funcs[DECIMAL_INT_512][2][1][idx] = NULL; \
    merge_batch_funcs[DECIMAL_INT_32][idx] = ObDecIntSumBatchOpFunc<int32_t, int32_t, int32_t, selector>::add_batch; \
    merge_batch_funcs[DECIMAL_INT_64][idx] = ObDecIntSumBatchOpFunc<int64_t, int64_t, int64_t, selector>::add_batch; \
    merge_batch_funcs[DECIMAL_INT_128][idx] = ObDecIntSumBatchOpFunc<int128_t, int128_t, int128_t, selector>::add_batch; \
    merge_batch_funcs[DECIMAL_INT_256][idx] = ObDecIntSumBatchOpFunc<int256_t, int256_t, int256_t, selector>::add_batch; \
    merge_batch_funcs[DECIMAL_INT_512][idx] = ObDecIntSumBatchOpFunc<int512_t, int512_t, int512_t, selector>::add_batch;

  INIT_BATCH_OP_FUNCS(0, ObAggregateProcessor::ObSelector);
  INIT_BATCH_OP_FUNCS(1, ObAggregateProcessor::ObBatchRowsSlice);
#undef INIT_BATCH_OP_FUNCS
    return OB_SUCCESS;
  }
};

static int init_agg_func_ret = InitDecIntAggFunc::init();

} //namespace sql
} //namespace oceanbase
