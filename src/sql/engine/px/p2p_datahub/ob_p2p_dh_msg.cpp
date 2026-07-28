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

#include "ob_p2p_dh_msg.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_mgr.h"
#include "sql/engine/px/p2p_datahub/ob_runtime_filter_query_range.h"
using namespace oceanbase;
using namespace common;
using namespace sql;

DEFINE_ENUM_FUNC(ObP2PDatahubMsgBase::ObP2PDatahubMsgType, p2p_datahub_msg_type,
                 P2P_DATAHUB_MSG_TYPE, ObP2PDatahubMsgBase::);

OB_SERIALIZE_MEMBER(ObP2PDatahubMsgBase,
    trace_id_, p2p_datahub_id_, px_sequence_id_,
    task_id_, timeout_ts_, msg_type_,
    is_active_, is_empty_);

int ObP2PDatahubMsgBase::init(int64_t p2p_dh_id,
    int64_t px_sequence_id, int64_t task_id, int64_t timeout_ts)
{
  int ret = OB_SUCCESS;
  trace_id_ = *ObCurTraceId::get_trace_id();
  p2p_datahub_id_ = p2p_dh_id;
  px_sequence_id_ = px_sequence_id;
  task_id_ = task_id;
  
  timeout_ts_ = timeout_ts;
  is_active_ = true;
  is_ready_ = false;
  is_empty_ = true;
  
  allocator_.set_label("ObP2PDHMsg");
  return ret;
}

int ObP2PDatahubMsgBase::assign(const ObP2PDatahubMsgBase &msg)
{
  int ret = OB_SUCCESS;
  trace_id_ = msg.get_trace_id();
  p2p_datahub_id_ = msg.get_p2p_datahub_id();
  px_sequence_id_ = msg.get_px_seq_id();
  task_id_ = msg.get_task_id();
  
  timeout_ts_ = msg.get_timeout_ts();
  msg_type_ = msg.get_msg_type();
  is_active_ = msg.is_active();
  is_ready_ = msg.check_ready();
  is_empty_ = msg.is_empty();
  
  allocator_.set_label("ObP2PDHMsg");
  return ret;
}

template <>
int ObP2PDatahubMsgBase::proc_filter_empty<IntegerFixedVec>(IntegerFixedVec *res_vec,
                                                            const ObBitVector &skip,
                                                            const EvalBound &bound,
                                                            int64_t &total_count,
                                                            int64_t &filter_count)
{
  int ret = OB_SUCCESS;
  uint64_t *data = reinterpret_cast<uint64_t *>(res_vec->get_data());
  MEMSET(data + bound.start(), 0, (bound.range_size() * res_vec->get_length(0)));

  int64_t valid_cnt = bound.range_size() - skip.accumulate_bit_cnt(bound);
  total_count += valid_cnt;
  filter_count += valid_cnt;
  return ret;
}

template <>
int ObP2PDatahubMsgBase::proc_filter_empty<IntegerUniVec>(IntegerUniVec *res_vec,
                                                          const ObBitVector &skip,
                                                          const EvalBound &bound,
                                                          int64_t &total_count,
                                                          int64_t &filter_count)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObBitVector::flip_foreach(
          skip, bound, [&](int64_t idx) __attribute__((always_inline)) {
            res_vec->set_int(idx, 0);
            ++filter_count;
            ++total_count;
            return OB_SUCCESS;
          }))) {
    LOG_WARN("fail to do for each operation", K(ret));
  }
  return ret;
}

int ObP2PDatahubMsgBase::preset_not_match(IntegerFixedVec *res_vec, const EvalBound &bound)
{
  int ret = OB_SUCCESS;
  uint64_t *data = reinterpret_cast<uint64_t *>(res_vec->get_data());
  MEMSET(data + bound.start(), 0, (bound.range_size() * res_vec->get_length(0)));
  return ret;
}

int ObP2PDatahubMsgBase::fill_empty_query_range(const ObPxQueryRangeInfo &query_range_info,
                             common::ObIAllocator &allocator, ObNewRange &query_range)
{
  int ret = OB_SUCCESS;
  query_range.table_id_ = query_range_info.table_id_;

  ObObj *start = NULL;
  ObObj *end = NULL;
  int64_t range_column_cnt = query_range_info.range_column_cnt_;
  if (OB_ISNULL(start = static_cast<ObObj *>(
                    allocator.alloc(sizeof(ObObj) * range_column_cnt)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory for start_obj failed", K(ret));
  } else if (OB_ISNULL(end = static_cast<ObObj *>(
                           allocator.alloc(sizeof(ObObj) * range_column_cnt)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc memory for end_obj failed", K(ret));
  } else {
    // fill all columns with (max, min)
    for (int64_t i = 0; i < range_column_cnt; ++i) {
      new (start + i) ObObj();
      new (end + i) ObObj();
      (start + i)->set_max_value();
      (end + i)->set_min_value();
    }
    ObRowkey start_key(start, range_column_cnt);
    ObRowkey end_key(end, range_column_cnt);
    query_range.start_key_ = start_key;
    query_range.end_key_ = end_key;
  }
  return ret;
}

ObP2PDatahubMsgGuard::ObP2PDatahubMsgGuard(ObP2PDatahubMsgBase *msg) : msg_(msg)
{
  // one for dh map hold msg and one for we use msg to reg dm
  msg->inc_ref_count(2);
}

ObP2PDatahubMsgGuard::~ObP2PDatahubMsgGuard()
{
  dec_msg_ref_count();
}

void ObP2PDatahubMsgGuard::release()
{
  msg_ = nullptr;
}

void ObP2PDatahubMsgGuard::dec_msg_ref_count()
{
  if (OB_NOT_NULL(msg_)) {
    msg_->dec_ref_count();
  }
}
