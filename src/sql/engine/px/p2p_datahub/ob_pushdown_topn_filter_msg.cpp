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

#include "ob_pushdown_topn_filter_msg.h"
#include "common/datum/datum_parameter_codec.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_mgr.h"
#include "sql/engine/expr/ob_expr_topn_filter.h"
#include "sql/engine/px/ob_px_sqc_handler.h"


namespace oceanbase
{
namespace sql
{

OB_SERIALIZE_MEMBER(ObTopNFilterCmpMeta, ser_cmp_func_, obj_meta_);
OB_SERIALIZE_MEMBER(ObTopNFilterCompare, build_meta_, filter_meta_, is_ascending_, null_pos_);
OB_SERIALIZE_MEMBER(ObPushDownTopNFilterInfo, enabled_, p2p_dh_id_, effective_sk_cnt_,
                    total_sk_cnt_, cmp_metas_, dh_msg_type_, expr_ctx_id_, is_shared_, is_shuffle_,
                    max_batch_size_, adaptive_filter_ratio_);

int ObPushDownTopNFilterInfo::init(int64_t p2p_dh_id, int64_t effective_sk_cnt,
                                   int64_t total_sk_cnt,
                                   const ObIArray<ObTopNFilterCmpMeta> &cmp_metas,
                                   ObP2PDatahubMsgBase::ObP2PDatahubMsgType dh_msg_type,
                                   uint32_t expr_ctx_id, bool is_shared, bool is_shuffle,
                                   int64_t max_batch_size, double adaptive_filter_ratio)
{
  int ret = OB_SUCCESS;
  p2p_dh_id_ = p2p_dh_id;
  effective_sk_cnt_ = effective_sk_cnt;
  total_sk_cnt_ = total_sk_cnt;
  dh_msg_type_ = dh_msg_type;
  expr_ctx_id_ = expr_ctx_id;
  is_shared_ = is_shared;
  is_shuffle_ = is_shuffle;
  max_batch_size_ = max_batch_size;
  adaptive_filter_ratio_ = adaptive_filter_ratio;
  if (OB_FAIL(cmp_metas_.assign(cmp_metas))) {
  } else {
    enabled_ = true;
  }
  return ret;
}


namespace
{
// TopN v1 carries comparison metadata before the old ObDatum records. These
// bounded readers preserve that physical protocol without a resident descriptor.
int topn_integer(const char *buf, int64_t length, int64_t &pos, int64_t &value, int bits = 64)
{
  if (buf == nullptr || pos < 0 || pos >= length) { return OB_DESERIALIZE_ERROR; }
  uint64_t result = 0;
  const int64_t start = pos;
  for (int i = 0; i < (bits + 6) / 7 && start + i < length; ++i) {
    const uint8_t byte = static_cast<uint8_t>(buf[start + i]);
    const int shift = i * 7;
    if (shift + 7 > bits && (byte & 0x7f) >= (1U << (bits - shift))) {
      return OB_DESERIALIZE_ERROR;
    }
    result |= static_cast<uint64_t>(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) { value = static_cast<int64_t>(result); pos = start + i + 1; return OB_SUCCESS; }
  }
  return OB_DESERIALIZE_ERROR;
}

int topn_record(const char *buf, int64_t length, int64_t &pos, int64_t &begin, int64_t &end)
{
  int64_t cursor = pos, version = 0, bytes = 0;
  int ret = topn_integer(buf, length, cursor, version);
  if (OB_FAIL(ret)) {
  } else if (version != 1) { ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(topn_integer(buf, length, cursor, bytes))) {
  } else if (bytes < 0 || bytes > length - cursor) { ret = OB_DESERIALIZE_ERROR;
  } else { begin = cursor; end = cursor + bytes; pos = end; }
  return ret;
}

int topn_count(const char *buf, int64_t length, int64_t &pos, int64_t &count)
{
  int ret = topn_integer(buf, length, pos, count);
  if (OB_SUCC(ret) && (count < 0 || count > UINT32_MAX || count > length - pos)) {
    ret = OB_DESERIALIZE_ERROR;
  }
  return ret;
}

int topn_base_record(const char *buf, int64_t length, int64_t &pos)
{
  int64_t cursor = 0, end = 0, trace_pos = 0, trace_end = 0, value = 0;
  int ret = topn_record(buf, length, pos, cursor, end);
  if (OB_SUCC(ret)) { ret = topn_record(buf, end, cursor, trace_pos, trace_end); }
  for (int i = 0; OB_SUCC(ret) && i < 4; ++i) { ret = topn_integer(buf, trace_end, trace_pos, value); }
  if (OB_SUCC(ret) && trace_pos != trace_end) { ret = OB_DESERIALIZE_ERROR; }
  for (int i = 0; OB_SUCC(ret) && i < 5; ++i) { ret = topn_integer(buf, end, cursor, value, i == 4 ? 32 : 64); }
  if (OB_SUCC(ret) && (end - cursor != 2 || static_cast<uint8_t>(buf[cursor]) > 1
      || static_cast<uint8_t>(buf[cursor + 1]) > 1)) { ret = OB_DESERIALIZE_ERROR; }
  return ret;
}

int topn_compare_record(const char *buf, int64_t length, int64_t &pos)
{
  int64_t cursor = 0, end = 0, value = 0;
  int ret = topn_record(buf, length, pos, cursor, end);
  for (int i = 0; OB_SUCC(ret) && i < 2; ++i) {
    int64_t meta_pos = 0, meta_end = 0;
    if (OB_FAIL(topn_record(buf, end, cursor, meta_pos, meta_end))) {
    } else if (OB_FAIL(topn_integer(buf, meta_end, meta_pos, value))) { // serialized comparator
    } else if (meta_end - meta_pos != 4
        || static_cast<uint8_t>(buf[meta_pos]) >= ObMaxType) { ret = OB_DESERIALIZE_ERROR; }
  }
  if (OB_SUCC(ret) && (cursor >= end || static_cast<uint8_t>(buf[cursor++]) > 1)) { ret = OB_DESERIALIZE_ERROR; }
  if (OB_SUCC(ret)) { ret = topn_integer(buf, end, cursor, value, 32); }
  if (OB_SUCC(ret) && (cursor != end || (value != NULL_FIRST && value != NULL_LAST))) { ret = OB_DESERIALIZE_ERROR; }
  return ret;
}

int topn_cell(const char *buf, int64_t length, int64_t &pos, ObDatum *output = nullptr)
{
  int64_t cursor = 0, end = 0, pack = 0;
  int ret = topn_record(buf, length, pos, cursor, end);
  if (OB_SUCC(ret)) { ret = topn_integer(buf, end, cursor, pack, 32); }
  if (OB_SUCC(ret)) {
    ObDatum value;
    value.pack_ = static_cast<uint32_t>(pack);
    value.ptr_ = buf + cursor;
    if (value.len_ != end - cursor || (value.is_ext()
        && value.len_ != DATUM_EXTEND_PAYLOAD_SIZE && value.len_ != DATUM_LEGACY_EXTEND_SIZE)) {
      ret = OB_DESERIALIZE_ERROR;
    } else if (output != nullptr) { *output = value; }
  }
  return ret;
}

int topn_preflight(const char *buf, int64_t length, int64_t pos)
{
  int64_t count = 0, values = 0, capacities = 0, total = 0, value = 0;
  int ret = topn_base_record(buf, length, pos);
  if (OB_SUCC(ret)) { ret = topn_integer(buf, length, pos, total); }
  if (OB_SUCC(ret)) { ret = topn_count(buf, length, pos, count); }
  if (OB_SUCC(ret) && (total < count || total < 0)) { ret = OB_DESERIALIZE_ERROR; }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) { ret = topn_compare_record(buf, length, pos); }
  if (OB_SUCC(ret)) { ret = topn_count(buf, length, pos, values); }
  if (OB_SUCC(ret) && count != values) { ret = OB_DESERIALIZE_ERROR; }
  for (int64_t i = 0; OB_SUCC(ret) && i < values; ++i) { ret = topn_cell(buf, length, pos); }
  if (OB_SUCC(ret)) { ret = topn_count(buf, length, pos, capacities); }
  if (OB_SUCC(ret) && capacities != count) { ret = OB_DESERIALIZE_ERROR; }
  for (int64_t i = 0; OB_SUCC(ret) && i < capacities; ++i) {
    if (OB_FAIL(topn_integer(buf, length, pos, value))) {
    } else if (value < 0) { ret = OB_DESERIALIZE_ERROR; }
  }
  // Existing UNIS permits omitted trailing version/ties members.
  if (OB_SUCC(ret) && pos < length) { ret = topn_integer(buf, length, pos, value); }
  if (OB_SUCC(ret) && pos < length && static_cast<uint8_t>(buf[pos]) > 1) { ret = OB_DESERIALIZE_ERROR; }
  return ret;
}
} // namespace

OB_DEF_SERIALIZE(ObPushDownTopNFilterMsg)
{
  int ret = OB_SUCCESS;
  const int64_t count = heap_top_datums_.count();
  if (compares_.count() != count || cells_size_.count() != count || total_sk_cnt_ < count) {
    return OB_INVALID_ARGUMENT;
  }
  BASE_SER((ObPushDownTopNFilterMsg, ObP2PDatahubMsgBase));
  LST_DO_CODE(OB_UNIS_ENCODE, total_sk_cnt_, compares_, count);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    alignas(8) char scratch[64];
    ObDatum legacy;
    if (OB_FAIL(DatumParameterCodec::export_native_value(heap_top_datums_.at(i),
        compares_.at(i).build_meta_.obj_meta_, legacy, scratch, sizeof(scratch)))) {
    } else { OB_UNIS_ENCODE(legacy); }
  }
  LST_DO_CODE(OB_UNIS_ENCODE, cells_size_, data_version_, is_fetch_with_ties_);
  return ret;
}

int ObPushDownTopNFilterMsg::deserialize(const char *buf, const int64_t data_len, int64_t &pos)
{
  int64_t cursor = pos, begin = 0, end = 0;
  int ret = topn_record(buf, data_len, cursor, begin, end);
  if (OB_SUCC(ret)) {
    int64_t body_pos = 0;
    ret = deserialize_(buf + begin, end - begin, body_pos);
  }
  if (OB_SUCC(ret)) { pos = end; }
  return ret;
}

int ObPushDownTopNFilterMsg::deserialize_(const char *buf, const int64_t data_len, int64_t &pos)
{
  int ret = topn_preflight(buf, data_len, pos);
  if (OB_SUCC(ret) && is_ready_) { ret = OB_STATE_NOT_MATCH; }
  if (OB_SUCC(ret)) {
    (void)destroy();
    int64_t cursor = pos, count = 0;
    if (OB_FAIL(ObP2PDatahubMsgBase::deserialize(buf, data_len, cursor))) {
    } else if (OB_FAIL(topn_integer(buf, data_len, cursor, total_sk_cnt_))) {
    } else if (OB_FAIL(serialization::decode(buf, data_len, cursor, compares_))) {
    } else if (OB_FAIL(topn_count(buf, data_len, cursor, count))) {
    } else if (OB_FAIL(heap_top_datums_.prepare_allocate(count))) {
    }
    const bool active = is_active_;
    is_active_ = false;
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      ObDatum legacy;
      if (OB_ISNULL(compares_.at(i).build_meta_.cmp_func_)
          || OB_ISNULL(compares_.at(i).filter_meta_.cmp_func_)) { ret = OB_DESERIALIZE_ERROR;
      } else if (OB_FAIL(topn_cell(buf, data_len, cursor, &legacy))) {
      } else if (is_empty_ && legacy.pack_ == 0) {
        // Old init() constructed empty descriptors before the first update.
        // They have no scalar payload; the base empty state keeps them unused.
        heap_top_datums_.at(i).set_null();
      } else if (OB_FAIL(DatumParameterCodec::import_native_value(legacy,
          compares_.at(i).build_meta_.obj_meta_, allocator_, heap_top_datums_.at(i)))) {
      }
    }
    if (OB_SUCC(ret)) { ret = serialization::decode(buf, data_len, cursor, cells_size_); }
    if (OB_SUCC(ret) && cursor < data_len) { ret = topn_integer(buf, data_len, cursor, data_version_); }
    if (OB_SUCC(ret) && cursor < data_len) { is_fetch_with_ties_ = buf[cursor++] != 0; }
    if (OB_SUCC(ret)) { ret = adjust_cell_size(); }
    if (OB_FAIL(ret)) { (void)destroy(); }
    else { is_active_ = active; pos = data_len; }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPushDownTopNFilterMsg)
{
  int64_t len = 0;
  BASE_ADD_LEN((ObPushDownTopNFilterMsg, ObP2PDatahubMsgBase));
  const int64_t count = heap_top_datums_.count();
  OB_ASSERT(compares_.count() == count && cells_size_.count() == count);
  LST_DO_CODE(OB_UNIS_ADD_LEN, total_sk_cnt_, compares_, count);
  for (int64_t i = 0; i < count; ++i) {
    alignas(8) char scratch[64];
    ObDatum legacy;
    const int ret = DatumParameterCodec::export_native_value(heap_top_datums_.at(i),
        compares_.at(i).build_meta_.obj_meta_, legacy, scratch, sizeof(scratch));
    OB_ASSERT(ret == OB_SUCCESS);
    OB_UNIS_ADD_LEN(legacy);
  }
  LST_DO_CODE(OB_UNIS_ADD_LEN, cells_size_, data_version_, is_fetch_with_ties_);
  return len;
}
ObPushDownTopNFilterMsg::ObPushDownTopNFilterMsg()
    : ObP2PDatahubMsgBase(), total_sk_cnt_(0), datum_access_ctx_(nullptr),
      compares_(allocator_), heap_top_datums_(allocator_),
      cells_size_(allocator_), data_version_(0), is_fetch_with_ties_(false)
{}

int ObPushDownTopNFilterMsg::init(const ObPushDownTopNFilterInfo *pd_topn_filter_info,
                                  const ObIArray<ObSortFieldCollation> *sort_collations,
                                  ObExecContext *exec_ctx,
                                  int64_t px_seq_id,
                                  bool is_fetch_with_ties)
{
  int ret = OB_SUCCESS;
  is_fetch_with_ties_ = is_fetch_with_ties;
  int64_t timeout_ts = GET_PHY_PLAN_CTX(*exec_ctx)->get_timeout_timestamp();
  int64_t effective_sk_cnt = pd_topn_filter_info->effective_sk_cnt_;
  ObPxSqcHandler *sqc_handler = exec_ctx->get_sqc_handler();
  int64_t task_id = 0;
  if (OB_FAIL(exec_ctx->get_datum_access_ctx(datum_access_ctx_))) {
  } else if (nullptr == sqc_handler) {
    // none px plan
  } else {
    task_id = exec_ctx->get_px_task_id();
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObP2PDatahubMsgBase::init(
          pd_topn_filter_info->p2p_dh_id_, px_seq_id, task_id, timeout_ts))) {
  } else if (FALSE_IT(total_sk_cnt_ = pd_topn_filter_info->total_sk_cnt_)) {
  } else if (OB_FAIL(heap_top_datums_.prepare_allocate(effective_sk_cnt))) {
  } else if (OB_FAIL(cells_size_.prepare_allocate(effective_sk_cnt))) {
  } else if (OB_FAIL(compares_.prepare_allocate(effective_sk_cnt))) {
  } else {
    for (int64_t i = 0; i < effective_sk_cnt && OB_SUCC(ret); ++i) {
      // TODO XUNSI: in join scene, if the sort key is the join key of the right table
      // the build_meta_ and filter_meta_ may different, preprae it
      compares_.at(i).build_meta_.cmp_func_ = pd_topn_filter_info->cmp_metas_.at(i).cmp_func_;
      compares_.at(i).build_meta_.obj_meta_.set_meta(pd_topn_filter_info->cmp_metas_.at(i).obj_meta_);
      compares_.at(i).filter_meta_.cmp_func_ = pd_topn_filter_info->cmp_metas_.at(i).cmp_func_;
      compares_.at(i).filter_meta_.obj_meta_.set_meta(pd_topn_filter_info->cmp_metas_.at(i).obj_meta_);
      cells_size_.at(i) = 0;
      compares_.at(i).is_ascending_ = sort_collations->at(i).is_ascending_;
      compares_.at(i).null_pos_ = sort_collations->at(i).null_pos_;
    }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::destroy()
{
  int ret = OB_SUCCESS;
  compares_.reset();
  heap_top_datums_.reset();
  cells_size_.reset();
  allocator_.reset();
  total_sk_cnt_ = 0;
  datum_access_ctx_ = nullptr;
  data_version_ = 0;
  is_fetch_with_ties_ = false;
  is_active_ = false;
  is_empty_ = true;
  is_ready_ = false;
  LOG_DEBUG("[TopN Filter] destroy ObPushDownTopNFilterMsg", K(common::lbt()));
  return OB_SUCCESS;
}

int ObPushDownTopNFilterMsg::assign(const ObP2PDatahubMsgBase &src_msg)
{
  int ret = OB_SUCCESS;
  const auto &source = static_cast<const ObPushDownTopNFilterMsg &>(src_msg);
  if (this == &source) { return OB_SUCCESS; }
  const int64_t count = source.heap_top_datums_.count();
  ObSEArray<Datum, 8> values;
  ObSEArray<int64_t, 8> capacities;
  if (source.compares_.count() != count || source.cells_size_.count() != count) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(values.prepare_allocate(count))) {
  } else if (OB_FAIL(capacities.prepare_allocate(count))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const SqlTypeMeta &meta = source.compares_.at(i).build_meta_.obj_meta_;
    if (OB_FAIL(DatumParameterCodec::copy_native_value_span(source.heap_top_datums_.at(i),
        meta, allocator_, values.at(i)))) {
    } else { ret = DatumParameterCodec::native_span_size(values.at(i), meta, capacities.at(i)); }
  }
  // Reserve every descriptor array before changing any logical member. Fixed
  // array assignment of these value types cannot allocate after these checks.
  if (OB_SUCC(ret)) {
    if (OB_FAIL(compares_.reserve(count))) {
    } else if (OB_FAIL(heap_top_datums_.reserve(count))) {
    } else if (OB_FAIL(cells_size_.reserve(count))) {
    } else if (OB_FAIL(compares_.assign(source.compares_))) {
    } else if (OB_FAIL(heap_top_datums_.assign(values))) {
    } else if (OB_FAIL(cells_size_.assign(capacities))) {
    } else if (OB_FAIL(ObP2PDatahubMsgBase::assign(source))) {
    } else {
      datum_access_ctx_ = source.datum_access_ctx_;
      total_sk_cnt_ = source.total_sk_cnt_;
      data_version_ = source.data_version_;
      is_fetch_with_ties_ = source.is_fetch_with_ties_;
    }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::deep_copy_msg(ObP2PDatahubMsgBase *&dest_msg)
{
  int ret = OB_SUCCESS;
  ObPushDownTopNFilterMsg *new_topn_msg = nullptr;
  ObMemAttr attr("TOPNVECMSG");
  if (OB_FAIL(PX_P2P_DH.alloc_msg<ObPushDownTopNFilterMsg>(attr, new_topn_msg))) {
  } else if (OB_FAIL(new_topn_msg->assign(*this))) {
    ObP2PDatahubMsgBase *failed = new_topn_msg;
    PX_P2P_DH.free_msg(failed);
  } else {
    dest_msg = new_topn_msg;
  }
  return ret;
}

int ObPushDownTopNFilterMsg::merge(ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  ObPushDownTopNFilterMsg &incomming_topn_msg = static_cast<ObPushDownTopNFilterMsg &>(msg);
  if (incomming_topn_msg.heap_top_datums_.count() != heap_top_datums_.count()
      || incomming_topn_msg.compares_.count() != compares_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected heap top datum count", K(incomming_topn_msg.heap_top_datums_.count()),
             K(heap_top_datums_.count()));
  } else if (incomming_topn_msg.is_empty_) {
    /*do nothing*/
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < compares_.count(); ++i) {
      const SqlTypeMeta &left = compares_.at(i).build_meta_.obj_meta_;
      const SqlTypeMeta &right = incomming_topn_msg.compares_.at(i).build_meta_.obj_meta_;
      if (left != right || left.get_scale() != right.get_scale()) { ret = OB_INVALID_ARGUMENT; }
    }
    if (OB_SUCC(ret)) {
      ObSpinLockGuard guard(lock_);
      if (OB_FAIL(merge_heap_top_datums(incomming_topn_msg.heap_top_datums_))) {
      } else if (is_empty_) {
        is_empty_ = false;
      }
    }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::filter_out_data(const ObExpr &expr, ObEvalCtx &ctx,
                                             ObExprTopNFilterContext &filter_ctx, Datum &res)
{
  int ret = OB_SUCCESS;
  Datum *datum = nullptr;
  int cmp_res = 0;
  bool is_filtered = false;
  filter_ctx.total_count_++;
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_UNLIKELY(is_empty_)) {
    res.set_int(0);
    filter_ctx.filter_count_++;
    filter_ctx.check_count_++;
  } else {
    filter_ctx.check_count_++;
    for (int i = 0; OB_SUCC(ret) && i < expr.arg_cnt_; ++i) {
      if (OB_FAIL(expr.args_[i]->eval(ctx, datum))) {
      } else if (datum->is_null()) {
        cmp_res = -1;
        break;
      } else {
        alignas(8) char scratch[64];
        ObDatum legacy;
        if (OB_FAIL(expr.args_[i]->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, sizeof(scratch)))) {
        } else if (OB_FAIL(get_compare_result(i, legacy, cmp_res))) {
        } else if (cmp_res > 0) {
          is_filtered = true;
          break;
        } else if (cmp_res < 0) {
          // the data less than head top data is selected
          break;
        } else {
          // only if the data of the previous column is equal, we need compare the next column
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (!is_filtered) {
        res.set_int(1);
      } else {
        filter_ctx.filter_count_++;
        res.set_int(0);
      }
      filter_ctx.collect_sample_info(is_filtered, 1);
    }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::filter_out_data_batch(
    const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size,
    ObExprTopNFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  if (OB_FAIL(ctx.get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_UNLIKELY(is_empty_)) {
    Datum *results = expr.locate_batch_datums(ctx);
    for (int64_t i = 0; i < batch_size; i++) {
      results[i].set_int(0);
    }
    filter_ctx.total_count_ += batch_size;
    filter_ctx.check_count_ += batch_size;
    filter_ctx.filter_count_ += batch_size;
  } else if (OB_FAIL(do_filter_out_data_batch(expr, ctx, skip, batch_size, filter_ctx))) {
  }
  if (OB_SUCC(ret)) {
    eval_flags.set_all(batch_size);
  }
  return ret;
}

int ObPushDownTopNFilterMsg::update_filter_data(ObChunkDatumStore::StoredRow *store_row,
                                                bool &is_updated)
{
  int ret = OB_SUCCESS;
  is_updated = false;
  // TODO XUNSI: update data for shared topn msg, be care of thread safe
  if (OB_ISNULL(store_row) || store_row->cnt_ < heap_top_datums_.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (check_has_null(store_row)) {
    // do nothing, null will not be updated into filter
  } else if (OB_FAIL(copy_heap_top_datums_from(store_row))) {
  } else {
    is_updated = true;
    int64_t v = ATOMIC_AAF(&data_version_, 1);
  }
  if (OB_SUCC(ret) && is_updated && OB_UNLIKELY(is_empty_)) {
    is_empty_ = false;
  }
  return ret;
}

bool ObPushDownTopNFilterMsg::check_has_null(ObChunkDatumStore::StoredRow *store_row)
{
  int ret = OB_SUCCESS;
  bool has_null = false;
  const StoredDatumCell *incomming_cells = store_row->stored_cells();
  for (int64_t i = 0; i < heap_top_datums_.count() && OB_SUCC(ret); ++i) {
    const ObDatum incomming_datum = StoredDatumCodec::decode_legacy_view(incomming_cells[i]);
    if (incomming_datum.is_null()) {
      has_null = true;
      break;
    }
  }
  return has_null;
}

int ObPushDownTopNFilterMsg::prepare_storage_white_filter_data(
    ObDynamicFilterExecutor &dynamic_filter, ObEvalCtx &eval_ctx, ObRuntimeFilterParams &params,
    bool &is_data_prepared)
{
  int ret = OB_SUCCESS;
  int col_idx = dynamic_filter.get_col_idx();
  if (is_empty_) {
    dynamic_filter.set_filter_action(DynamicFilterAction::FILTER_ALL);
    is_data_prepared = true;
  } else if (heap_top_datums_.at(col_idx).is_null()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(append_storage_value(col_idx, dynamic_filter.get_allocator(), params))) {
  } else {
    int64_t now_data_version = ATOMIC_LOAD(&data_version_);
    dynamic_filter.set_filter_action(DynamicFilterAction::DO_FILTER);
    dynamic_filter.set_filter_val_meta(compares_.at(col_idx).build_meta_.obj_meta_);
    dynamic_filter.set_stored_data_version(now_data_version);
    // caution, see bool ObOpRawExpr::is_white_runtime_filter_expr() const
    // now only one column topn filter can be pushdown as white filter,
    // if these column is the last sort key, means we can filter the data which the compare reuslt is equal.
    ObWhiteFilterOperatorType op_type;
    if (col_idx == total_sk_cnt_ - 1) {
      op_type = compares_.at(col_idx).is_ascending_ ? WHITE_OP_LT : WHITE_OP_GT;
    } else {
      op_type = compares_.at(col_idx).is_ascending_ ? WHITE_OP_LE : WHITE_OP_GE;
    }
    dynamic_filter.get_filter_node().set_op_type(op_type);
    is_data_prepared = true;
  }
  return ret;
}

int ObPushDownTopNFilterMsg::update_storage_white_filter_data(
    ObDynamicFilterExecutor &dynamic_filter, ObRuntimeFilterParams &params, bool &is_update)
{
  int ret = OB_SUCCESS;
  int64_t now_data_version = ATOMIC_LOAD(&data_version_);
  int col_idx = dynamic_filter.get_col_idx();
  if (heap_top_datums_.at(col_idx).is_null()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(append_storage_value(col_idx, dynamic_filter.get_allocator(), params))) {
  } else {
    dynamic_filter.set_stored_data_version(now_data_version);
    is_update = true;
  }
  return ret;
}

// private interface
int ObPushDownTopNFilterMsg::append_storage_value(int64_t col_idx, ObIAllocator &owner,
                                                ObRuntimeFilterParams &params) const
{
  ObDatum legacy;
  int ret = DatumParameterCodec::copy_legacy_value(heap_top_datums_.at(col_idx),
      compares_.at(col_idx).build_meta_.obj_meta_, owner, legacy);
  if (OB_SUCC(ret)) { ret = params.push_back(legacy); }
  return ret;
}

int ObPushDownTopNFilterMsg::compare(int64_t col_idx, const ObDatum &probe, int &cmp_res)
{
  alignas(8) char scratch[64];
  ObDatum bound;
  int ret = DatumParameterCodec::export_native_value(heap_top_datums_.at(col_idx),
      compares_.at(col_idx).build_meta_.obj_meta_, bound, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = compares_.at(col_idx).compare_for_filter(probe, bound, cmp_res, datum_access_ctx_); }
  return ret;
}

int ObPushDownTopNFilterMsg::merge_heap_top_datums(const ObIArray<Datum> &incomming_datums)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_empty_)) {
    if (OB_FAIL(copy_heap_top_datums_from(incomming_datums))) {
    } else { is_empty_ = false; }
  } else {
    int cmp_res = 0;
    for (int64_t i = 0; i < incomming_datums.count() && OB_SUCC(ret); ++i) {
      const ObTopNFilterCompare &comparison = compares_.at(i);
      alignas(8) char incoming_scratch[64], current_scratch[64];
      ObDatum incoming, current;
      cmp_res = 0;
      if (OB_FAIL(DatumParameterCodec::export_native_value(incomming_datums.at(i),
          comparison.build_meta_.obj_meta_, incoming, incoming_scratch, sizeof(incoming_scratch)))) {
      } else if (OB_FAIL(DatumParameterCodec::export_native_value(heap_top_datums_.at(i),
          comparison.build_meta_.obj_meta_, current, current_scratch, sizeof(current_scratch)))) {
      } else if (OB_FAIL(comparison.compare_for_build(incoming, current, cmp_res, datum_access_ctx_))) {
      } else if (cmp_res < 0) { break; }
    }
    if (OB_SUCC(ret) && cmp_res < 0) { ret = copy_heap_top_datums_from(incomming_datums); }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::copy_heap_top_datums_from(const ObIArray<Datum> &incomming_datums)
{
  int ret = OB_SUCCESS;
  const int64_t count = heap_top_datums_.count();
  ObSEArray<Datum, 8> staged;
  ObSEArray<int64_t, 8> capacities;
  ObSEArray<int64_t, 8> sizes;
  if (incomming_datums.count() != count || compares_.count() != count || cells_size_.count() != count) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(staged.prepare_allocate(count))) {
  } else if (OB_FAIL(capacities.prepare_allocate(count))) {
  } else if (OB_FAIL(sizes.prepare_allocate(count))) {
  }
  // Do not overwrite reusable buffers until every cell and allocation succeeds.
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const Datum &incoming = incomming_datums.at(i);
    const SqlTypeMeta &meta = compares_.at(i).build_meta_.obj_meta_;
    int64_t &size = sizes.at(i);
    if (OB_FAIL(DatumParameterCodec::native_span_size(incoming, meta, size))) {
    } else if (size == 0) {
      int64_t position = 0;
      capacities.at(i) = 0;
      ret = DatumParameterCodec::copy_native_span(incoming, meta, nullptr, 0, position, staged.at(i));
    } else {
      int64_t capacity = cells_size_.at(i);
      void *bytes = capacity > 0 && heap_top_datums_.at(i).length() > 0
          ? const_cast<void *>(heap_top_datums_.at(i).get_reference()) : nullptr;
      if (capacity < size || bytes == nullptr) {
        capacity = size * 2; // physical length is bounded by Datum's length field
        bytes = allocator_.alloc(capacity);
        if (bytes == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      }
      if (OB_SUCC(ret)) {
        capacities.at(i) = capacity;
        ret = staged.at(i).set_reference(bytes, incoming.length(), incoming.reference_flags());
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < count; ++i) {
      if (sizes.at(i) > 0) {
        MEMMOVE(const_cast<void *>(staged.at(i).get_reference()),
                incomming_datums.at(i).get_reference(), sizes.at(i));
      }
      heap_top_datums_.at(i) = staged.at(i);
      cells_size_.at(i) = capacities.at(i);
    }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::copy_heap_top_datums_from(ObChunkDatumStore::StoredRow *store_row)
{
  int ret = OB_SUCCESS;
  ObSEArray<Datum, 8> incoming;
  if (OB_FAIL(incoming.prepare_allocate(heap_top_datums_.count()))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < incoming.count(); ++i) {
    const ObDatum legacy = StoredDatumCodec::decode_legacy_view(store_row->stored_cells()[i]);
    ret = DatumParameterCodec::borrow_native_value(legacy, compares_.at(i).build_meta_.obj_meta_, incoming.at(i));
  }
  if (OB_SUCC(ret)) { ret = copy_heap_top_datums_from(incoming); }
  return ret;
}

int ObPushDownTopNFilterMsg::adjust_cell_size()
{
  int ret = OB_SUCCESS;
  if (cells_size_.count() != heap_top_datums_.count() || compares_.count() != heap_top_datums_.count()) {
    ret = OB_DESERIALIZE_ERROR;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < cells_size_.count(); ++i) {
    int64_t bytes = 0;
    if (cells_size_.at(i) < 0) { ret = OB_DESERIALIZE_ERROR;
    } else if (OB_FAIL(DatumParameterCodec::native_span_size(heap_top_datums_.at(i),
        compares_.at(i).build_meta_.obj_meta_, bytes))) {
    } else { cells_size_.at(i) = std::min(cells_size_.at(i), bytes); }
  }
  return ret;
}

int ObPushDownTopNFilterMsg::do_filter_out_data_batch(
    const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size,
    ObExprTopNFilterContext &filter_ctx)
{
  int ret = OB_SUCCESS;
  int64_t filter_count = 0;
  int64_t total_count = 0;
  Datum *results = expr.locate_batch_datums(ctx);
  ObEvalCtx::BatchInfoScopeGuard batch_info_guard(ctx);
  batch_info_guard.set_batch_size(batch_size);
  for (int idx = 0; OB_SUCC(ret) && idx < expr.arg_cnt_; ++idx) {
    if (OB_FAIL(expr.args_[idx]->eval_batch(ctx, skip, batch_size))) {
    }
  }
  if (OB_SUCC(ret)) {
    int cmp_res = 0;
    Datum *datum = nullptr;
    bool is_filtered = false;
    for (int64_t batch_i = 0; OB_SUCC(ret) && batch_i < batch_size; ++batch_i) {
      if (skip.at(batch_i)) {
        continue;
      }
      cmp_res = 0;
      is_filtered = false;
      total_count++;
      batch_info_guard.set_batch_idx(batch_i);
      for (int arg_i = 0; OB_SUCC(ret) && arg_i < expr.arg_cnt_; ++arg_i) {
        datum = &expr.args_[arg_i]->locate_expr_datum(ctx, batch_i);
        if (datum->is_null()) {
          cmp_res = -1;
          break;
        } else {
          alignas(8) char scratch[64];
          ObDatum legacy;
          if (OB_FAIL(expr.args_[arg_i]->get_datum_copy_plan().legacy_view(*datum, legacy, scratch, sizeof(scratch)))) {
          } else { ret = get_compare_result(arg_i, legacy, cmp_res); }
        }
        if (OB_FAIL(ret)) {
        } else if (cmp_res > 0) {
          // the data bigger than head top data should be filterd out.
          filter_count++;
          is_filtered = true;
          break;
        } else if (cmp_res < 0) {
          // the data less than head top data is selected
          break;
        } else {
          // only if the data of the previous column is equal, we need compare the next column
        }
      }
      results[batch_i].set_int(is_filtered ? 0 : 1);
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

} // end namespace sql
} // end namespace oceanbase
