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
#include "share/datum/datum_value_cast.h"
#include "share/datum/typed_datum_compare.h"
#include "ob_stat_item.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/optimizer/stat/ob_hybrid_hist_estimator.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
namespace oceanbase
{
using namespace sql;
namespace common
{

namespace {
int export_stat_item_value(const Datum &value, const SqlTypeMeta &meta, DatumValue &output,
                           void *scratch, int64_t capacity)
{
  ObDatum physical;
  int ret = DatumParameterCodec::export_schema_legacy_value(value, meta, physical, scratch, capacity);
  if (ret == OB_SUCCESS) { ret = output.assign_legacy_borrowed(physical, meta); }
  return ret;
}
int copy_stat_item_value(const Datum &value, const SqlTypeMeta &meta,
                         DatumValue &output, ObIAllocator &owner)
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret = export_stat_item_value(value, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? clone_typed_value(view, output, owner) : ret;
}
int clone_stat_item_value(const Datum &value, const SqlTypeMeta &meta, ObIAllocator &owner,
                          Datum &output, SqlTypeMeta &output_meta)
{
  Datum candidate;
  const int ret = DatumParameterCodec::clone_schema_native_value(value, meta, owner, candidate);
  if (ret == OB_SUCCESS) { output = candidate; output_meta = meta; }
  return ret;
}
int borrow_stat_item_value(const DatumValue &value, Datum &output, SqlTypeMeta &output_meta)
{
  Datum candidate;
  alignas(8) char scratch[64];
  ObDatum physical;
  int ret = value.export_legacy(physical, scratch, sizeof(scratch));
  if (ret == OB_SUCCESS) { ret = DatumParameterCodec::borrow_schema_native_value(physical, value.meta_, candidate); }
  if (ret == OB_SUCCESS) { output = candidate; output_meta = value.meta_; }
  return ret;
}
int borrow_stat_item_native(const Datum &value, const SqlTypeMeta &meta,
                            Datum &output, SqlTypeMeta &output_meta)
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret = export_stat_item_value(value, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? borrow_stat_item_value(view, output, output_meta) : ret;
}
} // namespace

int ObGlobalColumnStat::set_min_native(const Datum &value, const SqlTypeMeta &meta, ObIAllocator &owner)
{ return clone_stat_item_value(value, meta, owner, min_val_, min_meta_); }
int ObGlobalColumnStat::export_min_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_item_value(min_val_, min_meta_, output, scratch, capacity); }
int ObGlobalColumnStat::copy_min_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_item_value(min_val_, min_meta_, output, owner); }
int ObHistogramParam::set_min_borrowed(const DatumValue &value)
{ return borrow_stat_item_value(value, minval_, min_meta_); }
int ObHistogramParam::set_min_native_borrowed(const Datum &value, const SqlTypeMeta &meta)
{ return borrow_stat_item_native(value, meta, minval_, min_meta_); }
int ObHistogramParam::export_min_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_item_value(minval_, min_meta_, output, scratch, capacity); }

int ObGlobalColumnStat::set_max_native(const Datum &value, const SqlTypeMeta &meta, ObIAllocator &owner)
{ return clone_stat_item_value(value, meta, owner, max_val_, max_meta_); }
int ObGlobalColumnStat::export_max_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_item_value(max_val_, max_meta_, output, scratch, capacity); }
int ObGlobalColumnStat::copy_max_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_item_value(max_val_, max_meta_, output, owner); }
int ObHistogramParam::set_max_borrowed(const DatumValue &value)
{ return borrow_stat_item_value(value, maxval_, max_meta_); }
int ObHistogramParam::set_max_native_borrowed(const Datum &value, const SqlTypeMeta &meta)
{ return borrow_stat_item_native(value, meta, maxval_, max_meta_); }
int ObHistogramParam::export_max_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_item_value(maxval_, max_meta_, output, scratch, capacity); }

DEF_TO_STRING(ObGlobalColumnStat)
{
  int64_t pos = 0;
  alignas(8) char min_scratch[64], max_scratch[64];
  DatumValue minimum, maximum;
  const int min_ret = export_min_value(minimum, min_scratch, sizeof(min_scratch));
  const int max_ret = export_max_value(maximum, max_scratch, sizeof(max_scratch));
  J_OBJ_START();
  if (min_ret == OB_SUCCESS && max_ret == OB_SUCCESS) { J_KV("min_val_", minimum, "max_val_", maximum); }
  else { J_KV(K(min_ret), K(max_ret), K_(min_val), K_(min_meta), K_(max_val), K_(max_meta)); }
  J_COMMA();
  J_KV(K(null_val_), K(avglen_val_), K(ndv_val_));
  J_OBJ_END();
  return pos;
}
DEF_TO_STRING(ObHistogramParam)
{
  int64_t pos = 0;
  alignas(8) char min_scratch[64], max_scratch[64];
  DatumValue minimum, maximum;
  const int min_ret = export_min_value(minimum, min_scratch, sizeof(min_scratch));
  const int max_ret = export_max_value(maximum, max_scratch, sizeof(max_scratch));
  J_OBJ_START();
  J_KV(K(epc_));
  J_COMMA();
  if (min_ret == OB_SUCCESS && max_ret == OB_SUCCESS) { J_KV("minval_", minimum, "maxval_", maximum); }
  else { J_KV(K(min_ret), K(max_ret), K_(minval), K_(min_meta), K_(maxval), K_(max_meta)); }
  J_COMMA();
  J_KV(K(bkvals_), K(novals_), K(chvals_), K(eavals_), K(rpcnts_), K(eavs_));
  J_OBJ_END();
  return pos;
}

int ObStatRowCount::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(databuff_printf( buf, buf_len, pos, " COUNT(*)"))) {
  }
  return ret;
}

int ObStatRowCount::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  int64_t row_count = 0;
  if (OB_ISNULL(tab_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(cast_int(obj, row_count))) {
  } else {
    tab_stat_->set_row_count(row_count);
  }
  return ret;
}

int ObStatAvgRowLen::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  // dummy estimator
  if (OB_FAIL(databuff_printf(buf, buf_len, pos, " 1"))) {
  }
  return ret;
}

int ObStatAvgRowLen::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  int64_t avg_row_size = 0;
  UNUSED(obj);
  if (OB_ISNULL(tab_stat_) || OB_ISNULL(col_stats_)) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < col_stats_->count(); ++i) {
    if (OB_ISNULL(col_stats_->at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      avg_row_size += col_stats_->at(i)->get_avg_len();
    }
  }
  if (OB_SUCC(ret)) {
    tab_stat_->set_avg_row_size(avg_row_size);
  }
  return ret;
}

int ObStatColItem::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_param_) || OB_ISNULL(get_fmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, get_fmt(),
                                     col_param_->column_name_.length(),
                                     col_param_->column_name_.ptr()))) {
  }
  return ret;
}

int ObStatMaxValue::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                                     " MAX(`%.*s`)",
                                     col_param_->column_name_.length(),
                                     col_param_->column_name_.ptr()))) {
  }
  return ret;
}

int ObStatMaxValue::decode(DatumValue &obj,
                           ObIAllocator &allocator,
                           const ObDatumAccessContext *datum_access_ctx)
{
  // print cstring and hex string here
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObDbmsStatsUtils::truncate_string_for_opt_stats(
                 obj, allocator, datum_access_ctx))) {
  } else {
    ret = col_stat_->set_max_value(obj);
  }
  return ret;
}

int ObStatMinValue::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                                     " MIN(`%.*s`)",
                                     col_param_->column_name_.length(),
                                     col_param_->column_name_.ptr()))) {
  }
  return ret;
}

int ObStatMinValue::decode(DatumValue &obj,
                           ObIAllocator &allocator,
                           const ObDatumAccessContext *datum_access_ctx)
{
  // print cstring and hex string here
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObDbmsStatsUtils::truncate_string_for_opt_stats(
                 obj, allocator, datum_access_ctx))) {
  } else {
    ret = col_stat_->set_min_value(obj);
  }
  return ret;
}

int ObStatNumNull::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  int64_t num_not_null = 0;
  if (OB_ISNULL(col_stat_) || OB_ISNULL(tab_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(cast_int(obj, num_not_null))) {
  } else {
    col_stat_->set_num_not_null(num_not_null);
    col_stat_->set_num_null(tab_stat_->get_row_count() - num_not_null);
  }
  return ret;
}

int ObStatNumDistinct::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  int64_t num_distinct = 0;
  if (OB_ISNULL(col_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(cast_int(obj, num_distinct))) {
  } else {
    col_stat_->set_num_distinct(num_distinct);
  }
  return ret;
}

int ObStatAvgLen::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  int64_t avg_len = 0;
  if (OB_ISNULL(col_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(cast_int(obj, avg_len))) {
  } else {
    col_stat_->set_avg_len(avg_len);
  }
  return ret;
}

int ObStatLlcBitmap::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  ObString llc_bitmap_buf;
  if (OB_ISNULL(col_stat_) ||
      OB_ISNULL(col_stat_->get_llc_bitmap()) ||
      OB_UNLIKELY(!obj.is_varchar() && !obj.is_varbinary())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(obj.get_string(llc_bitmap_buf))) {
  } else if (OB_UNLIKELY(llc_bitmap_buf.length() > col_stat_->get_llc_bitmap_size())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    MEMCPY(col_stat_->get_llc_bitmap(), llc_bitmap_buf.ptr(), llc_bitmap_buf.length());
    col_stat_->set_llc_bitmap_size(llc_bitmap_buf.length());
  }
  return ret;
}

bool ObStatTopKHist::is_needed() const
{
  return NULL != col_param_ &&
         col_param_->need_basic_stat() &&
         col_param_->bucket_num_ > 1;
}

int ObStatTopKHist::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t bkt_num = col_param_->bucket_num_;
    if (bkt_num < MIN_BUCKET_SIZE) {
      bkt_num = MIN_BUCKET_SIZE;
    } else if (bkt_num > MAX_BUCKET_SIZE) {
      ret = OB_ERR_INVALID_SIZE_SPECIFIED;
    }
    double err_rate = 1.0 / get_window_size(bkt_num);
    if (OB_SUCC(ret)) {
      if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                                  " TOP_K_FRE_HIST(%lf, `%.*s`, %ld, %ld)",
                                  err_rate,
                                  col_param_->column_name_.length(),
                                  col_param_->column_name_.ptr(),
                                  col_param_->bucket_num_,
                                  max_disuse_cnt_))) {
      }
    }
  }
  return ret;
}


struct ObBucketCompare
{
  ObBucketCompare()
  {
    ret_ = OB_SUCCESS;
  }

  bool operator()(const ObHistBucket &l,
                  const ObHistBucket &r)
  {
    bool bret = false;
    int &ret = ret_;
    if (OB_FAIL(ret)) {
      // already fail
    } else {
      int cmp = 0;
      alignas(8) char left_scratch[64], right_scratch[64];
      DatumValue left, right;
      if (OB_FAIL(l.export_endpoint_value(left, left_scratch, sizeof(left_scratch)))) {
      } else if (OB_FAIL(r.export_endpoint_value(right, right_scratch, sizeof(right_scratch)))) {
      } else if (OB_FAIL(compare_datum_values(left, right, cmp))) {
      } else { bret = cmp < 0; }
    }
    return bret;
  }

  int ret_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBucketCompare);
};

class CopyableBucketComparer
{
public:
  CopyableBucketComparer(ObBucketCompare &compare) : compare_(compare) {}
  bool operator()(const ObHistBucket &l, const ObHistBucket &r)
  {
    return compare_(l, r);
  }
  ObBucketCompare &compare_;
};

int ObStatTopKHist::decode(DatumValue &obj,
                           ObIAllocator &allocator,
                           const ObDatumAccessContext *datum_access_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(datum_access_ctx);
  ObTopKFrequencyHistograms topk_hist;
  int64_t bucket_num = -1;
  if (OB_ISNULL(col_param_) || OB_ISNULL(tab_stat_) || OB_ISNULL(col_stat_) ||
      OB_UNLIKELY((bucket_num = col_param_->bucket_num_) <= 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(topk_hist.read_result(obj))) {
  } else if (OB_FAIL(build_histogram_from_topk_items(allocator,
                                                     topk_hist.get_buckets(),
                                                     col_param_->bucket_num_,
                                                     tab_stat_->get_row_count(),
                                                     col_stat_->get_num_not_null(),
                                                     col_stat_->get_num_distinct(),
                                                     col_stat_->get_histogram()))) {
  } else {/*do nothing*/}
  return ret;
}

int ObStatTopKHist::build_histogram_from_topk_items(ObIAllocator &allocator,
                                                    const ObIArray<ObTopkItem> &buckets,
                                                    int64_t max_bucket_num,
                                                    int64_t total_row_count,
                                                    int64_t not_null_count,
                                                    int64_t num_distinct,
                                                    ObHistogram &histogram)
{
  int ret = OB_SUCCESS;
  ObArray<ObHistBucket> tmp;
  histogram.reset();
  int64_t num_cnt = std::min(buckets.count(), max_bucket_num);
  for (int64_t i = 0; OB_SUCC(ret) && i < num_cnt; ++i) {
    ObHistBucket bkt(buckets.at(i).fre_times_, buckets.at(i).fre_times_);
    if (OB_FAIL(bkt.set_endpoint_borrowed(buckets.at(i).col_obj_))) {
    } else if (OB_FAIL(tmp.push_back(bkt))) {
    }
  }
  if (OB_SUCC(ret) && tmp.count() > 0) {
    ObBucketCompare cmp;
    lib::ob_sort(&tmp.at(0),
              &tmp.at(0) + tmp.count(),
              CopyableBucketComparer(cmp));
    if (OB_FAIL(cmp.ret_)) {
    }
  }
  for (int64_t i = 1; OB_SUCC(ret) && i < tmp.count(); ++i) {
    tmp.at(i).endpoint_num_ += tmp.at(i - 1).endpoint_num_;
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(try_build_topk_histogram(allocator,
                                         tmp,
                                         max_bucket_num,
                                         total_row_count,
                                         not_null_count,
                                         num_distinct,
                                         histogram))) {
    } else {
      LOG_TRACE("Succeed to build topk histogram", K(histogram), K(histogram.get_bucket_size()));
    }
  }
  return ret;
}

/**
 * @brief ObStatTopKHist::try_build_topk_histogram
 * @param bkts, an approximated topk histogram collected by a full table scan
 * @param max_bucket_num, the maximum bucket number specified by the user
 * @param total_row_count, the total row count estimated by a full table scan
 * @param num_distinct, the approximate number of distinct estimated by a full table scan
 * @param histogram, the result histogram built from bkts
 * @return
 */
int ObStatTopKHist::try_build_topk_histogram(ObIAllocator &allocator,
                                             const ObIArray<ObHistBucket> &bkts,
                                             const int64_t max_bucket_num,
                                             const int64_t total_row_count,
                                             const int64_t not_null_count,
                                             const int64_t num_distinct,
                                             ObHistogram &histogram)
{
  int ret = OB_SUCCESS;
  int64_t num = std::min(bkts.count(), max_bucket_num);
  if (OB_UNLIKELY(max_bucket_num <= 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (not_null_count == 0) {
    // all vals are null, there is no need to build a histogram
    histogram.set_type(ObHistType::INVALID_TYPE);
    histogram.set_sample_size(0);
    histogram.set_bucket_cnt(0);
    histogram.set_density(0);
  } else if (num > 0 && bkts.at(num - 1).endpoint_num_ == not_null_count) {
    histogram.set_type(ObHistType::FREQUENCY);
    histogram.set_sample_size(not_null_count);
    histogram.set_bucket_cnt(bkts.count());
    histogram.calc_density(ObHistType::FREQUENCY,
                           not_null_count,
                           not_null_count,
                           num_distinct,
                           bkts.count());
    if (OB_FAIL(histogram.prepare_allocate_buckets(allocator, bkts.count()))) {
    } else if (OB_FAIL(histogram.assign_buckets(bkts, allocator))) {
    } else {/*do nothing*/}
  } else if (num > 0 && bkts.at(num - 1).endpoint_num_ >=
             (not_null_count * (1 - 1.0 / max_bucket_num))) {
    histogram.set_type(ObHistType::TOP_FREQUENCY);
    histogram.set_sample_size(not_null_count);
    histogram.calc_density(ObHistType::TOP_FREQUENCY,
                           not_null_count,
                           bkts.at(num - 1).endpoint_num_,
                           num_distinct,
                           num);
    if (OB_FAIL(histogram.prepare_allocate_buckets(allocator, num))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < num; ++i) {
        ret = histogram.add_bucket(bkts.at(i), allocator);
      }
    }
  } else {
    histogram.set_type(ObHistType::HYBIRD);
    if (!bkts.empty() &&
        bkts.at(bkts.count() -1).endpoint_num_ >= not_null_count) {
      // if the topk histogram contains all records of the table
      // then we can build hybrid histogram directly from the topk result.
      histogram.set_sample_size(not_null_count);
      histogram.set_bucket_cnt(bkts.count());
      if (OB_FAIL(histogram.prepare_allocate_buckets(allocator, bkts.count()))) {
      } else if (OB_FAIL(histogram.assign_buckets(bkts, allocator))) {
      } else {/*do nothing*/}
    }
  }
  return ret;
}

int ObPartitionId::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (!calc_partition_id_str_.empty()) {
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%.*s",
                                calc_partition_id_str_.length(),
                                calc_partition_id_str_.ptr()))) {
    } else {/*do nothing*/}
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, "%ld", partition_id_))) {
  } else {/*do nothing*/}
  return ret;
}

int ObPartitionId::decode(DatumValue &obj)
{
  int ret = OB_SUCCESS;
  int64_t partition_id = -1;
  if (OB_ISNULL(tab_stat_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(cast_int(obj, partition_id))) {
  } else {
    tab_stat_->set_partition_id(partition_id);
  }
  return ret;
}

int ObStatItem::cast_int(const DatumValue &obj, int64_t &ret_value)
{
  int ret = OB_SUCCESS;
  DatumValue dest_obj;
  ObArenaAllocator calc_buffer(ObModIds::OB_BUFFER);
  ObCastCtx cast_ctx(&calc_buffer, NULL, CM_NONE, ObCharset::get_system_collation());
  if (OB_FAIL(DatumValueCaster::to_type(ObIntType, cast_ctx, obj, *cast_ctx.allocator_v2_, dest_obj))) {
  } else if (dest_obj.is_null()) {
    /*do nothing*/
  } else if (OB_FAIL(dest_obj.get_int(ret_value))) {
  }
  return ret;
}

void ObGlobalTableStat::add(int64_t rc, int64_t rs, int64_t ds, int64_t mac, int64_t mic)
{
  // skip empty partition
  if (rc > 0) {
    row_count_ += rc;
    row_size_ += rs;
    data_size_ += ds;
    macro_block_count_ += mac;
    micro_block_count_ += mic;
    part_cnt_ ++;
  }
}

int ObGlobalTableStat::add(int64_t rc, int64_t rs, int64_t ds, int64_t mac, int64_t mic,
                           int64_t scnt, int64_t mcnt)
{
  if (rc > 0) {
    row_count_ += rc;
    row_size_ += rs;
    data_size_ += ds;
    macro_block_count_ += mac;
    micro_block_count_ += mic;
    part_cnt_++;
    sstable_row_cnt_ += scnt;
    memtable_row_cnt_ += mcnt;
  }
  return OB_SUCCESS;
}

int64_t ObGlobalTableStat::get_row_count() const
{
  return row_count_;
}

int64_t ObGlobalTableStat::get_avg_row_size() const
{
  int64_t ret = row_count_ > 0 ? row_size_ / part_cnt_ : 0;
  return ret;
}

int64_t ObGlobalTableStat::get_avg_data_size() const
{
  int64_t ret = part_cnt_ > 0 ? data_size_ / part_cnt_ : 0;
  return ret;
}

int64_t ObGlobalTableStat::get_macro_block_count() const
{
  return macro_block_count_;
}

int64_t ObGlobalTableStat::get_micro_block_count() const
{
  return micro_block_count_;
}


void ObGlobalNdvEval::add(int64_t ndv, const char *llc_bitmap)
{
  if (llc_bitmap != NULL) {
    update_llc(global_llc_bitmap_, llc_bitmap, part_cnt_ == 0);
    ++ part_cnt_;
  }
  if (ndv > global_ndv_) {
    global_ndv_ = ndv;
  }
}

void ObGlobalNdvEval::update_llc(char *dst_llc_bitmap, const char *src_llc_bitmap, bool force_update)
{
  if (dst_llc_bitmap != NULL && src_llc_bitmap != NULL) {
    for (int64_t k = 0; k < ObOptColumnStat::NUM_LLC_BUCKET; ++k) {
      if (force_update ||
          static_cast<uint8_t>(src_llc_bitmap[k]) > static_cast<uint8_t>(dst_llc_bitmap[k])) {
        dst_llc_bitmap[k] = src_llc_bitmap[k];
      }
    }
  }
}

int64_t ObGlobalNdvEval::get() const
{
  int64_t num_distinct = 0;
  if (part_cnt_ <= 1) {
    num_distinct = global_ndv_;
  } else {
    num_distinct = get_ndv_from_llc(global_llc_bitmap_);
  }
  return num_distinct;
}

//splict the get function in to two, so get function should be used outside the NdvEval.
int64_t ObGlobalNdvEval::get_ndv_from_llc(const char *llc_bitmap)
{
  int64_t num_distinct = 0;
  if (OB_ISNULL(llc_bitmap)) {
    // ret is useless here, we just need to raise a warn to avoid core.
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "get unexpected null pointer");
  } else {
    double sum_of_pmax = 0;
    double alpha = select_alpha_value(ObOptColumnStat::NUM_LLC_BUCKET);
    int64_t empty_bucket_num = 0;
    for (int64_t i = 0; i < ObOptColumnStat::NUM_LLC_BUCKET; ++i) {
      sum_of_pmax += (1 / pow(2, (llc_bitmap[i])));
      if (llc_bitmap[i] == 0) {
        ++empty_bucket_num;
      }
    }
    double estimate_ndv = (alpha * ObOptColumnStat::NUM_LLC_BUCKET
                          * ObOptColumnStat::NUM_LLC_BUCKET)  / sum_of_pmax;
    num_distinct = static_cast<int64_t>(estimate_ndv);
    // check if estimate result too tiny or large.
    if (estimate_ndv <= 5 * ObOptColumnStat::NUM_LLC_BUCKET / 2) {
      if (0 != empty_bucket_num) {
        // use linear count
        num_distinct = static_cast<int64_t>(ObOptColumnStat::NUM_LLC_BUCKET
                                            * log(ObOptColumnStat::NUM_LLC_BUCKET / double(empty_bucket_num)));
      }
    }
    if (estimate_ndv > (static_cast<double>(ObOptColumnStat::LARGE_NDV_NUMBER) / 30)) {
      num_distinct = static_cast<int64_t>((0-pow(2, 32)) * log(1 - estimate_ndv / ObOptColumnStat::LARGE_NDV_NUMBER));
    }
  }

  return num_distinct;
}

double ObGlobalNdvEval::select_alpha_value(const int64_t num_bucket)
{
  double ret = 0.0;
  switch (num_bucket) {
  case 16:
    ret = 0.673;
    break;
  case 32:
    ret = 0.697;
    break;
  case 64:
    ret = 0.709;
    break;
  default:
    ret = 0.7213 / (1 + 1.079 / double(num_bucket));
    break;
  }
  return ret;
}

void ObGlobalNdvEval::get_llc_bitmap(char *llc_bitmap, const int64_t llc_bitmap_size) const
{
  if (llc_bitmap != NULL && llc_bitmap_size >= NUM_LLC_BUCKET) {
    MEMCPY(llc_bitmap, global_llc_bitmap_, NUM_LLC_BUCKET);
  }
}

int ObGlobalMaxEval::add(const DatumValue &value)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  Datum candidate;
  alignas(8) char scratch[64];
  ObDatum physical;
  if (OB_FAIL(value.export_legacy(physical, scratch, sizeof(scratch)))) {
  } else if (OB_FAIL(DatumParameterCodec::borrow_schema_native_value(physical, value.meta_, candidate))) {
  } else if (global_max_.is_null()) {
    global_max_ = candidate; meta_ = value.meta_;
  } else {
    alignas(8) char current_scratch[64];
    DatumValue current_view;
    if (OB_FAIL(export_value(current_view, current_scratch, sizeof(current_scratch)))) {
    } else if (OB_FAIL(compare_datum_values(value, current_view, cmp))) {
    } else if (cmp > 0) { global_max_ = candidate; meta_ = value.meta_; }
  }
  return ret;
}
int ObGlobalMaxEval::add_native(const Datum &value, const SqlTypeMeta &meta)
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret = export_stat_item_value(value, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? add(view) : ret;
}
int ObGlobalMaxEval::export_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_item_value(global_max_, meta_, output, scratch, capacity); }
int ObGlobalMaxEval::copy_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_item_value(global_max_, meta_, output, owner); }
int ObGlobalMaxEval::flush(ObIAllocator *owner)
{
  return owner == nullptr ? OB_INVALID_ARGUMENT : !is_valid() ? OB_SUCCESS
      : clone_stat_item_value(global_max_, meta_, *owner, global_max_, meta_);
}
int ObGlobalMinEval::add(const DatumValue &value)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  Datum candidate;
  alignas(8) char scratch[64];
  ObDatum physical;
  if (OB_FAIL(value.export_legacy(physical, scratch, sizeof(scratch)))) {
  } else if (OB_FAIL(DatumParameterCodec::borrow_schema_native_value(physical, value.meta_, candidate))) {
  } else if (global_min_.is_null()) {
    global_min_ = candidate; meta_ = value.meta_;
  } else {
    alignas(8) char current_scratch[64];
    DatumValue current_view;
    if (OB_FAIL(export_value(current_view, current_scratch, sizeof(current_scratch)))) {
    } else if (OB_FAIL(compare_datum_values(current_view, value, cmp))) {
    } else if (cmp > 0) { global_min_ = candidate; meta_ = value.meta_; }
  }
  return ret;
}
int ObGlobalMinEval::add_native(const Datum &value, const SqlTypeMeta &meta)
{
  alignas(8) char scratch[64];
  DatumValue view;
  const int ret = export_stat_item_value(value, meta, view, scratch, sizeof(scratch));
  return ret == OB_SUCCESS ? add(view) : ret;
}
int ObGlobalMinEval::export_value(DatumValue &output, void *scratch, int64_t capacity) const
{ return export_stat_item_value(global_min_, meta_, output, scratch, capacity); }
int ObGlobalMinEval::copy_value(DatumValue &output, ObIAllocator &owner) const
{ return copy_stat_item_value(global_min_, meta_, output, owner); }
int ObGlobalMinEval::flush(ObIAllocator *owner)
{
  return owner == nullptr ? OB_INVALID_ARGUMENT : !is_valid() ? OB_SUCCESS
      : clone_stat_item_value(global_min_, meta_, *owner, global_min_, meta_);
}

int ObStatHybridHist::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos,
                                     " HYBRID_HIST(`%.*s`, %ld)",
                                     col_param_->column_name_.length(),
                                     col_param_->column_name_.ptr(),
                                     col_param_->bucket_num_))) {
  }
  return ret;
}

int ObStatHybridHist::decode(DatumValue &obj,
                             ObIAllocator &allocator,
                             const ObDatumAccessContext *datum_access_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(datum_access_ctx);
  ObHybridHistograms hybrid_hist;
  ObArray<ObHistBucket> tmp;
  int64_t bucket_num = -1;
  if (OB_ISNULL(col_param_) || OB_ISNULL(col_stat_) ||
      OB_UNLIKELY((bucket_num = col_param_->bucket_num_) <= 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (obj.is_null()) {
    col_stat_->get_histogram().reset();
  } else if (OB_FAIL(hybrid_hist.read_result(obj))) {
  } else {
    col_stat_->get_histogram().get_buckets().reset();
    col_stat_->get_histogram().set_bucket_cnt(hybrid_hist.get_buckets().count());
    if (hybrid_hist.get_buckets().empty()) {
      //do nothing, maybe the sample data is all null
    } else if (OB_FAIL(col_stat_->get_histogram().prepare_allocate_buckets(allocator, hybrid_hist.get_buckets().count()))) {
    } else if (OB_FAIL(col_stat_->get_histogram().assign_buckets(hybrid_hist.get_buckets(), allocator))) {
    } else {
      col_stat_->get_histogram().set_type(ObHistType::HYBIRD);
      col_stat_->get_histogram().set_sample_size(hybrid_hist.get_total_count());
      col_stat_->get_histogram().set_pop_frequency(hybrid_hist.get_pop_freq());
      col_stat_->get_histogram().set_pop_count(hybrid_hist.get_pop_count());
      col_stat_->get_histogram().calc_density(ObHistType::HYBIRD,
                                              hybrid_hist.get_total_count(),
                                              hybrid_hist.get_pop_freq(),
                                              col_stat_->get_num_distinct(),
                                              hybrid_hist.get_pop_count());
      LOG_TRACE("succeed to build hybrid hist", K(hybrid_hist), K(col_stat_->get_histogram()));
    }
  }
  return ret;
}

int ObGlobalAllColEvals::flush(common::ObIAllocator *alloc)
{
  int ret = OB_SUCCESS;
  if(OB_FAIL(max_eval_.flush(alloc))) {
  } else if (OB_FAIL((min_eval_.flush(alloc)))) {
  }
  return ret;
}

int ObGlobalAllColEvals::merge(const ObOptColumnStat &col_stats)
{
  int ret = OB_SUCCESS;
  if (col_stats.get_last_analyzed() > 0) {
    ndv_eval_.add(col_stats.get_num_distinct(), col_stats.get_llc_bitmap());
    null_eval_.add(col_stats.get_num_null());
    avglen_eval_.add(col_stats.get_avg_len());
    // a partition has min/max values only when it contains a valid value in the other word, ndv is not zero
    if (col_stats.get_num_distinct() != 0) {
      if (OB_FAIL(min_eval_.add_native(col_stats.get_min_datum(), col_stats.get_min_meta()))) {
      } else if (OB_FAIL(max_eval_.add_native(col_stats.get_max_datum(), col_stats.get_max_meta()))) {
      }
    }
  } else {
    column_stat_valid_ = false;
  }
  return ret;
}
static const int32_t DEFAULT_DATA_TYPE_LEGNTH[] = 
{
  /*ObNullType        = 0*/  12,
  /*ObTinyIntType     = 1*/  20,
  /*ObSmallIntType    = 2*/  20,  
  /*ObMediumIntType   = 3*/  20,  
  /*ObInt32Type       = 4*/  20,  
  /*ObIntType         = 5*/  20,  

  /*ObUTinyIntType    = 6*/  20,  
  /*ObUSmallIntType   = 7*/  20,  
  /*ObUMediumIntType  = 8*/  20,  
  /*ObUInt32Type      = 9*/  20,  
  /*ObUInt64Type      = 10*/ 20, 

  /*ObFloatType       = 11*/ 16, 
  /*ObDoubleType      = 12*/ 20, 

  /*ObUFloatType      = 13*/ 16, 
  /*ObUDoubleType     = 14*/ 20, 

  /*ObNumberType      = 15*/ -1, 
  /*ObUNumberType     = 16*/ -1,

  /*ObDateTimeType    = 17*/ 20,
  /*ObTimestampType   = 18*/ 20,
  /*ObDateType        = 19*/ 16,
  /*ObTimeType        = 20*/ 20,
  /*ObYearType        = 21*/ 13,

  /*ObVarcharType     = 22*/ -1, 
  /*ObCharType        = 23*/ -1, 

  /*ObHexStringType   = 24*/ -1, 

  /*ObExtendType      = 25*/ -1, 
  /*ObUnknownType     = 26*/ -1, 

  /*ObTinyTextType    = 27*/ -1,
  /*ObTextType        = 28*/ -1,
  /*ObMediumTextType  = 29*/ -1,
  /*ObLongTextType    = 30*/ -1,

  /*ObBitType         = 31*/ 20,
  /*ObEnumType        = 32*/ 20,
  /*ObSetType         = 33*/ 20,
  /*ObEnumInnerType   = 34*/ -1,
  /*ObSetInnerType    = 35*/ -1,
  /*ObJsonType          = 36*/ -1, 
  /*ObGeometryType      = 37*/ -1, 

  /*ObUserDefinedSQLType = 38*/ -1, 
  /*ObDecimalIntType     = 39*/ -1, 
  /*ObCollectionSQLType  = 40*/ -1, 
  /*ObMySQLDateType      = 41*/ 16, 
  /*ObMySQLDateTimeType  = 42*/ 20, 
  /*ObMaxType                */ -1
};

int ObStatAvgLen::gen_expr(char *buf, const int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t type_count = sizeof(DEFAULT_DATA_TYPE_LEGNTH) / sizeof(int32_t);
  if (OB_ISNULL(col_param_) || OB_ISNULL(get_fmt())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (col_param_->column_type_ < type_count &&
             DEFAULT_DATA_TYPE_LEGNTH[col_param_->column_type_] > 0) {
    const char* fmt = " (%d * COUNT(`%.*s`))/(case when COUNT(*) = 0 then 1 else COUNT(*) end)";
    if (OB_FAIL(databuff_printf(buf, buf_len, pos, fmt, 
                                DEFAULT_DATA_TYPE_LEGNTH[col_param_->column_type_],
                                col_param_->column_name_.length(),
                                col_param_->column_name_.ptr()))) {
    }
  } else if (OB_FAIL(databuff_printf(buf, buf_len, pos, get_fmt(),
                                     col_param_->column_name_.length(),
                                     col_param_->column_name_.ptr()))) {
  }
  return ret;
}

} // end of common
} // end of oceanbase
