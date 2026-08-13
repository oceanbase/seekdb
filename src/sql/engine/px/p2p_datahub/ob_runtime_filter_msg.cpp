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
#include "sql/engine/px/p2p_datahub/ob_runtime_filter_msg.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_mgr.h"
#include "sql/engine/expr/ob_expr_hash.h"
#include "sql/engine/basic/ob_pushdown_filter.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;

OB_SERIALIZE_MEMBER(ObRFRangeFilterMsg::MinMaxCellSize, min_datum_buf_size_, max_datum_buf_size_);

OB_DEF_SERIALIZE(ObRFBloomFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObRFBloomFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ENCODE,
              bloom_filter_,
              use_hash_join_seed_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRFBloomFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((ObRFBloomFilterMsg, ObP2PDatahubMsgBase));
  
  bloom_filter_.allocator_.set_label("ObPxBFDESER");

  LST_DO_CODE(OB_UNIS_DECODE,
              bloom_filter_,
              use_hash_join_seed_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRFBloomFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObRFBloomFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              bloom_filter_,
              use_hash_join_seed_);
  return len;
}

OB_DEF_SERIALIZE(ObRFRangeFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObRFRangeFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ENCODE,
              lower_bounds_,
              upper_bounds_,
              need_null_cmp_flags_,
              cells_size_,
              cmp_funcs_,
              query_range_info_,
              build_obj_metas_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRFRangeFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_DESER((ObRFRangeFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_DECODE,
              lower_bounds_,
              upper_bounds_,
              need_null_cmp_flags_,
              cells_size_,
              cmp_funcs_,
              query_range_info_,
              build_obj_metas_);
  if (OB_FAIL(adjust_cell_size())) {
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRFRangeFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObRFRangeFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              lower_bounds_,
              upper_bounds_,
              need_null_cmp_flags_,
              cells_size_,
              cmp_funcs_,
              query_range_info_,
              build_obj_metas_);
  return len;
}

OB_DEF_SERIALIZE(ObRFInFilterMsg)
{
  int ret = OB_SUCCESS;
  BASE_SER((ObRFInFilterMsg, ObP2PDatahubMsgBase));
  int cnt = is_active_? serial_rows_.count() : 0;
  OB_UNIS_ENCODE(cnt);
  OB_UNIS_ENCODE(cmp_funcs_);
  OB_UNIS_ENCODE(hash_funcs_for_insert_);
  OB_UNIS_ENCODE(col_cnt_);
  OB_UNIS_ENCODE(max_in_num_);
  OB_UNIS_ENCODE(need_null_cmp_flags_);
  if (is_active_) {
    for (int i = 0; OB_SUCC(ret) && i < serial_rows_.count(); ++i) {
      if (OB_FAIL(serial_rows_.at(i)->serialize(buf, buf_len, pos))) {
      }
    }
  }
  OB_UNIS_ENCODE(query_range_info_);
  OB_UNIS_ENCODE(build_obj_metas_);
  return ret;
}

OB_DEF_DESERIALIZE(ObRFInFilterMsg)
{
  int ret = OB_SUCCESS;
  int64_t row_cnt = 0;
  BASE_DESER((ObRFInFilterMsg, ObP2PDatahubMsgBase));
  OB_UNIS_DECODE(row_cnt);
  OB_UNIS_DECODE(cmp_funcs_);
  OB_UNIS_DECODE(hash_funcs_for_insert_);
  OB_UNIS_DECODE(col_cnt_);
  OB_UNIS_DECODE(max_in_num_);
  OB_UNIS_DECODE(need_null_cmp_flags_);
  if (OB_SUCC(ret) && is_active_) {
    ObFixedArray<ObDatum, ObIAllocator> *new_row = nullptr;
    void *array_ptr = nullptr;
    int64_t buckets_cnt = max(row_cnt, 1);
    if (OB_FAIL(serial_rows_.reserve(row_cnt))) {
    } else if (OB_FAIL(rows_set_.create(buckets_cnt * 2,
        "RFDEInFilter",
        "RFDEInFilter"))) {
    } else if (OB_FAIL(cur_row_.prepare_allocate(col_cnt_))) {
    }
    for (int i = 0; OB_SUCC(ret) && i < row_cnt; ++i) {
      new_row = nullptr;
      array_ptr = nullptr;
      if (OB_ISNULL(array_ptr = allocator_.alloc(sizeof(ObFixedArray<ObDatum, ObIAllocator>)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc memory", K(ret));
      } else {
        new_row = new(array_ptr) ObFixedArray<ObDatum, ObIAllocator>(allocator_);
        if (OB_FAIL(new_row->deserialize(buf, data_len, pos))) {
        } else if (OB_FAIL(serial_rows_.push_back(new_row))) {
        } else {
          ObRFInFilterNode node(
              &cmp_funcs_, &hash_funcs_for_insert_, new_row, datum_access_ctx_);
          if (OB_FAIL(rows_set_.set_refactored(node))) {
          }
        }
      }
    }
  }
  OB_UNIS_DECODE(query_range_info_);
  OB_UNIS_DECODE(build_obj_metas_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObRFInFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObRFInFilterMsg, ObP2PDatahubMsgBase));
  int cnt = is_active_? serial_rows_.count() : 0;
  OB_UNIS_ADD_LEN(cnt);
  OB_UNIS_ADD_LEN(cmp_funcs_);
  OB_UNIS_ADD_LEN(hash_funcs_for_insert_);
  OB_UNIS_ADD_LEN(col_cnt_);
  OB_UNIS_ADD_LEN(max_in_num_);
  OB_UNIS_ADD_LEN(need_null_cmp_flags_);
  if (is_active_) {
    for (int i = 0; i < serial_rows_.count(); ++i) {
      len += serial_rows_.at(i)->get_serialize_size();
    }
  }
  OB_UNIS_ADD_LEN(query_range_info_);
  OB_UNIS_ADD_LEN(build_obj_metas_);
  return len;
}


//ObRFBloomFilterMsg
int ObRFBloomFilterMsg::reuse()
{
  int ret = OB_SUCCESS;
  is_empty_ = true;
  bloom_filter_.reset_filter();
  is_active_ = true;
  return ret;
}

int ObRFBloomFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr)
{
  int ret = OB_SUCCESS;
  ObRFBloomFilterMsg *bf_msg = nullptr;
  ObMemAttr attr("PxBfMsg");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObRFBloomFilterMsg>(attr, bf_msg))) {
  } else if (OB_FAIL(bf_msg->assign(*this))) {
  } else {
    new_msg_ptr = bf_msg;
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(bf_msg)) {
    bf_msg->destroy();
    ob_free(bf_msg);
  }
  return ret;
}

int ObRFBloomFilterMsg::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  const ObRFBloomFilterMsg &other_msg = static_cast<const ObRFBloomFilterMsg &>(msg);
  use_hash_join_seed_ = other_msg.use_hash_join_seed_;
  if (OB_FAIL(ObP2PDatahubMsgBase::assign(msg))) {
  } else if (OB_FAIL(bloom_filter_.assign(other_msg.bloom_filter_))) {
  }
  return ret;
}

// the merge process of bloom_filter_ is atomic by using CAS
int ObRFBloomFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObRFBloomFilterMsg &bf_msg = static_cast<ObRFBloomFilterMsg &>(msg);
  if (bf_msg.is_empty_) {
  } else if (OB_FAIL(bloom_filter_.merge_filter(&bf_msg.bloom_filter_))) {
  } else {
    is_empty_ = false;
  }
  return ret;
}

int ObRFBloomFilterMsg::destroy()
{
  int ret = OB_SUCCESS;
  bloom_filter_.reset();
  allocator_.reset();
  return ret;
}

int ObRFBloomFilterMsg::might_contain(const ObExpr &expr,
    ObEvalCtx &ctx,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
    ObDatum &res)
{
  int ret = OB_SUCCESS;
  uint64_t hash_val = ObExprJoinFilter::JOIN_FILTER_SEED;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (use_hash_join_seed_) {
    // hash value explained in:
    //        hash join            simd block bloom filter
    //  10001111....1011010         10001111....1011010
    //  ||_______63_______|          |___32___||__32___|
    //  |     hash               _______|_______   |-->locate block
    //  |                        |    |    |    |
    // is match                  8bit 8bit 8bit 8bit -> split to 4 byte
    //                           |    |    |    |
    //                           6bit 6bit 6bit 6bit -> each low 6bit are used, high 2 bit is useless
    // the highest bit of hash values is not used in bloom filter
    hash_val = ObExprHash::HASH_SEED;
  }
  ObDatum *datum = nullptr;
  ObHashFunc hash_func;
  if (!is_active_) {
    res.set_int(1);
  } else if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else if (OB_FAIL(ctx.get_datum_access_ctx(access_ctx))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else {
        hash_func.hash_func_ = filter_ctx.hash_funcs_.at(i).hash_func_;
        if (OB_FAIL(hash_func.hash_func_(
                *datum, hash_val, hash_val, access_ctx))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      bool is_match = true;
      if (OB_FAIL(bloom_filter_.might_contain(hash_val, is_match))) {
      } else {
        if (!is_match) {
          filter_ctx.filter_count_++;
        }
        filter_ctx.check_count_++;
        res.set_int(is_match ? 1 : 0);
        filter_ctx.collect_sample_info(!is_match, 1);
      }
    }
  }
  return ret;
}

int ObRFBloomFilterMsg::might_contain_batch(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  bool is_match = true;
  uint64_t seed = ObExprJoinFilter::JOIN_FILTER_SEED;
  ObDatum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  uint64_t *hash_values = reinterpret_cast<uint64_t *>(
                                ctx.frames_[expr.frame_idx_] + expr.res_buf_off_);
  int64_t total_count = 0;
  int64_t filter_count = 0;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (OB_UNLIKELY(is_empty_)) {
    if (OB_FAIL(ObBitVector::flip_foreach(skip, batch_size,
        [&](int64_t idx) __attribute__((always_inline)) {
      results[idx].set_int(0);
      ++filter_count;
      ++total_count;
      return OB_SUCCESS;
    }))) {
    }
    if (OB_SUCC(ret)) {
      eval_flags.set_all(true);
      filter_ctx.filter_count_ += filter_count;
      filter_ctx.check_count_ += total_count;
      filter_ctx.total_count_ += total_count;
    }
  } else if (OB_FAIL(ctx.get_datum_access_ctx(access_ctx))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      ObExpr *e = expr.args_[i];
      if (OB_FAIL(e->eval_batch(ctx, skip, batch_size))) {
      } else {
        const bool is_batch_seed = (i > 0);
        ObBatchDatumHashFunc hash_func_batch = filter_ctx.hash_funcs_.at(i).batch_hash_func_;
        hash_func_batch(hash_values,
                        e->locate_batch_datums(ctx), e->is_batch_result(),
                        skip, batch_size,
                        is_batch_seed ? hash_values : &seed,
                        is_batch_seed,
                        access_ctx);
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObBitVector::flip_foreach(skip, batch_size,
          [&](int64_t idx) __attribute__((always_inline)) {
            bloom_filter_.prefetch_bits_block(hash_values[idx]); return OB_SUCCESS;
          }))) {
    } else if (OB_FAIL(ObBitVector::flip_foreach(skip, batch_size,
        [&](int64_t idx) __attribute__((always_inline)) {
          int tmp_ret = bloom_filter_.might_contain(hash_values[idx], is_match);
          if (OB_SUCCESS == tmp_ret) {
            filter_count += !is_match;
            ++total_count;
            results[idx].set_int(is_match);
          }
          return tmp_ret;
        }))) {
    } else {
      eval_flags.set_all(true);
      filter_ctx.filter_count_ += filter_count;
      filter_ctx.check_count_ += total_count;
      filter_ctx.total_count_ += total_count;
      filter_ctx.collect_sample_info(filter_count, total_count);
    }
  }
  return ret;
}

int ObRFBloomFilterMsg::insert_by_row_batch(
  const ObBatchRows *child_brs,
  const common::ObIArray<ObExpr *> &expr_array,
  const common::ObHashFuncs &hash_funcs,
  const ObExpr *calc_tablet_id_expr,
  ObEvalCtx &eval_ctx,
  uint64_t *batch_hash_values)
{
  int ret = OB_SUCCESS;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (child_brs->size_ > 0) {
    uint64_t seed = ObExprJoinFilter::JOIN_FILTER_SEED;
    if (OB_FAIL(eval_ctx.get_datum_access_ctx(access_ctx))) {
    } else if (OB_NOT_NULL(calc_tablet_id_expr)) {
      if (OB_ISNULL(calc_tablet_id_expr) || hash_funcs.count() != 1) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected part id expr", K(ret));
      } else if (OB_FAIL(calc_tablet_id_expr->eval_batch(eval_ctx,
        *(child_brs->skip_), child_brs->size_))) {
      } else {
        ObBatchDatumHashFunc hash_func_batch = hash_funcs.at(0).batch_hash_func_;
        hash_func_batch(batch_hash_values,
                        calc_tablet_id_expr->locate_batch_datums(eval_ctx),
                        calc_tablet_id_expr->is_batch_result(),
                        *child_brs->skip_, child_brs->size_,
                        &seed,
                        false,
                        access_ctx);
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
        ObExpr *expr = expr_array.at(i); // expr ptr check in cg, not check here
        if (OB_FAIL(expr->eval_batch(eval_ctx, *(child_brs->skip_), child_brs->size_))) {
        } else {
          ObBatchDatumHashFunc hash_func_batch = hash_funcs.at(i).batch_hash_func_;
          const bool is_batch_seed = (i > 0);
          hash_func_batch(batch_hash_values,
                          expr->locate_batch_datums(eval_ctx), expr->is_batch_result(),
                          *child_brs->skip_, child_brs->size_,
                          is_batch_seed ? batch_hash_values : &seed,
                          is_batch_seed,
                          access_ctx);
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < child_brs->size_; ++i) {
      if (OB_NOT_NULL(calc_tablet_id_expr)) {
        ObDatum &datum = calc_tablet_id_expr->locate_expr_datum(eval_ctx, i);
        if (ObExprCalcPartitionId::NONE_PARTITION_ID == datum.get_int()) {
          continue;
        }
      }
      if (OB_SUCC(ret)) {
        if (child_brs->skip_->at(i)) {
          continue;
        } else if (OB_FAIL(bloom_filter_.put(batch_hash_values[i]))) {
        } else if (is_empty_) {
          is_empty_ = false;
        }
      }
    }
  }
  return ret;
}
int ObRFBloomFilterMsg::insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  uint64_t hash_value = 0;
  bool ignore = false;
  if (OB_FAIL(calc_hash_value(expr_array,
    hash_funcs, calc_tablet_id_expr,
    eval_ctx, hash_value, ignore))) {
  } else if (ignore) {
      /*do nothing*/
  } else if (OB_FAIL(bloom_filter_.put(hash_value))) {
  } else if (is_empty_) {
    is_empty_ = false;
  }
  return ret;
}

int ObRFBloomFilterMsg::calc_hash_value(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx,
    uint64_t &hash_value, bool &ignore)
{
  int ret = OB_SUCCESS;
  hash_value = ObExprJoinFilter::JOIN_FILTER_SEED;
  ignore = false;
  ObDatum *datum = nullptr;
  const ObDatumAccessContext *access_ctx = nullptr;
  if (OB_FAIL(eval_ctx.get_datum_access_ctx(access_ctx))) {
  } else if (OB_NOT_NULL(calc_tablet_id_expr)) {
    int64_t partition_id = 0;
    if (hash_funcs.count() != 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected part id expr", K(ret));
    } else if (OB_FAIL(calc_tablet_id_expr->eval(eval_ctx, datum))) {
    } else if (ObExprCalcPartitionId::NONE_PARTITION_ID == (partition_id = datum->get_int())) {
      ignore = true;
    } else if (OB_FAIL(
                   hash_funcs.at(0).hash_func_(*datum, hash_value, hash_value, access_ctx))) {
    }
  } else {
    for (int64_t idx = 0; OB_SUCC(ret) && idx < expr_array.count() ; ++idx) {
      if (OB_FAIL(expr_array.at(idx)->eval(eval_ctx, datum))) {
      } else if (OB_FAIL(
                     hash_funcs.at(idx).hash_func_(*datum, hash_value, hash_value, access_ctx))) {
      }
    }
  }
  return ret;
}

//end ObRFBloomFilterMsg

//ObRFRangeFilterMsg
ObRFRangeFilterMsg::ObRFRangeFilterMsg()
: ObP2PDatahubMsgBase(), lower_bounds_(allocator_), upper_bounds_(allocator_),
  need_null_cmp_flags_(allocator_), cells_size_(allocator_),
  cmp_funcs_(allocator_), query_range_info_(allocator_),
  query_range_(), is_query_range_ready_(false), query_range_allocator_(),
  datum_access_ctx_(nullptr),
  build_obj_metas_(allocator_)
{
}

int ObRFRangeFilterMsg::reuse()
{
  int ret = OB_SUCCESS;
  is_empty_ = true;
  lower_bounds_.reset();
  upper_bounds_.reset();
  cells_size_.reset();
  if (OB_FAIL(lower_bounds_.prepare_allocate(cmp_funcs_.count()))) {
  } else if (OB_FAIL(upper_bounds_.prepare_allocate(cmp_funcs_.count()))) {
  } else if (OB_FAIL(cells_size_.prepare_allocate(cmp_funcs_.count()))) {
  }
  (void)reuse_query_range();
  return ret;
}

int ObRFRangeFilterMsg::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  const ObRFRangeFilterMsg &other_msg = static_cast<const ObRFRangeFilterMsg &>(msg);
  if (OB_FAIL(ObP2PDatahubMsgBase::assign(msg))) {
  } else if (OB_FAIL(lower_bounds_.assign(other_msg.lower_bounds_))) {
  } else if (OB_FAIL(upper_bounds_.assign(other_msg.upper_bounds_))) {
  } else if (OB_FAIL(cmp_funcs_.assign(other_msg.cmp_funcs_))) {
  } else if (OB_FAIL(build_obj_metas_.assign(other_msg.build_obj_metas_))) {
  } else if (OB_FAIL(need_null_cmp_flags_.assign(other_msg.need_null_cmp_flags_))) {
  } else if (OB_FAIL(cells_size_.assign(other_msg.cells_size_))) {
  } else if (OB_FAIL(adjust_cell_size())) {
  } else if (OB_FAIL(query_range_info_.assign(other_msg.query_range_info_))) {
  } else {
    datum_access_ctx_ = other_msg.datum_access_ctx_;
  }
  return ret;
}

int ObRFRangeFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr)
{
  int ret = OB_SUCCESS;
  ObRFRangeFilterMsg *rf_msg = nullptr;
  ObMemAttr attr("PxRangeMsg");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObRFRangeFilterMsg>(attr, rf_msg))) {
  } else if (OB_FAIL(rf_msg->assign(*this))) {
  } else {
    for (int i = 0; i < rf_msg->lower_bounds_.count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(rf_msg->lower_bounds_.at(i).deep_copy(lower_bounds_.at(i),
          rf_msg->get_allocator()))) {
      } else if (OB_FAIL(rf_msg->upper_bounds_.at(i).deep_copy(upper_bounds_.at(i),
          rf_msg->get_allocator()))) {
      }
    }
    if (OB_SUCC(ret)) {
      new_msg_ptr = rf_msg;
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(rf_msg)) {
    rf_msg->destroy();
    ob_free(rf_msg);
  }
  return ret;
}

int ObRFRangeFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObRFRangeFilterMsg &range_msg = static_cast<ObRFRangeFilterMsg &>(msg);
  CK(range_msg.lower_bounds_.count() == lower_bounds_.count() &&
     range_msg.upper_bounds_.count() == upper_bounds_.count());
  if (OB_FAIL(ret)) {
  } else if (range_msg.is_empty_) {
    /*do nothing*/
  } else {
    ObSpinLockGuard guard(lock_);
    if (OB_ISNULL(datum_access_ctx_)) {
      datum_access_ctx_ = range_msg.datum_access_ctx_;
    }
    if (OB_FAIL(get_min(range_msg.lower_bounds_, datum_access_ctx_))) {
    } else if (OB_FAIL(get_max(range_msg.upper_bounds_, datum_access_ctx_))) {
    } else if (is_empty_) {
      is_empty_ = false;
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_min(
    ObIArray<ObDatum> &vals,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  for (int i = 0; i < vals.count() && OB_SUCC(ret); ++i) {
    // null value is also suitable
    if (OB_FAIL(get_min(cmp_funcs_.at(i), lower_bounds_.at(i),
        vals.at(i), cells_size_.at(i).min_datum_buf_size_, access_ctx))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_max(
    ObIArray<ObDatum> &vals,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  for (int i = 0; i < vals.count() && OB_SUCC(ret); ++i) {
    // null value is also suitable
    if (OB_FAIL(get_max(cmp_funcs_.at(i), upper_bounds_.at(i),
        vals.at(i), cells_size_.at(i).max_datum_buf_size_, access_ctx))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_min(
    ObCmpFunc &func,
    ObDatum &l,
    ObDatum &r,
    int64_t &cell_size,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  // when [null, null] merge [a, b], the expect result in mysql mode is [null, b]
  // the lower bound l, with ptr==NULL and null_==true, should not be covered by a.
  //
  // the reason we remove the OB_ISNULL(l.ptr_) condition is that when l is a empty char with l.ptr=0x0 and
  // l.len=0 and null_=false, it should not be covered by r directly
  if (is_empty_) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size))) {
    }
  } else if (OB_FAIL(func.cmp_func_(l, r, cmp, access_ctx))) {
  } else if (cmp > 0) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::prepare_query_range()
{
  int ret = OB_SUCCESS;
  (void)reuse_query_range();
  if (!query_range_info_.can_extract()) {
    is_query_range_ready_ = false;
  } else if (is_empty_) {
    // make empty range
    if (OB_FAIL(fill_empty_query_range(query_range_info_, query_range_allocator_, query_range_))) {
    } else {
      is_query_range_ready_ = true;
    }
  } else {
    // only extract the first column
    int64_t prefix_col_idx = query_range_info_.prefix_col_idxs_.at(0);
    int64_t range_column_cnt = query_range_info_.range_column_cnt_;
    const ObObjMeta &prefix_col_obj_meta = query_range_info_.prefix_col_obj_metas_.at(0);

    query_range_.table_id_ = query_range_info_.table_id_;
    query_range_.border_flag_.set_inclusive_start();
    query_range_.border_flag_.set_inclusive_end();

    const ObDatum &lower_bound = lower_bounds_.at(prefix_col_idx);
    const ObDatum &upper_bound = upper_bounds_.at(prefix_col_idx);
    ObObj *start = NULL;
    ObObj *end = NULL;
    if (OB_ISNULL(start = static_cast<ObObj *>(
                      query_range_allocator_.alloc(sizeof(ObObj) * range_column_cnt)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc memory for start_obj failed", K(ret));
    } else if (OB_ISNULL(end = static_cast<ObObj *>(
                             query_range_allocator_.alloc(sizeof(ObObj) * range_column_cnt)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc memory for end_obj failed", K(ret));
    } else {
      new(start) ObObj();
      new(end) ObObj();
      lower_bound.to_obj(*start, prefix_col_obj_meta);
      upper_bound.to_obj(*end, prefix_col_obj_meta);
      // fill left coloumn with (min, max)
      for (int64_t i = 1; i < range_column_cnt; ++i) {
        new(start + i) ObObj();
        new(end + i) ObObj();
        (start + i)->set_min_value();
        (end + i)->set_max_value();
      }
      ObRowkey start_key(start, range_column_cnt);
      ObRowkey end_key(end, range_column_cnt);
      query_range_.start_key_ = start_key;
      query_range_.end_key_ = end_key;
    }

    if (OB_SUCC(ret)) {
      is_query_range_ready_ = true;
    }
  }
  return ret;
}

void ObRFRangeFilterMsg::after_process()
{
  // prepare_query_range can be failed, but rf still worked
  (void)prepare_query_range();
}

int ObRFRangeFilterMsg::try_extract_query_range(bool &has_extract, ObIArray<ObNewRange> &ranges,
                                                bool need_deep_copy, common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (!is_query_range_ready_) {
    has_extract = false;
  } else {
    // overwrite ranges
    ranges.reset();
    if (need_deep_copy) {
      if (OB_FAIL(ranges.prepare_allocate(1))) {
      } else if (OB_FAIL(deep_copy_range(*allocator, query_range_, ranges.at(0)))) {
      }
    } else {
      if (OB_FAIL(ranges.push_back(query_range_))) {
      }
    }
    if (OB_SUCC(ret)) {
      has_extract = true;
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::adjust_cell_size()
{
  int ret = OB_SUCCESS;
  CK(cells_size_.count() == lower_bounds_.count() &&
     lower_bounds_.count() == upper_bounds_.count());
  for (int i = 0; OB_SUCC(ret) && i < cells_size_.count(); ++i) {
    cells_size_.at(i).min_datum_buf_size_ =
        std::min(cells_size_.at(i).min_datum_buf_size_, (int64_t)lower_bounds_.at(i).len_);
    cells_size_.at(i).max_datum_buf_size_ =
        std::min(cells_size_.at(i).max_datum_buf_size_, (int64_t)upper_bounds_.at(i).len_);
  }
  return ret;
}

int ObRFRangeFilterMsg::dynamic_copy_cell(const ObDatum &src, ObDatum &target, int64_t &cell_size)
{
  int ret = OB_SUCCESS;
  int64_t need_size = src.len_;
  if (src.is_null()) {
    target.null_ = 1;
  } else {
    if (need_size > cell_size) {
      need_size = need_size * 2;
      char *buff_ptr = NULL;
      if (OB_ISNULL(buff_ptr = static_cast<char*>(allocator_.alloc(need_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        SQL_LOG(WARN, "fall to alloc buff", K(need_size), K(ret));
      } else {
        memcpy(buff_ptr, src.ptr_, src.len_);
        target.pack_ = src.pack_;
        target.ptr_ = buff_ptr;
        cell_size = need_size;
      }
    } else {
      memcpy(const_cast<char *>(target.ptr_), src.ptr_, src.len_);
      target.pack_ = src.pack_;
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::get_max(
    ObCmpFunc &func,
    ObDatum &l,
    ObDatum &r,
    int64_t &cell_size,
    const ObDatumAccessContext *access_ctx)
{
  int ret = OB_SUCCESS;
  int cmp = 0;
  if (is_empty_) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size))) {
    }
  } else if (OB_FAIL(func.cmp_func_(l, r, cmp, access_ctx))) {
  } else if (cmp < 0) {
    if (OB_FAIL(dynamic_copy_cell(r, l, cell_size))) {
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(hash_funcs);
  ObDatum *datum = nullptr;
  if (OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (is_empty_) {
    bool ignore_null = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
      ObExpr *expr = expr_array.at(i);
      if (OB_FAIL(expr->eval(eval_ctx, datum))) {
      } else if (datum->is_null() && !need_null_cmp_flags_.at(i)) {
        ignore_null = true;
        break;
      } else if (OB_FAIL(dynamic_copy_cell(*datum, lower_bounds_.at(i), cells_size_.at(i).min_datum_buf_size_))) {
      } else if (OB_FAIL(dynamic_copy_cell(*datum, upper_bounds_.at(i), cells_size_.at(i).max_datum_buf_size_))) {
      }
    }
    if (!ignore_null) {
      is_empty_ = false;
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
      ObExpr *expr = expr_array.at(i);
      if (OB_FAIL(expr->eval(eval_ctx, datum))) {
      } else if (datum->is_null() && !need_null_cmp_flags_.at(i)) {
        /*do nothing*/
        break;
      } else if (OB_FAIL(get_min(cmp_funcs_.at(i), lower_bounds_.at(i), *datum,
                                 cells_size_.at(i).min_datum_buf_size_, datum_access_ctx_))) {
      } else if (OB_FAIL(get_max(cmp_funcs_.at(i), upper_bounds_.at(i), *datum,
                                 cells_size_.at(i).max_datum_buf_size_, datum_access_ctx_))) {
      }
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::insert_by_row_batch(
  const ObBatchRows *child_brs,
  const common::ObIArray<ObExpr *> &expr_array,
  const common::ObHashFuncs &hash_funcs,
  const ObExpr *calc_tablet_id_expr,
  ObEvalCtx &eval_ctx,
  uint64_t *batch_hash_values)
{
  int ret = OB_SUCCESS;
  UNUSED(batch_hash_values);
  UNUSED(calc_tablet_id_expr);
  if (child_brs->size_ > 0) {
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
    batch_info_guard.set_batch_size(child_brs->size_);
    for (int64_t idx = 0; OB_SUCC(ret) && idx < child_brs->size_; ++idx) {
      if (child_brs->skip_->at(idx)) {
        continue;
      } else {
        batch_info_guard.set_batch_idx(idx);
        if (OB_FAIL(insert_by_row(expr_array, hash_funcs,
            calc_tablet_id_expr, eval_ctx))) {
        }
      }
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  ObCmpFunc cmp_func;
  int cmp_min = 0;
  int cmp_max = 0;
  bool is_match = true;
  if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else {
        cmp_min = 0;
        cmp_max = 0;
        cmp_func.cmp_func_ = filter_ctx.cmp_funcs_.at(i).cmp_func_;
        if (OB_FAIL(cmp_func.cmp_func_(
                *datum, lower_bounds_.at(i), cmp_min, datum_access_ctx_))) {
        } else if (cmp_min < 0) {
          is_match = false;
          break;
        } else if (OB_FAIL(cmp_func.cmp_func_(
                       *datum, upper_bounds_.at(i), cmp_max, datum_access_ctx_))) {
        } else if (cmp_max > 0) {
          is_match = false;
          break;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_match) {
        filter_ctx.filter_count_++;
      }
      filter_ctx.check_count_++;
      res.set_int(is_match ? 1 : 0);
      filter_ctx.collect_sample_info(!is_match, 1);
    }
  }
  return ret;
}

int ObRFRangeFilterMsg::do_might_contain_batch(const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) {
  int ret = OB_SUCCESS;
  int64_t filter_count = 0;
  int64_t total_count = 0;
  ObDatum *results = expr.locate_batch_datums(ctx);
  for (int idx = 0; OB_SUCC(ret) && idx < expr.arg_cnt_; ++idx) {
    if (OB_FAIL(expr.args_[idx]->eval_batch(ctx, skip, batch_size))) {
    }
  }
  if (OB_SUCC(ret)) {
    int cmp_min = 0;
    int cmp_max = 0;
    ObDatum *datum = nullptr;
    bool is_match = true;
    for (int64_t batch_i = 0; OB_SUCC(ret) && batch_i < batch_size; ++batch_i) {
      if (skip.at(batch_i)) {
        continue;
      }
      cmp_min = 0;
      cmp_max = 0;
      is_match = true;
      total_count++;
      for (int arg_i = 0; OB_SUCC(ret) && arg_i < expr.arg_cnt_; ++arg_i) {
        datum = &expr.args_[arg_i]->locate_expr_datum(ctx, batch_i);
        if (OB_FAIL(filter_ctx.cmp_funcs_.at(arg_i).cmp_func_(
                *datum, lower_bounds_.at(arg_i), cmp_min, datum_access_ctx_))) {
        } else if (cmp_min < 0) {
          filter_count++;
          is_match = false;
          break;
        } else if (OB_FAIL(filter_ctx.cmp_funcs_.at(arg_i).cmp_func_(
                       *datum, upper_bounds_.at(arg_i), cmp_max, datum_access_ctx_))) {
        } else if (cmp_max > 0) {
          filter_count++;
          is_match = false;
          break;
        }
      }
      results[batch_i].set_int(is_match ? 1 : 0);
    }
  }
  if (OB_SUCC(ret)) {
    filter_ctx.filter_count_ += filter_count;
    filter_ctx.total_count_ += total_count;
    filter_ctx.check_count_ += total_count;
    filter_ctx.collect_sample_info(filter_count, total_count);
  }
  return ret;
}

int ObRFRangeFilterMsg::might_contain_batch(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  ObDatum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
  batch_info_guard.set_batch_size(batch_size);
  if (OB_UNLIKELY(is_empty_)) {
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(0);
    }
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_FAIL(do_might_contain_batch(expr, ctx, skip, batch_size, filter_ctx))) {
  }
  if (OB_SUCC(ret)) {
    eval_flags.set_all(batch_size);
  }
  return ret;
}

int ObRFRangeFilterMsg::prepare_storage_white_filter_data(ObDynamicFilterExecutor &dynamic_filter,
                                ObEvalCtx &eval_ctx,
                                ObRuntimeFilterParams &params,
                                bool &is_data_prepared)
{
  int ret = OB_SUCCESS;
  int col_idx = dynamic_filter.get_col_idx();
  if (is_empty_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::FILTER_ALL);
    is_data_prepared = true;
  } else if (OB_FAIL(params.push_back(lower_bounds_.at(col_idx)))) {
  } else if (OB_FAIL(params.push_back(upper_bounds_.at(col_idx)))) {
  } else {
    dynamic_filter.set_filter_val_meta(build_obj_metas_.at(col_idx));
    is_data_prepared = true;
  }
  return ret;
}

// end ObRFRangeFilterMsg

// ObRFInFilterMsg

int ObRFInFilterMsg::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  const ObRFInFilterMsg &other_msg = static_cast<const ObRFInFilterMsg &>(msg);
  if (OB_FAIL(ObP2PDatahubMsgBase::assign(msg))) {
  } else if (OB_FAIL(cmp_funcs_.assign(other_msg.cmp_funcs_))) {
  } else if (OB_FAIL(hash_funcs_for_insert_.assign(other_msg.hash_funcs_for_insert_))) {
  } else if (OB_FAIL(cur_row_.assign(other_msg.cur_row_))) {
  } else if (OB_FAIL(need_null_cmp_flags_.assign(other_msg.need_null_cmp_flags_))) {
  } else if (OB_FAIL(query_range_info_.assign(other_msg.query_range_info_))) {
  } else if (OB_FAIL(build_obj_metas_.assign(other_msg.build_obj_metas_))) {
  } else {
    col_cnt_ = other_msg.col_cnt_;
    max_in_num_ = other_msg.max_in_num_;
    datum_access_ctx_ = other_msg.datum_access_ctx_;
  }
  return ret;
}

int ObRFInFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&new_msg_ptr)
{
  int ret = OB_SUCCESS;
  ObRFInFilterMsg *in_msg = nullptr;
  int64_t row_cnt = max(serial_rows_.count(), 1);
  ObMemAttr attr("PxInMsg");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObRFInFilterMsg>(attr, in_msg))) {
  } else if (OB_FAIL(in_msg->assign(*this))) {
  } else if (OB_FAIL(in_msg->rows_set_.create(row_cnt * 2,
        "RFCPInFilter",
        "RFCPInFilter"))) {
  } else {
    int64_t row_cnt = serial_rows_.count();
    if (0 == row_cnt) {
    } else {
      for (int i = 0; i < row_cnt && OB_SUCC(ret); ++i) {
        for (int j = 0; j < col_cnt_ && OB_SUCC(ret); ++j) {
          in_msg->cur_row_.at(j) = serial_rows_.at(i)->at(j);
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(in_msg->append_row())) {
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      new_msg_ptr = in_msg;
    }
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(in_msg)) {
    in_msg->destroy();
    ob_free(in_msg);
  }
  return ret;
}

int ObRFInFilterMsg::insert_by_row_batch(
  const ObBatchRows *child_brs,
  const common::ObIArray<ObExpr *> &expr_array,
  const common::ObHashFuncs &hash_funcs,
  const ObExpr *calc_tablet_id_expr,
  ObEvalCtx &eval_ctx,
  uint64_t *batch_hash_values)
{
  int ret = OB_SUCCESS;
  UNUSED(batch_hash_values);
  UNUSED(calc_tablet_id_expr);
  if (child_brs->size_ > 0 && is_active_
      && OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
    LOG_WARN("failed to get datum access context", K(ret));
  } else if (child_brs->size_ > 0 && is_active_) {
    ObEvalCtx::BatchInfoScopeGuard batch_info_guard(eval_ctx);
    batch_info_guard.set_batch_size(child_brs->size_);
    for (int64_t idx = 0; OB_SUCC(ret) && idx < child_brs->size_; ++idx) {
      if (child_brs->skip_->at(idx)) {
        continue;
      } else {
        batch_info_guard.set_batch_idx(idx);
        ObDatum *datum = nullptr;
        bool ignore_null_row = false;
        for (int64_t i = 0; OB_SUCC(ret) && i < expr_array.count(); ++i) {
          ObExpr *expr = expr_array.at(i);
          if (OB_FAIL(expr->eval(eval_ctx, datum))) {
          } else if (datum->is_null() && !need_null_cmp_flags_.at(i)) {
            ignore_null_row = true;
            break;
          } else {
            cur_row_.at(i) = (*datum);
          }
        }
        if (OB_SUCC(ret) && !ignore_null_row) {
          if (OB_FAIL(insert_node())) {
          }
        }
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::insert_node()
{
  int ret = OB_SUCCESS;
  ObRFInFilterNode node(
      &cmp_funcs_, &hash_funcs_for_insert_, &cur_row_, datum_access_ctx_);
  if (OB_FAIL(rows_set_.exist_refactored(node))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      if (serial_rows_.count() > max_in_num_) {
        is_active_ = false;
      } else if (OB_FAIL(append_row())) {
      } else if (is_empty_) {
        is_empty_ = false;
      }
    } else if (OB_HASH_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("fail to check node", K(ret));
    }
  }
  return ret;
}

int ObRFInFilterMsg::insert_by_row(
    const common::ObIArray<ObExpr *> &expr_array,
    const common::ObHashFuncs &hash_funcs,
    const ObExpr *calc_tablet_id_expr,
    ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  if (is_active_ && OB_FAIL(eval_ctx.get_datum_access_ctx(datum_access_ctx_))) {
    LOG_WARN("failed to get datum access context", K(ret));
  } else if (is_active_) {
    bool ignore_null_row = false;
    for (int64_t idx = 0; OB_SUCC(ret) && idx < expr_array.count() ; ++idx) {
      datum = nullptr;
      if (OB_FAIL(expr_array.at(idx)->eval(eval_ctx, datum))) {
      } else if (datum->is_null() && !need_null_cmp_flags_.at(idx)) {
        ignore_null_row = true;
        break;
      } else {
        cur_row_.at(idx) = (*datum);
      }
    }
    if (OB_SUCC(ret) && !ignore_null_row) {
      if (OB_FAIL(insert_node())) {
      }
    }
  }

  return ret;
}

int ObRFInFilterMsg::append_row()
{
  int ret = OB_SUCCESS;
  ObFixedArray<ObDatum, ObIAllocator> *new_row = nullptr;
  void *array_ptr = nullptr;
  if (OB_ISNULL(array_ptr = allocator_.alloc(sizeof(ObFixedArray<ObDatum, ObIAllocator>)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else {
    new_row = new(array_ptr) ObFixedArray<ObDatum, ObIAllocator>(allocator_);
    if (OB_FAIL(new_row->init(cur_row_.count()))) {
    } else {
      ObDatum datum;
      for (int i = 0; i < cur_row_.count() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(datum.deep_copy(cur_row_.at(i), allocator_))) {
        } else if (OB_FAIL(new_row->push_back(datum))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(serial_rows_.push_back(new_row))) {
        } else {
          ObRFInFilterNode node(
              &cmp_funcs_, &hash_funcs_for_insert_, new_row, datum_access_ctx_);
          if (OB_FAIL(rows_set_.set_refactored(node))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::ObRFInFilterNode::hash(uint64_t &hash_ret) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(hash_funcs_)) {
    hash_ret = hash_val_;
  } else {
    hash_ret = ObExprJoinFilter::JOIN_FILTER_SEED;
    for (int i = 0; i < row_->count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(hash_funcs_->at(i).hash_func_(
              row_->at(i), hash_ret, hash_ret, datum_access_ctx_))) {
      }
    }
  }

  return ret;
}

// the ObRFInFilterNode stores in ObRFInFilter always be the datum of build table,
// while the other node can be the build table(during insert or merge process)
// or the probe table(during filter process),
// so the compare process relies on the other node, always using other's cmp_func_.
bool ObRFInFilterMsg::ObRFInFilterNode::operator==(const ObRFInFilterNode &other) const
{
  int cmp_ret = 0;
  bool ret = true;
  for (int i = 0; i < other.row_->count(); ++i) {
    if (row_->at(i).is_null() && other.row_->at(i).is_null()) {
      continue;
    } else {
      // because cmp_func is chosen as compare(probe_data/build_data, build_data)
      // so the other's data must be placed at first
      int tmp_ret = other.cmp_funcs_->at(i).cmp_func_(
          other.row_->at(i), row_->at(i), cmp_ret, other.datum_access_ctx_);
      if (OB_SUCCESS != tmp_ret || cmp_ret != 0) {
        if (OB_SUCCESS != tmp_ret) {
        }
        ret = false;
        break;
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObRFInFilterMsg &in_msg = static_cast<ObRFInFilterMsg &>(msg);
  if (!msg.is_active()) {
    is_active_ = false;
  } else if (!msg.is_empty() && is_active_) {
    ObSpinLockGuard guard(lock_);
    if (OB_ISNULL(datum_access_ctx_)) {
      datum_access_ctx_ = in_msg.datum_access_ctx_;
    }
    for (int i = 0; i < in_msg.serial_rows_.count() && OB_SUCC(ret); ++i) {
      for (int j = 0; j < in_msg.serial_rows_.at(i)->count(); ++j) {
        cur_row_.at(j) = in_msg.serial_rows_.at(i)->at(j);
      }
      if (OB_FAIL(insert_node())) {
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::might_contain(const ObExpr &expr,
      ObEvalCtx &ctx,
      ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx,
      ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *datum = nullptr;
  bool is_match = true;
  uint64_t hash_val = ObExprJoinFilter::JOIN_FILTER_SEED;
  ObIArray<ObDatum> &cur_row = filter_ctx.cur_row_;
  if (OB_UNLIKELY(!is_active_)) {
    res.set_int(1);
  } else if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else {
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else {
        cur_row.at(i) = *datum;
        ObHashFunc hash_func;
        hash_func.hash_func_ = filter_ctx.hash_funcs_.at(i).hash_func_;
        if (OB_FAIL(hash_func.hash_func_(
                *datum, hash_val, hash_val, datum_access_ctx_))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObRFInFilterNode node(
          &filter_ctx.cmp_funcs_, nullptr, &cur_row, datum_access_ctx_, hash_val);
      if (OB_FAIL(rows_set_.exist_refactored(node))) {
        if (OB_HASH_NOT_EXIST == ret) {
          is_match = false;
          ret = OB_SUCCESS;
        } else if (OB_HASH_EXIST == ret) {
          is_match = true;
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to check node", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_match) {
        filter_ctx.filter_count_++;
      }
      filter_ctx.check_count_++;
      res.set_int(is_match ? 1 : 0);
      filter_ctx.collect_sample_info(!is_match, 1);
    }
  }
  return ret;
}

int ObRFInFilterMsg::reuse()
{
  int ret = OB_SUCCESS;
  is_empty_ = true;
  serial_rows_.reset();
  rows_set_.reuse();
  (void)reuse_query_range();
  is_active_ = true;
  return ret;
}

int ObRFInFilterMsg::do_might_contain_batch(const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx) {
  int ret = OB_SUCCESS;
  int64_t filter_count = 0;
  int64_t total_count = 0;
  uint64_t *right_hash_vals = reinterpret_cast<uint64_t *>(
                                ctx.frames_[expr.frame_idx_] + expr.res_buf_off_);
  uint64_t seed = ObExprJoinFilter::JOIN_FILTER_SEED;
  for (int idx = 0; OB_SUCC(ret) && idx < expr.arg_cnt_; ++idx) {
    if (OB_FAIL(expr.args_[idx]->eval_batch(ctx, skip, batch_size))) {
    } else {
      const bool is_batch_seed = (idx > 0);
      ObBatchDatumHashFunc hash_func = filter_ctx.hash_funcs_.at(idx).batch_hash_func_;
      hash_func(right_hash_vals,
                expr.args_[idx]->locate_batch_datums(ctx), expr.args_[idx]->is_batch_result(),
                skip, batch_size,
                is_batch_seed ? right_hash_vals : &seed,
                is_batch_seed,
                datum_access_ctx_);
    }
  }
  ObIArray<ObDatum> &cur_row = filter_ctx.cur_row_;
  ObRFInFilterNode node(
      &filter_ctx.cmp_funcs_, nullptr, &cur_row, datum_access_ctx_, 0);
  ObDatum *res_datums = expr.locate_batch_datums(ctx);
  for (int64_t batch_i = 0; OB_SUCC(ret) && batch_i < batch_size; ++batch_i) {
    if (skip.at(batch_i)) {
      continue;
    }
    total_count++;
    node.hash_val_ = right_hash_vals[batch_i];
    for (int64_t arg_i = 0; OB_SUCC(ret) && arg_i < expr.arg_cnt_; ++arg_i) {
      cur_row.at(arg_i) = expr.args_[arg_i]->locate_expr_datum(ctx, batch_i);
    } 
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(rows_set_.exist_refactored(node))) {
      if (OB_HASH_NOT_EXIST == ret) {
        res_datums[batch_i].set_int(0);
        filter_count++;
        ret = OB_SUCCESS;
      } else if (OB_HASH_EXIST == ret) {
        res_datums[batch_i].set_int(1);
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to check node", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    filter_ctx.filter_count_ += filter_count;
    filter_ctx.total_count_ += total_count;
    filter_ctx.check_count_ += total_count;
    filter_ctx.collect_sample_info(filter_count, total_count);
  }
  return ret;
}

int ObRFInFilterMsg::might_contain_batch(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    const ObBitVector &skip,
    const int64_t batch_size,
    ObExprJoinFilter::ObExprJoinFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  ObDatum *results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
  batch_info_guard.set_batch_size(batch_size);
  if (!is_active_) {
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(1);
    }
  } else if (OB_UNLIKELY(is_empty_)) {
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(0);
    }
  } else if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_FAIL(do_might_contain_batch(expr, ctx, skip, batch_size, filter_ctx))) {
  }
  if (OB_SUCC(ret)) {
    eval_flags.set_all(batch_size);
  }
  return ret;
}

int ObRFInFilterMsg::prepare_storage_white_filter_data(ObDynamicFilterExecutor &dynamic_filter,
                                ObEvalCtx &eval_ctx,
                                ObRuntimeFilterParams &params,
                                bool &is_data_prepared)
{
  int ret = OB_SUCCESS;
  int col_idx = dynamic_filter.get_col_idx();
  if (!is_active_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::PASS_ALL);
    is_data_prepared = true;
  } else if (is_empty_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::FILTER_ALL);
    is_data_prepared = true;
  } else {
    for (int64_t i = 0; i < serial_rows_.count() && OB_SUCC(ret); ++i) {
      if (OB_FAIL(params.push_back(serial_rows_.at(i)->at(col_idx)))) {
      }
    }
    if (OB_SUCC(ret)) {
      dynamic_filter.set_filter_val_meta(build_obj_metas_.at(col_idx));
      is_data_prepared = true;
    }
  }
  return ret;
}

int ObRFInFilterMsg::destroy()
{
  int ret = OB_SUCCESS;
  rows_set_.destroy();
  hash_funcs_for_insert_.reset();
  cmp_funcs_.reset();
  need_null_cmp_flags_.reset();
  build_obj_metas_.reset();
  cur_row_.reset();
  for (int i = 0; i < serial_rows_.count(); ++i) {
    if (OB_NOT_NULL(serial_rows_.at(i))) {
      serial_rows_.at(i)->reset();
    }
  }
  serial_rows_.reset();
  query_range_info_.destroy();
  query_range_.destroy();
  query_range_allocator_.reset();
  allocator_.reset();
  return ret;
}

int ObRFInFilterMsg::prepare_query_ranges()
{
  int ret = OB_SUCCESS;
  (void)reuse_query_range();
  if (!query_range_info_.can_extract() || !is_active_) {
    is_query_range_ready_ = false;
  } else if (is_empty_) {
    // make empty range
    ObNewRange query_range;
    if (OB_FAIL(fill_empty_query_range(query_range_info_, query_range_allocator_, query_range))) {
    } else if (OB_FAIL(query_range_.push_back(query_range))) {
    } else {
      is_query_range_ready_ = true;
    }
  } else if (query_range_info_.prefix_col_idxs_.count() == col_cnt_) {
    // col count matches, the hashmap make sure all rows in the filter are different
    // so not need to deduplicate
    ret = process_query_ranges_without_deduplicate();
  } else {
    // prefix col less than index column, need do deduplicate
    // for example:
    // there are three rows int the filter :{[1,2,3], [1,2,4], [1,2,5]}
    // and the range column is c1,c2
    // final query range extracted should be: range(1,2; 1,2)
    // we need to deduplicate to avoid duplicate range
    ret = process_query_ranges_with_deduplicate();
  }
  LOG_TRACE("in filter prepare query range", K(ret), K(is_query_range_ready_),
            K(query_range_.count()), K(rows_set_.size()),
            K(query_range_info_.prefix_col_idxs_.count()), K(col_cnt_), K(query_range_),
            K(query_range_info_), K(is_empty_));
  return ret;
}

int ObRFInFilterMsg::process_query_ranges_with_deduplicate()
{
  int ret = OB_SUCCESS;
  int64_t max_in_filter_query_range_count = ObPxQueryRangeInfo::MAX_IN_FILTER_QUERY_RANGE_COUNT;

#ifdef ERRSIM
  int tmp_ret = OB_E(EventTable::EN_PX_MAX_IN_FILTER_QR_COUNT) OB_SUCCESS;
  if (OB_SUCCESS != tmp_ret) {
    max_in_filter_query_range_count = max_in_num_;
  }
#endif

  hash::ObHashSet<ObRFInFilterNode, hash::NoPthreadDefendMode> tmp_rows_set;
  ObArenaAllocator tmp_allocator;
  ObHashFuncs hash_func(tmp_allocator);
  ObCmpFuncs cmp_funcs(tmp_allocator);
  const ObIArray<int64_t> &prefix_col_idxs = query_range_info_.prefix_col_idxs_;

  if (OB_FAIL(tmp_rows_set.create(rows_set_.size() * 2, "RFInTmpHashSet", "RFInTmpHashSet"))) {
  } else if (OB_FAIL(hash_func.init(prefix_col_idxs.count()))) {
  } else if (OB_FAIL(cmp_funcs.init(prefix_col_idxs.count()))) {
  }
  // reorder compare function and hash function
  for (int64_t j = 0; j < prefix_col_idxs.count() && OB_SUCC(ret); ++j) {
    int64_t col_idx = prefix_col_idxs.at(j);
    if (OB_FAIL(hash_func.push_back(hash_funcs_for_insert_.at(col_idx)))) {
    } else if (OB_FAIL(cmp_funcs.push_back(cmp_funcs_.at(col_idx)))) {
    }
  }
  ObTMArray<ObTMArray<ObDatum>> tmp_rows;
  ObTMArray<int64_t> effective_row_idxs;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(tmp_rows.prepare_allocate(serial_rows_.count()))) {
  } else if (OB_FAIL(effective_row_idxs.reserve(serial_rows_.count()))) {
  }
  for (int64_t row_idx = 0; row_idx < serial_rows_.count() && OB_SUCC(ret); ++row_idx) {
    if (OB_ISNULL(serial_rows_.at(row_idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("this row is null", K(ret));
    } else {
      ObTMArray<ObDatum> &tmp_row = tmp_rows.at(row_idx);
      if (OB_FAIL(tmp_row.prepare_allocate(prefix_col_idxs.count()))) {
      }
      for (int64_t j = 0; j < prefix_col_idxs.count() && OB_SUCC(ret); ++j) {
        int64_t col_idx = prefix_col_idxs.at(j);
        tmp_row.at(j) = serial_rows_.at(row_idx)->at(col_idx);
      }
      bool is_duplicate = true;
      if (OB_SUCC(ret)) {
        ObRFInFilterNode node(
            &cmp_funcs, &hash_func, &tmp_row, datum_access_ctx_);
        if (OB_FAIL(tmp_rows_set.set_refactored(node, 0/*not cover*/))) {
          if (ret != OB_HASH_EXIST) {
            LOG_WARN("failed to set_refactored");
          } else {
            ret = OB_SUCCESS;
          }
        } else {
          is_duplicate = false;
        }
      }
      if (!is_duplicate) {
        OZ(effective_row_idxs.push_back(row_idx));
        if (effective_row_idxs.count() > max_in_filter_query_range_count) {
          // no more than MAX_IN_FILTER_QUERY_RANGE_COUNT can be extracted
          // TODO[zhouhaiyu.zhy]: if the data of create table' prefix columns shows a high rate of
          // duplication and the final count of effective rows still exceeds
          // max_in_filter_query_range_count(128) the execution of the "prepare_query_ranges"
          // becomes redundant and may result in a decrease in performance.
          break;
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (effective_row_idxs.count() > max_in_filter_query_range_count) {
      is_query_range_ready_ = false;
    } else {
      if (OB_FAIL(query_range_.reserve(effective_row_idxs.count()))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < effective_row_idxs.count(); ++i) {
        OZ(generate_one_range(effective_row_idxs.at(i)));
      }
      if (OB_SUCC(ret)) {
        is_query_range_ready_ = true;
        LOG_DEBUG("TBDelete in filter succ extract query range", K(query_range_.count()),
                  K(serial_rows_.count()), K(query_range_));
      }
    }
  }
  return ret;
}

int ObRFInFilterMsg::process_query_ranges_without_deduplicate()
{
  int ret = OB_SUCCESS;
  int64_t max_in_filter_query_range_count = ObPxQueryRangeInfo::MAX_IN_FILTER_QUERY_RANGE_COUNT;

#ifdef ERRSIM
  int tmp_ret = OB_E(EventTable::EN_PX_MAX_IN_FILTER_QR_COUNT) OB_SUCCESS;
  if (OB_SUCCESS != tmp_ret) {
    max_in_filter_query_range_count = max_in_num_;
  }
#endif

  if (serial_rows_.count() > max_in_filter_query_range_count) {
    is_query_range_ready_ = false;
  } else {
    if (OB_FAIL(query_range_.reserve(serial_rows_.count()))) {
    }
    for (int64_t row_idx = 0; row_idx < serial_rows_.count() && OB_SUCC(ret); ++row_idx) {
      if (OB_ISNULL(serial_rows_.at(row_idx))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("this row is null", K(ret));
      } else {
        OZ(generate_one_range(row_idx));
      }
    }
    if (OB_SUCC(ret)) {
      is_query_range_ready_ = true;
      LOG_DEBUG("TBDelete in filter succ extract query range", K(serial_rows_.count()),
                K(query_range_));
    }
  }
  return ret;
}

int ObRFInFilterMsg::generate_one_range(int row_idx)
{
  int ret = OB_SUCCESS;
  int64_t range_column_cnt = query_range_info_.range_column_cnt_;
  const ObIArray<int64_t> &prefix_col_idxs = query_range_info_.prefix_col_idxs_;
  const ObIArray<ObObjMeta> &prefix_col_obj_metas = query_range_info_.prefix_col_obj_metas_;

  ObNewRange query_range;
  query_range.table_id_ = query_range_info_.table_id_;
  query_range.border_flag_.set_inclusive_start();
  query_range.border_flag_.set_inclusive_end();
  ObObj *start = NULL;
  ObObj *end = NULL;
  if (OB_ISNULL(start = static_cast<ObObj *>(
                    query_range_allocator_.alloc(sizeof(ObObj) * range_column_cnt)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory for start_obj failed", K(ret));
  } else if (OB_ISNULL(end = static_cast<ObObj *>(
                           query_range_allocator_.alloc(sizeof(ObObj) * range_column_cnt)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory for end_obj failed", K(ret));
  }
  for (int64_t j = 0; j < prefix_col_idxs.count() && OB_SUCC(ret); ++j) {
    int64_t col_idx = prefix_col_idxs.at(j);
    const ObObjMeta &obj_meta = prefix_col_obj_metas.at(j);
    ObDatum &datum = serial_rows_.at(row_idx)->at(col_idx);
    new (start + j) ObObj();
    new (end + j) ObObj();
    datum.to_obj(*(start + j), obj_meta);
    datum.to_obj(*(end + j), obj_meta);
  }
  for (int64_t j = prefix_col_idxs.count(); j < range_column_cnt && OB_SUCC(ret); ++j) {
    new (start + j) ObObj();
    new (end + j) ObObj();
    (start + j)->set_min_value();
    (end + j)->set_max_value();
  }
  if (OB_SUCC(ret)) {
    ObRowkey start_key(start, range_column_cnt);
    ObRowkey end_key(end, range_column_cnt);
    query_range.start_key_ = start_key;
    query_range.end_key_ = end_key;
    if (OB_FAIL(query_range_.push_back(query_range))) {
    }
  }
  return ret;
}

void ObRFInFilterMsg::after_process()
{
  // prepare_query_ranges can be failed, but rf still worked
  (void)prepare_query_ranges();
}

int ObRFInFilterMsg::try_extract_query_range(bool &has_extract, ObIArray<ObNewRange> &ranges,
                                             bool need_deep_copy, common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (!is_query_range_ready_) {
    has_extract = false;
  } else {
    // overwrite ranges
    ranges.reset();
    if (need_deep_copy) {
      if (OB_FAIL(ranges.prepare_allocate(query_range_.count()))) {
      } else if (need_deep_copy) {
        for (int64_t i = 0; i < ranges.count() && OB_SUCC(ret); ++i) {
          if (OB_FAIL(deep_copy_range(*allocator, query_range_.at(i), ranges.at(i)))) {
          }
        }
      }
    } else {
      if (OB_FAIL(ranges.assign(query_range_))) {
      }
    }
    if (OB_SUCC(ret)) {
      has_extract = true;
    }
  }
  return ret;
}

//end ObRFInFilterMsg
