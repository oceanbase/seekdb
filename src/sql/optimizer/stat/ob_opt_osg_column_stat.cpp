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

#define USING_LOG_PREFIX SQL_OPT

#include "ob_opt_osg_column_stat.h"
#include "common/lob/lob_types.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"
#include "share/datum/typed_datum_murmur.h"
#include "share/datum/ob_datum_funcs.h"
#include "sql/engine/aggregate/ob_aggregate_processor.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
namespace oceanbase {
namespace common {
using namespace sql;

namespace {
// OSG retains the existing physical value domain. Only selected reference
// bytes are owned; scalar values stay inline in the single native descriptor.
int copy_osg_extreme(ObMinMaxValEval &target, const Datum &source,
    const SqlTypeMeta &source_meta, const ObDatumCmpFuncType cmp_func,
    ObIAllocator &owner)
{
  Datum candidate;
  int ret = DatumParameterCodec::copy_native_value_span(source, source_meta, owner, candidate);
  if (OB_SUCC(ret)) {
    target.datum_ = candidate;
    if (!target.has_datum_) {
      target.meta_ = source_meta;
      target.cmp_func_ = cmp_func;
    }
    target.has_datum_ = true;
  }
  return ret;
}

int compare_osg_extreme(const ObMinMaxValEval &retained, const Datum &source,
    const SqlTypeMeta &source_meta, const ObDatumAccessContext *access_ctx, int &cmp)
{
  alignas(8) char retained_scratch[64], source_scratch[64];
  ObDatum left, right;
  int ret = DatumParameterCodec::export_native_value(retained.datum_, retained.meta_,
      left, retained_scratch, sizeof(retained_scratch));
  if (OB_SUCC(ret)) {
    ret = DatumParameterCodec::export_native_value(source, source_meta,
        right, source_scratch, sizeof(source_scratch));
  }
  if (OB_SUCC(ret)) { ret = retained.cmp_func_(left, right, cmp, access_ctx); }
  return ret;
}

// The historical replacement reuses this arena before its fallible copy.
// Detach first so an allocation error cannot expose a retired reference.
int replace_osg_extreme(ObMinMaxValEval &target, const Datum &source,
    const SqlTypeMeta &source_meta, const ObDatumCmpFuncType cmp_func,
    ObArenaAllocator &owner)
{
  target.datum_.set_null();
  owner.reuse();
  return copy_osg_extreme(target, source, source_meta, cmp_func, owner);
}

int hash_osg_value(const ObDatum &datum, const SqlTypeMeta &meta,
                   const ObDatumAccessContext *access_ctx, uint64_t &hash)
{
  int ret = OB_SUCCESS;
  DatumValue value;
  ret = value.assign_legacy_borrowed(datum, meta);
  bool outrow_lob = false;
  if (OB_SUCC(ret) && !datum.is_null() && meta.is_lob_storage() && meta.has_lob_header()) {
    ObLobLocatorV2 locator(datum.get_string(), true);
    // Invalid locators are treated as out-row by the old JSON/Geometry hash
    // path (it leaves the result at the seed); text hashing reports the read
    // error through Datum's native LOB hash function below.
    outrow_lob = !locator.is_valid(false) || !locator.has_inrow_data();
  }

  if (OB_FAIL(ret)) {
  } else if (outrow_lob && (meta.is_json() || meta.is_geometry())) {
    // The legacy v3 JSON and Geometry hash functions return the seed for an
    // out-row locator instead of reading its external payload.
    hash = 0;
  } else if (outrow_lob && meta.get_type_class() == ObTextTC) {
    // The legacy v3 text hash reads the full LOB before hashing it. The Datum
    // v2 callback has the same content semantics and accepts the active LOB
    // read context, avoiding materializing a legacy value object.
    ObDatumBasicFuncs *basic_funcs = ObDatumFuncs::get_basic_func(
        meta.get_type(), meta.get_collation_type(), meta.get_scale(), true);
    if (OB_ISNULL(basic_funcs) || OB_ISNULL(basic_funcs->murmur_hash_v2_)) {
      ret = OB_NOT_SUPPORTED;
    } else if (OB_FAIL(basic_funcs->murmur_hash_v2_(datum, 0, hash, access_ctx))) {
    }
  } else {
    ret = hash_datum_value_murmur(value, 0, hash);
  }
  return ret;
}
} // namespace

int ObMinMaxValEval::get_value(DatumValue &value) const
{
  if (!has_datum_) { value.set_null(); return OB_SUCCESS; }
  return value.assign_native_borrowed(datum_, meta_);
}

int ObMinMaxValEval::deep_copy(const ObMinMaxValEval &other, ObIAllocator &alloc)
{
  Datum candidate;
  int ret = DatumParameterCodec::copy_native_value_span(other.datum_, other.meta_, alloc, candidate);
  if (OB_SUCC(ret)) {
    datum_ = candidate;
    has_datum_ = other.has_datum_;
    meta_ = other.meta_;
    cmp_func_ = other.cmp_func_;
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObOptOSGColumnStat, *col_stat_);

void ObOptOSGColumnStat::reset()
{
  if (col_stat_ != NULL) {
    col_stat_->reset();
    col_stat_ = NULL;
  }
  min_val_.reset();
  max_val_.reset();
  inner_min_allocator_.reset();
  inner_max_allocator_.reset();
}

ObOptOSGColumnStat* ObOptOSGColumnStat::create_new_osg_col_stat(
    common::ObIAllocator &allocator,
    const ObDatumAccessContext *datum_access_ctx)
{
  ObOptOSGColumnStat *new_osg_col_stat = OB_NEWx(ObOptOSGColumnStat, (&allocator), allocator);
  ObOptColumnStat *new_col_stat = ObOptColumnStat::malloc_new_column_stat(allocator);
  if (OB_NOT_NULL(new_osg_col_stat) && OB_NOT_NULL(new_col_stat)) {
    new_osg_col_stat->col_stat_ = new_col_stat;
    new_osg_col_stat->datum_access_ctx_ = datum_access_ctx;
  } else {
    if (new_osg_col_stat != NULL) {
      new_osg_col_stat->~ObOptOSGColumnStat();
      allocator.free(new_osg_col_stat);
      new_osg_col_stat = NULL;
    }
    if (new_col_stat != NULL) {
      new_col_stat->~ObOptColumnStat();
      allocator.free(new_col_stat);
      new_col_stat = NULL;
    }
  }
  return new_osg_col_stat;
}

int ObOptOSGColumnStat::deep_copy(const ObOptOSGColumnStat &other)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(col_stat_->deep_copy(*other.col_stat_))) {
  } else if (other.min_val_.is_valid() && OB_FAIL(min_val_.deep_copy(other.min_val_, allocator_))) {
    LOG_WARN("failed to deep copy min val");
  } else if (other.max_val_.is_valid() && OB_FAIL(max_val_.deep_copy(other.max_val_, allocator_))) {
    LOG_WARN("failed to deep copy max val");
  }
  return ret;
}

int ObOptOSGColumnStat::publish_min_max_values()
{
  int ret = OB_SUCCESS, cmp = 0;
  DatumValue minimum, maximum;
  if (OB_ISNULL(col_stat_)) { ret = OB_ERR_UNEXPECTED; }
  else if (OB_FAIL(min_val_.get_value(minimum))) {}
  else if (OB_FAIL(max_val_.get_value(maximum))) {}
  else if (OB_FAIL(ObDbmsStatsUtils::truncate_string_for_opt_stats(minimum, allocator_, datum_access_ctx_))) {}
  else if (OB_FAIL(ObDbmsStatsUtils::truncate_string_for_opt_stats(maximum, allocator_, datum_access_ctx_))) {}
  else {
    if (col_stat_->get_min_datum().is_null()) { ret = col_stat_->set_min_value(minimum); }
    else if (!minimum.is_null()) {
      alignas(8) char min_scratch[64];
      DatumValue current_minimum;
      if (OB_FAIL(col_stat_->export_min_value(current_minimum, min_scratch, sizeof(min_scratch)))) {}
      else if (OB_FAIL(compare_datum_values(minimum, current_minimum, cmp))) {}
      else if (cmp < 0) { ret = col_stat_->set_min_value(minimum); }
    }
    if (OB_SUCC(ret)) {
      if (col_stat_->get_max_datum().is_null()) { ret = col_stat_->set_max_value(maximum); }
      else if (!maximum.is_null()) {
        alignas(8) char max_scratch[64];
        DatumValue current_maximum;
        if (OB_FAIL(col_stat_->export_max_value(current_maximum, max_scratch, sizeof(max_scratch)))) {}
        else if (OB_FAIL(compare_datum_values(maximum, current_maximum, cmp))) {}
        else if (cmp > 0) { ret = col_stat_->set_max_value(maximum); }
      }
    }
  }
  return ret;
}

int ObOptOSGColumnStat::merge_column_stat(const ObOptOSGColumnStat &other)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_stat_) || OB_ISNULL(other.col_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(col_stat_->get_table_id() != other.col_stat_->get_table_id() ||
                         col_stat_->get_partition_id() != other.col_stat_->get_partition_id() ||
                         col_stat_->get_column_id() != other.col_stat_->get_column_id())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("the key not match", K(col_stat_->get_table_id()),
                                  K(col_stat_->get_partition_id()),
                                  K(col_stat_->get_column_id()));
  } else if (other.min_val_.is_valid() &&
             OB_FAIL(inner_merge_min(other.min_val_.datum_,
                                     other.min_val_.meta_,
                                     other.min_val_.cmp_func_))) {
    LOG_WARN("failed to inner merge min val");
  } else if (other.max_val_.is_valid() &&
             OB_FAIL(inner_merge_max(other.max_val_.datum_,
                                     other.max_val_.meta_,
                                     other.max_val_.cmp_func_))) {
    LOG_WARN("failed to inner merge min val");
  } else {
    col_stat_->add_col_len(other.col_stat_->get_total_col_len());
    col_stat_->add_num_null(other.col_stat_->get_num_null());
    col_stat_->add_num_not_null(other.col_stat_->get_num_not_null());
    if (col_stat_->get_llc_bitmap_size() == other.col_stat_->get_llc_bitmap_size()) {
      ObGlobalNdvEval::update_llc(col_stat_->get_llc_bitmap(), other.col_stat_->get_llc_bitmap());
    }
  }
  return ret;
}

int ObOptOSGColumnStat::update_column_stat_info(const Datum *datum,
                                                const SqlType &source_type,
                                                const SqlTypeMeta &meta,
                                                const ObDatumCmpFuncType cmp_func)
{
  int ret = OB_SUCCESS;
  int64_t col_len = 0;
  alignas(8) char scratch[64];
  ObDatum physical;
  DatumValue value;
  if (OB_ISNULL(datum) || OB_ISNULL(col_stat_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(DatumCopyPlan(source_type).legacy_view(*datum, physical, scratch, sizeof(scratch)))) {
  } else if (physical.is_nop()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(value.assign_legacy_borrowed(physical, meta))) {
  } else if (OB_FAIL(calc_col_len(physical, meta, col_len))) {
  } else if (OB_FALSE_IT(col_stat_->add_col_len(col_len))) {
    // do nothing
  } else if (datum->is_null()) {
    col_stat_->add_num_null(1);
  } else {
    col_stat_->add_num_not_null(1);
    uint64_t hash_value = 0;
    if (OB_FAIL(hash_osg_value(physical, meta, datum_access_ctx_, hash_value))) {
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(col_stat_->get_llc_bitmap() == NULL
                                    || col_stat_->get_llc_bitmap_size() == 0)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_SUCC(ret) && OB_FAIL(ObAggregateProcessor::llc_add_value(
                   hash_value, col_stat_->get_llc_bitmap(), col_stat_->get_llc_bitmap_size()))) {
    } else if (OB_SUCC(ret) && OB_FAIL(inner_merge_min_max(value.datum_, meta, cmp_func))) {
    }
  }
  if (OB_SUCC(ret)) {
    col_stat_->calc_avg_len();
  }
  return ret;
}

int ObOptOSGColumnStat::calc_col_len(const ObDatum &datum, const SqlTypeMeta &meta, int64_t &col_len)
{
  int ret = OB_SUCCESS;
  if (!is_lob_storage(meta.get_type()) || datum.is_null()) {
    col_len = sizeof(datum) + (datum.is_null() ? 0 : datum.len_);
  } else {
    ObLobLocatorV2 locator(datum.get_string(), meta.has_lob_header());
    int64_t lob_data_byte_len = 0;
    if (OB_FAIL(locator.get_lob_data_byte_len(lob_data_byte_len))) {
    } else {
      col_len = sizeof(datum) + static_cast<int64_t>(lob_data_byte_len);
    }
  }
  return ret;
}

int ObOptOSGColumnStat::inner_merge_min(const Datum &datum, const SqlTypeMeta &meta,
                                        const ObDatumCmpFuncType cmp_func)
{
  int ret = OB_SUCCESS;
  if (meta.is_enum_or_set()) {
    // disable online gather enum/set max/min value.
  } else if (!min_val_.has_datum_) {
    ret = copy_osg_extreme(min_val_, datum, meta, cmp_func, inner_min_allocator_);
  } else if (min_val_.datum_.is_null()) {
    ret = replace_osg_extreme(min_val_, datum, meta, cmp_func, inner_min_allocator_);
  } else {
    int cmp_ret = 0;
    if (OB_FAIL(compare_osg_extreme(min_val_, datum, meta, datum_access_ctx_, cmp_ret))) {
    } else if (cmp_ret > 0) {
      ret = replace_osg_extreme(min_val_, datum, meta, cmp_func, inner_min_allocator_);
    }
  }
  return ret;
}

int ObOptOSGColumnStat::inner_merge_max(const Datum &datum, const SqlTypeMeta &meta,
                                        const ObDatumCmpFuncType cmp_func)
{
  int ret = OB_SUCCESS;
  if (meta.is_enum_or_set()) {
    // disable online gather enum/set max/min value.
  } else if (!max_val_.has_datum_) {
    ret = copy_osg_extreme(max_val_, datum, meta, cmp_func, inner_max_allocator_);
  } else if (max_val_.datum_.is_null()) {
    ret = replace_osg_extreme(max_val_, datum, meta, cmp_func, inner_max_allocator_);
  } else {
    int cmp_ret = 0;
    if (OB_FAIL(compare_osg_extreme(max_val_, datum, meta, datum_access_ctx_, cmp_ret))) {
    } else if (cmp_ret < 0) {
      ret = replace_osg_extreme(max_val_, datum, meta, cmp_func, inner_max_allocator_);
    }
  }
  return ret;
}

int ObOptOSGColumnStat::inner_merge_min_max(const Datum &datum, const SqlTypeMeta &meta,
                                            const ObDatumCmpFuncType cmp_func)
{
  int ret = OB_SUCCESS;
  bool cmp_min = true;
  bool cmp_max = true;
  int cmp_ret = 0;
  if (meta.is_enum_or_set()) {
    // disable online gather enum/set max/min value.
  } else {
    if (!max_val_.has_datum_) {
      if (OB_FAIL(copy_osg_extreme(max_val_, datum, meta, cmp_func, inner_max_allocator_))) {
      } else { cmp_max = false; }
    } else if (max_val_.datum_.is_null()) {
      if (OB_FAIL(replace_osg_extreme(max_val_, datum, meta, cmp_func, inner_max_allocator_))) {
      } else { cmp_max = false; }
    }

    if (OB_FAIL(ret)) {
    } else if (!min_val_.has_datum_) {
      if (OB_FAIL(copy_osg_extreme(min_val_, datum, meta, cmp_func, inner_min_allocator_))) {
      } else { cmp_min = false; }
    } else if (min_val_.datum_.is_null()) {
      if (OB_FAIL(replace_osg_extreme(min_val_, datum, meta, cmp_func, inner_min_allocator_))) {
      } else { cmp_min = false; }
    }

    if (OB_SUCC(ret) && cmp_max) {
      if (OB_FAIL(compare_osg_extreme(max_val_, datum, meta, datum_access_ctx_, cmp_ret))) {
      } else if (cmp_ret < 0) {
        if (OB_FAIL(replace_osg_extreme(max_val_, datum, meta, cmp_func, inner_max_allocator_))) {
        } else { cmp_min = false; }
      }
    }

    if (OB_SUCC(ret) && cmp_min) {
      if (OB_FAIL(compare_osg_extreme(min_val_, datum, meta, datum_access_ctx_, cmp_ret))) {
      } else if (cmp_ret > 0) {
        ret = replace_osg_extreme(min_val_, datum, meta, cmp_func, inner_min_allocator_);
      }
    }
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObOptOSGSampleHelper, sample_rate_, sample_value_, k_);

}
}
