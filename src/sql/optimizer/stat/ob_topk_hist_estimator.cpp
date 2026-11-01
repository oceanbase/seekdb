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
#include "ob_topk_hist_estimator.h"
#include "lib/alloc/alloc_func.h"
#include "sql/executor/ob_execute_result.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"
#include "share/datum/typed_datum_murmur.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"

namespace oceanbase
{
namespace common
{

namespace {
bool same_topk_physical_meta(const SqlTypeMeta &left, const SqlTypeMeta &right)
{
  return left.get_type() == right.get_type()
      && left.get_cs_level() == right.get_cs_level()
      && left.get_cs_type() == right.get_cs_type()
      && left.get_scale() == right.get_scale();
}
}

struct Compare
{
  Compare(): ret_(OB_SUCCESS) {}

  bool operator()(const ObTopkItem &l, const ObTopkItem &r)
  {
    bool less = false;
    int &ret = ret_;
    if (OB_FAIL(ret)) {
      // already fail
    } else if (OB_UNLIKELY(l.col_obj_.is_null()) ||
               OB_UNLIKELY(r.col_obj_.is_null())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      less = l.fre_times_ > r.fre_times_;//Descending sort
    }
    return less;
  }

  int ret_;
private:
  DISALLOW_COPY_AND_ASSIGN(Compare);
};

struct CopyableComparer
{
  CopyableComparer(Compare &compare) : compare_(compare) {}
  bool operator()(const ObTopkItem &l, const ObTopkItem &r)
  {
    return compare_(l, r);
  }
  Compare &compare_;
};

//sort and reserve topk sort
int ObTopKFrequencyHistograms::create_topk_fre_items(const SqlTypeMeta *obj_meta/*default null*/)
{
  int ret = OB_SUCCESS;
  if (!is_need_merge_topk_hist()) {
    ObSEArray<ObTopkItem, 16> staged;
    if (!used_list_.empty() && (!has_datum_meta_ || obj_meta == nullptr
        || !same_topk_physical_meta(datum_meta_, *obj_meta))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(staged.reserve(used_list_.count()))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
      ObTopkDatumItem *item = static_cast<ObTopkDatumItem *>(used_list_.at(i));
      alignas(8) char scratch[64];
      ObDatum physical;
      DatumValue view;
      ObTopkItem output;
      if (item == nullptr) { ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(DatumParameterCodec::export_native_value(item->datum_, datum_meta_,
          physical, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(sql::ResultRowView::project_value(physical, datum_meta_,
          ObDatum::get_sql_value_datum_map_type(datum_meta_.get_type()), SqlTypeMeta(), view, true))) {
      } else if (OB_FAIL(clone_typed_value(view, output.col_obj_, topk_buf_))) {
      } else {
        output.fre_times_ = item->fre_times_;
        output.delta_ = item->delta_;
        ret = staged.push_back(output);
      }
    }
    if (OB_SUCC(ret) && !staged.empty()) { ret = sort_topk_fre_items(staged); }
    while (OB_SUCC(ret) && staged.count() > item_size_) { staged.pop_back(); }
    topk_fre_items_.set_allocator(&topk_buf_);
    if (OB_SUCC(ret)) { ret = topk_fre_items_.reserve(staged.count()); }
    if (OB_SUCC(ret)) {
      topk_fre_items_.clear();
      for (int64_t i = 0; OB_SUCC(ret) && i < staged.count(); ++i) {
        ret = topk_fre_items_.push_back(staged.at(i)); // Capacity is already reserved.
      }
    }
    return ret;
  }
  topk_fre_items_.set_allocator(&topk_buf_);
  if (OB_FAIL(topk_fre_items_.prepare_allocate(used_list_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
    if (is_need_merge_topk_hist()) {
      ObTopkItem *topk_item = NULL;
      if (OB_ISNULL(topk_item = static_cast<ObTopkItem *>(used_list_.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        topk_fre_items_.at(i) = *topk_item;
        ret = clone_typed_value(topk_item->col_obj_, topk_fre_items_.at(i).col_obj_, topk_buf_);
      }
    }
  }
  if (OB_SUCC(ret) && topk_fre_items_.count() > 0) {
    if (OB_FAIL(sort_topk_fre_items(topk_fre_items_))) {
    } else {
      while (topk_fre_items_.count() > item_size_) {
        topk_fre_items_.pop_back();
      }
    }
  } else {
  }
  return ret;
}

int ObTopKFrequencyHistograms::sort_topk_fre_items(ObIArray<ObTopkItem> &items)
{
  int ret = OB_SUCCESS;
  Compare cmp;
  lib::ob_sort(&items.at(0),
            &items.at(0)+ items.count(),
            CopyableComparer(cmp));
  if (OB_FAIL(cmp.ret_)) {
    ret = cmp.ret_;
  }
  return ret;
}

void ObTopKFrequencyHistograms::reset_memory_usage()
{
  used_list_.reset();
  free_list_.reset();
  topk_map_.destroy();
  topk_buf_.reset();
  obj_buf1_.reset();
  obj_buf2_.reset();
  datum_meta_.reset();
  has_datum_meta_ = false;
}

int ObTopKFrequencyHistograms::merge_distribute_top_k_fre_items(const DatumValue &obj)
{
  int ret = OB_SUCCESS;
  ObTopKFrequencyHistograms topk_hist;
  if (obj.is_null() || by_pass_) {
    //do nothing
  } else if (OB_UNLIKELY(!obj.get_meta().is_lob())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(window_size_ <= 0 ||
                         item_size_ <= 0 ||
                         get_max_reserved_item_size() >= window_size_ ||
                         !is_need_merge_topk_hist())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(topk_hist.read_result(obj))) {
  } else if (topk_hist.is_by_pass()) {
    N_ += topk_hist.get_total_fre_item();
    disuse_cnt_ += topk_hist.get_total_disuse_fre_item();
    if (is_satisfied_by_pass()) {
      by_pass_ = true;
      reset_memory_usage();
    }
  } else {
    if (OB_UNLIKELY(!topk_map_.created())) {
      int64_t hash_map_size = window_size_ + get_max_reserved_item_size();
      if (OB_FAIL(topk_map_.create(hash_map_size, "TopkMap", "TopkNode"))) {
      } else {
        set_the_obj_memory_use_limit();
      }
    }
    const ObIArray<ObTopkItem> &topk_fre_items = topk_hist.get_buckets();
    for (int64_t i = 0; OB_SUCC(ret) && i < topk_fre_items.count(); ++i) {
      const ObTopkItem &cur_item = topk_fre_items.at(i);
      ObTopkBaseItem *item = NULL;
      uint64_t obj_hash = 0;
      uint64_t seed = 0;
      if (OB_FAIL(hash_datum_value_murmur(topk_fre_items.at(i).col_obj_, seed, obj_hash))) {
      } else if (NULL != (item = get_entry(obj_hash))) {
        item->fre_times_ += cur_item.fre_times_;
        item->delta_ += cur_item.delta_;
      } else if (OB_FAIL(add_entry(obj_hash,
                                   cur_item.col_obj_,
                                   cur_item.fre_times_,
                                   cur_item.delta_))) {
      }
    }
    if (OB_SUCC(ret)) {
      int64_t bucket_num_old = bucket_num_;
      N_ += topk_hist.get_total_fre_item();
      disuse_cnt_ += topk_hist.get_total_disuse_fre_item();
      bucket_num_ = N_ / window_size_;
      if (bucket_num_ > bucket_num_old) {
        if (OB_FAIL(shrink_topk_items())) {
        }
      }
    }
  }
  return ret;
}

int ObTopKFrequencyHistograms::add_top_k_frequency_item(
    uint64_t datum_hash, const ObDatum &datum, const SqlTypeMeta &actual_meta)
{
  int ret = OB_SUCCESS;
  Datum borrowed;
  const int64_t reserved = get_max_reserved_item_size();
  if (window_size_ <= 0 || item_size_ <= 0 || reserved >= window_size_
      || is_need_merge_topk_hist() || N_ < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (reserved > INT64_MAX - window_size_ || window_size_ > INT64_MAX / 100000
      || N_ == INT64_MAX) {
    ret = OB_SIZE_OVERFLOW;
  } else if ((actual_meta.is_null() && !datum.is_null())
      || (has_datum_meta_ && !same_topk_physical_meta(datum_meta_, actual_meta))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(DatumParameterCodec::borrow_native_value(datum, actual_meta, borrowed))) {
  } else if (!datum.is_null()) {
    if (!by_pass_ && !topk_map_.created()) {
      if (OB_FAIL(topk_map_.create(window_size_ + reserved, "TopkMap", "TopkNode"))) {
      } else { set_the_obj_memory_use_limit(); }
    }
    if (OB_SUCC(ret)) { ret = add_datum_item(datum_hash, borrowed, actual_meta); }
  }
  return ret;
}

int ObTopKFrequencyHistograms::add_batch_items(
    const char **payloads, int32_t *lens, uint64_t *hash_val, int64_t batch_cnt,
    const SqlTypeMeta &actual_meta)
{
  int ret = OB_SUCCESS;
  const int64_t reserved = get_max_reserved_item_size();
  if (window_size_ <= 0 || item_size_ <= 0 || reserved >= window_size_
      || is_need_merge_topk_hist() || batch_cnt < 0 || N_ < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (reserved > INT64_MAX - window_size_ || window_size_ > INT64_MAX / 100000
      || batch_cnt > INT64_MAX - N_) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_ISNULL(payloads) || OB_ISNULL(lens) || OB_ISNULL(hash_val)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (has_datum_meta_ && !same_topk_physical_meta(datum_meta_, actual_meta)) {
    ret = OB_INVALID_ARGUMENT;
  }
  // Keep the historical successful-prefix policy; each cell validates before
  // its own publication, and a later error does not roll back earlier cells.
  if (OB_SUCC(ret) && !by_pass_ && !topk_map_.created()) {
    if (OB_FAIL(topk_map_.create(window_size_ + reserved, "TopkMap", "TopkNode"))) {
    } else { set_the_obj_memory_use_limit(); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < batch_cnt; ++i) {
    ObDatum physical; physical.ptr_ = payloads[i]; physical.pack_ = lens[i];
    Datum borrowed;
    if (actual_meta.is_null() && !physical.is_null()) { ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(DatumParameterCodec::borrow_native_value(physical, actual_meta, borrowed))) {
    } else { ret = add_datum_item(hash_val[i], borrowed, actual_meta); }
  }
  return ret;
}

int ObTopKFrequencyHistograms::add_datum_item(
    uint64_t hash, const Datum &datum, const SqlTypeMeta &meta)
{
  int ret = OB_SUCCESS;
  const int64_t previous_count = N_;
  const bool previous_has_meta = has_datum_meta_;
  const SqlTypeMeta previous_meta = datum_meta_;
  ObTopkBaseItem *previous = by_pass_ ? nullptr : get_entry(hash);
  const int64_t previous_frequency = previous == nullptr ? 0 : previous->fre_times_;
  if (N_ == INT64_MAX || (previous != nullptr && previous_frequency == INT64_MAX)) {
    ret = OB_SIZE_OVERFLOW;
  } else if (by_pass_) {
    ++N_; ++disuse_cnt_;
  } else {
    if (previous != nullptr) { ++previous->fre_times_; }
    else { ret = add_entry(hash, datum, meta, 1L, bucket_num_ - 1); }
    if (OB_SUCC(ret)) {
      ++N_;
      if (N_ % window_size_ == 0) { ret = shrink_topk_items(); }
      if (OB_FAIL(ret)) {
        // Datum-mode window shrinking stages all failing work before it drops
        // an item. Roll back this cell, not earlier successful batch cells.
        N_ = previous_count;
        if (previous != nullptr) { previous->fre_times_ = previous_frequency; }
        else {
          ObTopkBaseItem *inserted = get_entry(hash);
          if (used_list_.empty() || used_list_.at(used_list_.count() - 1) != inserted) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            const int erase_ret = topk_map_.erase_refactored(hash);
            if (erase_ret != OB_SUCCESS) { ret = erase_ret; }
            else {
              used_list_.pop_back();
              const int free_ret = free_list_.push_back(inserted); // Reserved by add_entry.
              if (free_ret != OB_SUCCESS) { ret = free_ret; }
            }
          }
          datum_meta_ = previous_meta;
          has_datum_meta_ = previous_has_meta;
        }
      }
    }
  }
  return ret;
}

int ObTopKFrequencyHistograms::add_top_k_frequency_item(const DatumValue &obj, int64_t repeat_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(window_size_ <= 0 ||
                  item_size_ <= 0 ||
                  get_max_reserved_item_size() >= window_size_ ||
                  !is_need_merge_topk_hist())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!obj.is_null()) {
    if (OB_UNLIKELY(!topk_map_.created())) {
      int64_t hash_map_size = window_size_ + get_max_reserved_item_size();
      if (OB_FAIL(topk_map_.create(hash_map_size, "TopkMap", "TopkNode"))) {
      } else {
        set_the_obj_memory_use_limit();
      }
    }
    if (OB_SUCC(ret)) {
      if (!by_pass_) {
        ObTopkBaseItem *item = NULL;
        uint64_t obj_hash = 0;
        uint64_t seed = 0;
        if (OB_FAIL(hash_datum_value_murmur(obj, seed, obj_hash))) {
        } else if (NULL != (item = get_entry(obj_hash))) {
          item->fre_times_ += repeat_cnt;
        } else if (OB_FAIL(add_entry(obj_hash, obj, repeat_cnt, bucket_num_ - 1))) {
        }
        if (OB_SUCC(ret)) {
          int64_t bucket_num_old = bucket_num_;
          N_ = N_ + repeat_cnt;
          bucket_num_ = N_ > window_size_ ? N_ / window_size_ : bucket_num_;
          if (bucket_num_ > bucket_num_old) {
            if (OB_FAIL(shrink_topk_items())) {
            }
          }
        }
      }
    }
  }
  return ret;
}

ObTopkBaseItem* ObTopKFrequencyHistograms::get_entry(const uint64_t hash_val)
{
  ObTopkBaseItem *ret = NULL;
  const uint64_t* ptr = NULL;
  if (NULL != (ptr = topk_map_.get(hash_val))) {
    ret = reinterpret_cast<ObTopkBaseItem*>(*ptr);
  }
  return ret;
}

int ObTopKFrequencyHistograms::add_entry(const uint64_t &datum_hash,
                                         const Datum &datum,
                                         const SqlTypeMeta &actual_meta,
                                         const int64_t fre_time,
                                         const int64_t delta)
{
  int ret = OB_SUCCESS;
  Datum owned;
  ObTopkDatumItem *item = nullptr;
  if (has_datum_meta_ && !same_topk_physical_meta(datum_meta_, actual_meta)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (used_list_.count() == INT64_MAX) { ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(used_list_.reserve(used_list_.count() + 1))) {
  } else if (free_list_.count() == INT64_MAX) { ret = OB_SIZE_OVERFLOW;
  } else if (OB_FAIL(free_list_.reserve(free_list_.count() + 1))) {
  } else if (OB_FAIL(DatumParameterCodec::copy_native_value_span(
      datum, actual_meta, get_allocator(), owned))) {
  } else if (OB_ISNULL(item = alloc_topk_datum_item())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    item->fre_times_ = fre_time;
    item->delta_ = delta;
    item->datum_hash_ = datum_hash;
    item->datum_ = owned;
    if (OB_FAIL(topk_map_.set_refactored(datum_hash, reinterpret_cast<uint64_t>(item)))) {
    } else if (OB_FAIL(used_list_.push_back(item))) {
      (void)topk_map_.erase_refactored(datum_hash);
    } else if (!has_datum_meta_) {
      datum_meta_ = actual_meta;
      has_datum_meta_ = true;
    }
    if (OB_FAIL(ret)) {
      // An item taken from the free list must remain reusable after failed
      // insertion. This slot was reserved before payload ownership changed.
      const int free_ret = free_list_.push_back(item);
      if (free_ret != OB_SUCCESS) { ret = free_ret; }
    }
  }
  return ret;
}

int ObTopKFrequencyHistograms::add_entry(const uint64_t &obj_hash,
                                         const DatumValue &obj,
                                         const int64_t fre_time,
                                         const int64_t delta)
{
  int ret = OB_SUCCESS;
  ObTopkItem *item = NULL;
  if (OB_ISNULL(item = alloc_topk_item())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    item->fre_times_ = fre_time;
    item->delta_ = delta;
    item->obj_hash_ = obj_hash;
    if (OB_FAIL(clone_typed_value(obj, item->col_obj_, get_allocator()))) {
    } else if (OB_FAIL(topk_map_.set_refactored(obj_hash, reinterpret_cast<uint64_t>(item)))) {
    } else if (OB_FAIL(used_list_.push_back(item))) {
    }
  }
  return ret;
}

ObTopkDatumItem* ObTopKFrequencyHistograms::alloc_topk_datum_item()
{
  ObTopkDatumItem *item = NULL;
  if (free_list_.empty()) {
    void *ptr = topk_buf_.alloc(sizeof(ObTopkDatumItem));
    if (NULL != ptr) {
      item = new (ptr) ObTopkDatumItem();
    }
  } else {
    item = static_cast<ObTopkDatumItem*>(free_list_.at(free_list_.count() - 1));
    free_list_.pop_back();
  }
  return item;
}

ObTopkItem* ObTopKFrequencyHistograms::alloc_topk_item()
{
  ObTopkItem *item = NULL;
  if (free_list_.empty()) {
    void *ptr = topk_buf_.alloc(sizeof(ObTopkItem));
    if (NULL != ptr) {
      item = new (ptr) ObTopkItem();
    }
  } else {
    item = static_cast<ObTopkItem*>(free_list_.at(free_list_.count() - 1));
    free_list_.pop_back();
  }
  return item;
}

int ObTopKFrequencyHistograms::remove_entry(ObTopkBaseItem *item)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(item)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (is_need_merge_topk_hist() &&
             OB_FAIL(topk_map_.erase_refactored(static_cast<ObTopkItem*>(item)->obj_hash_))) {
  } else if (!is_need_merge_topk_hist() &&
             OB_FAIL(topk_map_.erase_refactored(static_cast<ObTopkDatumItem*>(item)->datum_hash_))) {
  } else if (OB_FAIL(free_list_.push_back(item))) {
  }
  return ret;
}

/**
 * @brief ObTopKFrequencyHistograms::shrink_topk_fre_items
 *     shrinking phase; remove infrequent item
 * @return
 */
int ObTopKFrequencyHistograms::shrink_topk_items()
{
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  int64_t rm_cnt = 0;
  int64_t max_rm_cnt = used_list_.count() - get_max_reserved_item_size();
  if (!is_need_merge_topk_hist()) {
    if (obj_memory_limit_ < MIN_OBJ_MEMORY_LIMIT || obj_memory_limit_ > MAX_OBJ_MEMORY_LIMIT) {
      return OB_ERR_UNEXPECTED;
    }
    if (free_list_.count() > INT64_MAX - used_list_.count()) { return OB_SIZE_OVERFLOW; }
    if (OB_FAIL(free_list_.reserve(free_list_.count() + used_list_.count()))) { return ret; }
    for (int64_t i = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
      ObTopkDatumItem *item = static_cast<ObTopkDatumItem *>(used_list_.at(i));
      if (item == nullptr || item->fre_times_ < 0 || item->delta_ < 0
          || get_entry(item->datum_hash_) != item) { ret = OB_ERR_UNEXPECTED; }
    }
    if (OB_SUCC(ret) && get_allocator().used() > obj_memory_limit_) { ret = shrink_memory_usage(); }
    if (OB_FAIL(ret)) { return ret; }
    // After these checks, filtering uses existing map nodes and reserved list
    // capacity only. Failed compaction has not changed counters or entries.
  }
  for (int64_t i  = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
    ObTopkBaseItem *item = NULL;
    if (OB_ISNULL(item = used_list_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if ((!is_need_merge_topk_hist() && item->delta_ > INT64_MAX - item->fre_times_)
               || item->fre_times_ + item->delta_ > bucket_num_ || rm_cnt >= max_rm_cnt) {
      used_list_.at(pos++) = item;
    } else if (OB_FAIL(remove_entry(item))) {
    } else {
      ++ rm_cnt;
      disuse_cnt_ += item->fre_times_;
    }
  }
  if (OB_SUCC(ret)) {
    if (is_satisfied_by_pass()) {
      by_pass_ = true;
      reset_memory_usage();
    } else {
      while (used_list_.count() > pos) {
        used_list_.pop_back();
      }
      if (OB_UNLIKELY(obj_memory_limit_ < MIN_OBJ_MEMORY_LIMIT ||
                      obj_memory_limit_ > MAX_OBJ_MEMORY_LIMIT)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (is_need_merge_topk_hist() && OB_UNLIKELY(get_allocator().used() > obj_memory_limit_)) {
        if (OB_FAIL(shrink_memory_usage())) {
        }
      }
      bucket_num_ = N_ / window_size_ + 1;//next window size should add 1.
    }
  }
  return ret;
}

bool ObTopKFrequencyHistograms::is_satisfied_by_pass() const
{
  bool is_satisfied = false;
  if (max_disuse_cnt_ > 0) {
    is_satisfied = disuse_cnt_ >= max_disuse_cnt_;
  } else if (N_ % (window_size_ * 100) == 0) {
    if (1.0 * disuse_cnt_ / N_ > get_current_min_topk_ratio()) {
      is_satisfied = true;
      LOG_INFO("topk hist is statisfied by pass", K(disuse_cnt_), K(N_), K(get_current_min_topk_ratio()));
    }
  }
  return is_satisfied;
}

/* as following rule to set min topk ratio, Assume the total min topk ratio is: r
 * if N_ < window_size_ * 1000:
 *    min_ratio at least r/2;
 * if N_ < window_size_ * 10000:
 *    min_ratio at least r/2 + r/4
 * if N_ between window_size_ * 10000 and window_size_ * 100000:
 *    increase per window_size_ * 10000, the min_ratio increase: r/2 + r/4+...r/(2^x)
 * if N_ more than window_size_ * 100000, at least 100000000 rows:
 *    min_ratio set the r.
 *
*/
double ObTopKFrequencyHistograms::get_current_min_topk_ratio() const
{
  double min_topk_ratio = 1.0 - 1.0 / item_size_;
  if (N_ < window_size_ * 1000) {
    min_topk_ratio = min_topk_ratio / 2;
  } else if (N_ < window_size_ * 10000) {
    min_topk_ratio = min_topk_ratio / 2 + min_topk_ratio / 4;
  } else if (N_ >= window_size_ * 10000 && N_ < window_size_ * 100000) {
    int64_t n = N_ / (window_size_ * 10000) + 3;
    int64_t power = 2;
    double new_min_topk_ratio = 0.0;
    for (int i = 1; i < n; i++) {
      power *= i;
      new_min_topk_ratio += min_topk_ratio / power;
    }
    min_topk_ratio = new_min_topk_ratio;
  }
  return min_topk_ratio;
}

//allocte object total memory used restict[1MB ~ 10MB]
void ObTopKFrequencyHistograms::set_the_obj_memory_use_limit()
{
  // Keep the historical capacity when the logical memory budget uses the new scale.
  obj_memory_limit_ = lib::get_memory_budget() / 500;
  obj_memory_limit_ = std::max(obj_memory_limit_, MIN_OBJ_MEMORY_LIMIT);
  obj_memory_limit_ = std::min(obj_memory_limit_, MAX_OBJ_MEMORY_LIMIT);
}

/**
 * @brief ObTopKFrequencyHistograms::shrink_memory_usage
 * we use two buffer in turn.
 * the memory we used would be about: the size of object * window_size.
 * @return
 */
int ObTopKFrequencyHistograms::shrink_memory_usage()
{
  int ret = OB_SUCCESS;
  ObIAllocator &next_allocator = get_next_allocator();
  ObIAllocator &curr_allocator = get_allocator();
  if (!is_need_merge_topk_hist()) {
    ObSEArray<Datum, 16> staged;
    if (!used_list_.empty() && !has_datum_meta_) { ret = OB_ERR_UNEXPECTED; }
    if (OB_SUCC(ret)) { ret = staged.reserve(used_list_.count()); }
    for (int64_t i = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
      ObTopkDatumItem *item = static_cast<ObTopkDatumItem *>(used_list_.at(i));
      Datum owned;
      if (item == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(DatumParameterCodec::copy_native_value_span(
          item->datum_, datum_meta_, next_allocator, owned))) {
      } else { ret = staged.push_back(owned); }
    }
    if (OB_FAIL(ret)) {
      next_allocator.reset(); // This inactive arena has no published values.
    } else {
      // Hash keys and topk_buf_ item addresses did not change. Keep the map;
      // no allocation or callback remains after native payload publication.
      for (int64_t i = 0; i < used_list_.count(); ++i) {
        static_cast<ObTopkDatumItem *>(used_list_.at(i))->datum_ = staged.at(i);
      }
      curr_allocator.reset();
      swap_allocator();
    }
    return ret;
  }
  ObSEArray<DatumValue, 16> candidates;
  if (is_need_merge_topk_hist()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
      ObTopkItem *item = static_cast<ObTopkItem *>(used_list_.at(i));
      DatumValue owned;
      if (item == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(clone_typed_value(item->col_obj_, owned, next_allocator))) {}
      else if (OB_FAIL(candidates.push_back(owned))) {}
    }
    if (OB_FAIL(ret)) { return ret; }
  }
  topk_map_.reuse();
  LOG_TRACE("begine to shrink memory usage", K(obj_memory_limit_), K(curr_allocator.used()));
  for (int64_t i = 0; OB_SUCC(ret) && i < used_list_.count(); ++i) {
    if (OB_ISNULL(used_list_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (is_need_merge_topk_hist()) {
      ObTopkItem *topk_item = NULL;
      if (OB_ISNULL(topk_item = static_cast<ObTopkItem *>(used_list_.at(i)))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(topk_map_.set_refactored(topk_item->obj_hash_,
                                                  reinterpret_cast<uint64_t>(topk_item)))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (is_need_merge_topk_hist()) {
      for (int64_t i = 0; i < used_list_.count(); ++i) {
        static_cast<ObTopkItem *>(used_list_.at(i))->col_obj_ = candidates.at(i);
      }
    }
    curr_allocator.reset();
    swap_allocator();
  }
  return ret;
}

int ObTopKFrequencyHistograms::read_result(const DatumValue &result_value)
{
  int ret = OB_SUCCESS;
  ObString result;
  int64_t pos = 0;
  if (result_value.is_null()) {}
  else if (!result_value.get_meta().is_lob()) { ret = OB_ERR_UNEXPECTED; }
  else if (OB_FAIL(result_value.get_string(result))) {}
  else { ret = deserialize(result.ptr(), result.length(), pos); }
  return ret;
}

OB_DEF_SERIALIZE(ObTopKFrequencyHistograms)
{
  int ret = OB_SUCCESS;
  OB_UNIS_ENCODE(N_);
  int64_t items_count = topk_fre_items_.count();
  OB_UNIS_ENCODE(items_count);
  for (int64_t i = 0; OB_SUCC(ret) && i < items_count; ++i) {
    const ObTopkItem &top_k_fre_item = topk_fre_items_.at(i);
    if (OB_SUCC(ret)) { ret = DatumParameterCodec::encode_native_value(top_k_fre_item.col_obj_.datum_,
        top_k_fre_item.col_obj_.meta_, buf, buf_len, pos); }
    OB_UNIS_ENCODE(top_k_fre_item.fre_times_);
    OB_UNIS_ENCODE(top_k_fre_item.delta_);
  }
  OB_UNIS_ENCODE(by_pass_);
  OB_UNIS_ENCODE(disuse_cnt_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObTopKFrequencyHistograms)
{
  int64_t len = 0;
  OB_UNIS_ADD_LEN(N_);
  int64_t items_count = topk_fre_items_.count();
  OB_UNIS_ADD_LEN(items_count);
  for (int64_t i = 0; i < topk_fre_items_.count(); ++i) {
    const ObTopkItem &top_k_fre_item = topk_fre_items_.at(i);
    int64_t value_size = 0;
    if (DatumParameterCodec::native_value_size(top_k_fre_item.col_obj_.datum_, top_k_fre_item.col_obj_.meta_, value_size) != OB_SUCCESS) { return 0; }
    len += value_size;
    OB_UNIS_ADD_LEN(top_k_fre_item.fre_times_);
    OB_UNIS_ADD_LEN(top_k_fre_item.delta_);
  }
  OB_UNIS_ADD_LEN(by_pass_);
  OB_UNIS_ADD_LEN(disuse_cnt_);
  return len;
}

OB_DEF_DESERIALIZE(ObTopKFrequencyHistograms)
{
  int ret = OB_SUCCESS;
  int64_t items_count = 0;
  OB_UNIS_DECODE(N_);
  OB_UNIS_DECODE(items_count);
  topk_fre_items_.set_allocator(&topk_buf_);
  if (OB_FAIL(topk_fre_items_.init(items_count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < items_count; ++i) {
    ObTopkItem top_k_fre_item;
    if (OB_SUCC(ret) && pos < data_len) {
      ret = DatumParameterCodec::decode_unis_native_value(buf, data_len, pos, topk_buf_,
          top_k_fre_item.col_obj_.datum_, top_k_fre_item.col_obj_.meta_);
    }
    OB_UNIS_DECODE(top_k_fre_item.fre_times_);
    OB_UNIS_DECODE(top_k_fre_item.delta_);
    if (OB_SUCC(ret) && OB_FAIL(topk_fre_items_.push_back(top_k_fre_item))) {
    }
  }
  OB_UNIS_DECODE(by_pass_);
  OB_UNIS_DECODE(disuse_cnt_);
  return ret;
}

ObTopkHistEstimator::ObTopkHistEstimator(ObExecContext &ctx, ObIAllocator &allocator)
  : ObBasicStatsEstimator(ctx, allocator)
{}

int ObTopkHistEstimator::estimate(const ObOptStatGatherParam &param,
                                  ObOptStat &opt_stat)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("ObTopkHistEst", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObSqlString raw_sql;
  int64_t duration_time = -1;
  ObSEArray<ObOptStat, 1> tmp_opt_stats;
  if (OB_FAIL(add_topk_hist_stat_items(param.column_params_, opt_stat))) {
  } else if (get_item_size() <= 0) {
    //no need topk histogram item.
  } else if (OB_FAIL(fill_hints(allocator, 
                                param.tab_name_, 
                                param.gather_vectorize_, 
                                !param.sample_info_.is_specify_sample()))) {
  } else if (OB_FAIL(add_from_table(allocator, param.db_name_, param.tab_name_))) {
  } else if (OB_UNLIKELY(param.partition_infos_.count() > 1)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!param.partition_infos_.empty() &&
             OB_FAIL(fill_partition_info(allocator, param, param.partition_infos_.at(0)))) {
  } else if (OB_FAIL(fill_parallel_info(allocator, param.degree_))) {
  } else if (OB_FAIL(ObDbmsStatsUtils::get_valid_duration_time(param.gather_start_time_,
                                                               param.max_duration_time_,
                                                               duration_time))) {
  } else if (OB_FAIL(fill_query_timeout_info(allocator, duration_time))) {
  } else if (OB_FAIL(fill_sample_info(allocator, param.sample_info_))) {
  } else if (OB_FAIL(fill_specify_scn_info(allocator, param.sepcify_scn_))) {
  } else if (OB_FAIL(pack(raw_sql))) {
  } else if (OB_FAIL(tmp_opt_stats.push_back(opt_stat))) {
  } else if (OB_FAIL(do_estimate(param, raw_sql.string(), false,
                                 opt_stat, tmp_opt_stats))) {
  } else {
  }
  return ret;
}

int ObTopkHistEstimator::add_topk_hist_stat_items(const ObIArray<ObColumnStatParam> &column_params,
                                                  ObOptStat &opt_stat)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(opt_stat.table_stat_) ||
      OB_UNLIKELY(opt_stat.column_stats_.count() != column_params.count())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < column_params.count(); ++i) {
      const ObColumnStatParam *col_param = &column_params.at(i);
      if (OB_ISNULL(opt_stat.column_stats_.at(i)) ||
          OB_UNLIKELY(col_param->column_id_ != opt_stat.column_stats_.at(i)->get_column_id())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (opt_stat.column_stats_.at(i)->get_histogram().get_type() != ObHistType::TOP_FREQUENCY) {
        //do nothing
      } else {
        int64_t max_disuse_cnt = std::ceil(opt_stat.column_stats_.at(i)->get_num_not_null() * 1.0 / col_param->bucket_num_);
        if (OB_FAIL(add_stat_item(ObStatTopKHist(col_param,
                                                 opt_stat.table_stat_,
                                                 opt_stat.column_stats_.at(i),
                                                 max_disuse_cnt)))) {
        } else {/*do noting*/}
      }
    }
  }
  return ret;
}

}
}
