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

#ifndef _SQL_ENGINE_JOIN_OB_JOIN_FILTER_OP_H
#define _SQL_ENGINE_JOIN_OB_JOIN_FILTER_OP_H 1

#include "lib/lock/ob_spin_lock.h"
#include "lib/container/ob_se_array.h"
#include "sql/optimizer/ob_table_location.h"
#include "sql/engine/ob_operator.h"
#include "sql/engine/px/ob_px_bloom_filter.h"
#include "sql/engine/px/ob_px_sqc_proxy.h"
#include "sql/engine/px/ob_px_basic_info.h"
#include "sql/dtl/ob_dtl_flow_control.h"
#include "sql/dtl/ob_dtl_channel_loop.h"
#include "sql/dtl/ob_op_metric.h"
#include "sql/engine/px/p2p_datahub/ob_runtime_filter_msg.h"
#include "sql/engine/px/p2p_datahub/ob_runtime_filter_query_range.h"


namespace oceanbase
{
namespace sql
{

class ObPxSQCProxy;
class ObJoinFilterOp;

class SharedJoinFilterConstructor
{
public:
  inline bool try_acquire_constructor() { return !ATOMIC_CAS(&is_acquired_, false, true); }
  inline bool try_release_constructor() { return ATOMIC_CAS(&is_acquired_, true, false); }
  int init();
  int reset_for_rescan();
  int wait_constructed(ObOperator *join_filter_op, ObRFBloomFilterMsg *bf_msg);
  int notify_constructed();
private:
  static constexpr uint64_t COND_WAIT_TIME_USEC = 100; // 100 us
  ObThreadCond cond_;
  bool is_acquired_{false};
  bool is_bloom_filter_constructed_{false};
} CACHE_ALIGNED;

struct ObJoinFilterShareInfo
{
  ObJoinFilterShareInfo()
      : unfinished_count_ptr_(0), ch_provider_ptr_(0), release_ref_ptr_(0), filter_ptr_(0),
        shared_msgs_(0), shared_jf_constructor_(nullptr)
  {}
  uint64_t unfinished_count_ptr_; // send_filter reference count, initial value is the number of workers
  uint64_t ch_provider_ptr_; // sqc_proxy, due to serialization requirements, use a pointer representation.
  uint64_t release_ref_ptr_; // Release memory reference count, initial value is the number of workers.
  uint64_t filter_ptr_;   // This pointer will be shared memory for PX JOIN FILTER CREATE operator.
  uint64_t shared_msgs_;  //sqc-shared dh msgs
  union {
    SharedJoinFilterConstructor *shared_jf_constructor_;
    uint64_t ser_shared_jf_constructor_;
  };
  OB_UNIS_VERSION_V(1);
public:
  TO_STRING_KV(KP(unfinished_count_ptr_), KP(ch_provider_ptr_), KP(release_ref_ptr_), KP(filter_ptr_), K(shared_msgs_));
};

struct ObJoinFilterRuntimeConfig
{
  OB_UNIS_VERSION_V(1);
public:
  TO_STRING_KV(K_(bloom_filter_ratio), K_(runtime_filter_wait_time_ms),
               K_(runtime_filter_max_in_num), K_(runtime_bloom_filter_max_size));
public:
  ObJoinFilterRuntimeConfig() :
      bloom_filter_ratio_(0.0),
      runtime_filter_wait_time_ms_(0),
      runtime_filter_max_in_num_(0),
      runtime_bloom_filter_max_size_(0),
      px_message_compression_(false) {}
  double bloom_filter_ratio_;
  int64_t runtime_filter_wait_time_ms_;
  int64_t runtime_filter_max_in_num_;
  int64_t runtime_bloom_filter_max_size_;
  bool px_message_compression_;
};

class ObJoinFilterOpInput : public ObOpInput
{
  OB_UNIS_VERSION_V(1);
public:
  ObJoinFilterOpInput(ObExecContext &ctx, const ObOpSpec &spec)
    : ObOpInput(ctx, spec),
      share_info_(),
      task_id_(0),
      px_sequence_id_(OB_INVALID_ID),
      bf_idx_at_sqc_proxy_(-1),
      config_()
  {}
  virtual ~ObJoinFilterOpInput() {}

  virtual void reset() override
  {
    auto &ctx = exec_ctx_;
    auto &spec = spec_;
    void *ptr = this;
    this->~ObJoinFilterOpInput();
    new (ptr) ObJoinFilterOpInput(ctx, spec);
  }
  bool check_release();
  // Each worker shares the same sqc_proxy
  void set_sqc_proxy(ObPxSQCProxy &sqc_proxy)
  {
    share_info_.ch_provider_ptr_ = reinterpret_cast<uint64_t>(&sqc_proxy);
  }
  ObJoinFilterOp *get_filter()
  {
    return reinterpret_cast<ObJoinFilterOp *>(share_info_.filter_ptr_);
  }
  int init_share_info(
      const ObJoinFilterSpec &spec,
      ObExecContext &ctx,
      int64_t task_count);
  int init_shared_msgs(const ObJoinFilterSpec &spec,
      ObExecContext &ctx);
  static int construct_msg_details(const ObJoinFilterSpec &spec,
      ObJoinFilterRuntimeConfig &config,
      ObP2PDatahubMsgBase &msg, int64_t estimated_rows);
  void set_task_id(int64_t task_id)  { task_id_ = task_id; }
  void set_px_sequence_id(int64_t id) { px_sequence_id_ = id; }
  int64_t get_px_sequence_id() { return px_sequence_id_; }
  int load_runtime_config(const ObJoinFilterSpec &spec, ObExecContext &ctx);
public:
  ObJoinFilterShareInfo share_info_; // bloom filter shared memory
  int64_t task_id_; // In the pwj join scenario, this task_id will be used as bf_key
  int64_t px_sequence_id_;
  int64_t bf_idx_at_sqc_proxy_;
  ObJoinFilterRuntimeConfig config_;
  DISALLOW_COPY_AND_ASSIGN(ObJoinFilterOpInput);
};

struct ObRuntimeFilterInfo
{
  OB_UNIS_VERSION_V(1);
public:
  TO_STRING_KV(K_(filter_expr_id), K_(p2p_datahub_id), K_(filter_shared_type));
public:
  ObRuntimeFilterInfo() :
      filter_expr_id_(OB_INVALID_ID),
      p2p_datahub_id_(OB_INVALID_ID),
      filter_shared_type_(INVALID_TYPE),
      dh_msg_type_(ObP2PDatahubMsgBase::ObP2PDatahubMsgType::NOT_INIT)
      {}
  virtual ~ObRuntimeFilterInfo() = default;
  void reset () {
    filter_expr_id_ = OB_INVALID_ID;
    p2p_datahub_id_ = OB_INVALID_ID;
    dh_msg_type_ = ObP2PDatahubMsgBase::ObP2PDatahubMsgType::NOT_INIT;
  }
  int64_t filter_expr_id_;
  int64_t p2p_datahub_id_;
  JoinFilterSharedType filter_shared_type_;
  ObP2PDatahubMsgBase::ObP2PDatahubMsgType dh_msg_type_;
};

class ObJoinFilterSpec : public ObOpSpec
{
  OB_UNIS_VERSION_V(2);
public:
  ObJoinFilterSpec(common::ObIAllocator &alloc, const ObPhyOperatorType type);

  INHERIT_TO_STRING_KV("op_spec", ObOpSpec, K_(mode), K_(filter_id), K_(filter_len), K_(rf_infos),
                       K_(bloom_filter_ratio));

  inline void set_mode(JoinFilterMode mode) { mode_ = mode; }
  inline JoinFilterMode get_mode() const { return mode_; }
  inline void set_filter_id(int64_t id) { filter_id_ = id; }
  inline int64_t get_filter_id() const { return filter_id_; }
  inline void set_filter_length(int64_t len) { filter_len_ = len; }
  inline int64_t get_filter_length() const { return filter_len_; }
  inline ObIArray<ObExpr*> &get_exprs() { return join_keys_; }
  inline bool is_create_mode() const { return JoinFilterMode::CREATE == mode_; }
  inline bool is_use_mode() const { return JoinFilterMode::USE == mode_; }
  inline bool is_partition_filter() const
  { return filter_shared_type_ == JoinFilterSharedType::NONSHARED_PARTITION_JOIN_FILTER ||
           filter_shared_type_ == JoinFilterSharedType::SHARED_PARTITION_JOIN_FILTER; };
  inline void set_shared_filter_type(JoinFilterSharedType type) { filter_shared_type_ = type; }
  inline bool is_shared_join_filter() const
  { return filter_shared_type_ == JoinFilterSharedType::SHARED_JOIN_FILTER ||
           filter_shared_type_ == JoinFilterSharedType::SHARED_PARTITION_JOIN_FILTER; }

  JoinFilterMode mode_;
  int64_t filter_id_;
  int64_t filter_len_;
  ExprFixedArray join_keys_;
  common::ObHashFuncs hash_funcs_;
  ObCmpFuncs cmp_funcs_;
  JoinFilterSharedType filter_shared_type_;
  ObExpr *calc_tablet_id_expr_;
  common::ObFixedArray<ObRuntimeFilterInfo, common::ObIAllocator> rf_infos_;
  common::ObFixedArray<bool, common::ObIAllocator> need_null_cmp_flags_;
  bool is_shuffle_;
  int64_t each_group_size_;
  ObPxQueryRangeInfo px_query_range_info_;
  int64_t bloom_filter_ratio_;
  int64_t send_bloom_filter_size_; // how many KB a piece bloom filter has
  int64_t rf_max_wait_time_ms_{0};
};

class ObJoinFilterOp : public ObOperator
{
public:
  ObJoinFilterOp(ObExecContext &exec_ctx, const ObOpSpec &spec, ObOpInput *input);
  virtual ~ObJoinFilterOp();

  virtual int inner_open() override;
  virtual int inner_close() override;
  virtual int inner_rescan() override;
  virtual int inner_get_next_row() override;
  virtual int inner_get_next_batch(const int64_t max_row_cnt) override; // for batch
  virtual int inner_drain_exch() override;
  int do_drain_exch() override;
  virtual void destroy() override {
    lucky_devil_champions_.reset();
    local_rf_msgs_.reset();
    shared_rf_msgs_.reset();
    ObOperator::destroy();
  }
private:
  bool is_valid();
  int insert_by_row();
  int insert_by_row_batch(const ObBatchRows *child_brs);
  int calc_expr_values(ObDatum *&datum);
  int do_create_filter_rescan();
  int do_use_filter_rescan();
  int try_send_join_filter();
  int try_merge_join_filter();
  int update_plan_monitor_info();
  int open_join_filter_create();
  int open_join_filter_use();
  int join_filter_create_get_next_batch(const int64_t max_row_cnt);
  int join_filter_use_get_next_batch(const int64_t max_row_cnt);
  int close_join_filter_create();
  int close_join_filter_use();
  int init_shared_msgs_from_input();
  int init_local_msg_from_shared_msg(ObP2PDatahubMsgBase &msg);
  int release_local_msg();
  int release_shared_msg();
private:
  static const int64_t ADAPTIVE_BF_WINDOW_ORG_SIZE = 4096;
  static constexpr double ACCEPTABLE_FILTER_RATE = 0.98;
public:
  ObArray<ObP2PDatahubMsgBase *> shared_rf_msgs_; // sqc level share
  ObArray<ObP2PDatahubMsgBase *> local_rf_msgs_;
  uint64_t *join_filter_hash_values_;
  ObArray<bool> lucky_devil_champions_;

  bool has_sent_runtime_filter_{false};
};

};

}


#endif /* _SQL_ENGINE_JOIN_OB_JOIN_FILTER_OP_H */
