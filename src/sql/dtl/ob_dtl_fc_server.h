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

#ifndef OB_DTL_FC_SERVER_H
#define OB_DTL_FC_SERVER_H

#include "lib/utility/ob_unify_serialize.h"
#include "lib/atomic/ob_atomic.h"
#include "sql/dtl/ob_dtl_flow_control.h"
#include "sql/dtl/ob_dtl_linked_buffer.h"
#include "sql/dtl/ob_dtl_tenant_mem_manager.h"
#include "sql/dtl/ob_dtl_local_first_buffer_manager.h"
#include "lib/list/ob_dlist.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_mutex.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_tracepoint.h"
#include "share/config/ob_server_config.h"

namespace oceanbase {
namespace sql {
namespace dtl {

class ObTenantDfc
{
public:
  ObTenantDfc(uint64_t tenant_id);
  virtual ~ObTenantDfc();
public:
  static int mtl_new(ObTenantDfc *&tenant_dfc);
  static int mtl_init(ObTenantDfc *&tenant_dfc);
  static void mtl_destroy(ObTenantDfc *&tenant_dfc);

  OB_INLINE virtual int64_t get_max_size_per_channel();

  OB_INLINE virtual bool need_block(ObDtlFlowControl *dfc);
  OB_INLINE virtual bool can_unblock(ObDtlFlowControl *dfc);

  OB_INLINE virtual void increase(int64_t size);
  OB_INLINE virtual void decrease(int64_t size);

  OB_INLINE virtual void increase_blocked_channel_cnt();
  OB_INLINE virtual void decrease_blocked_channel_cnt(int64_t unblock_cnt);

  int register_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch);
  int unregister_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch);

  int deregister_dfc(ObDtlFlowControl &dfc);

public:
  // for cache first msg and release first msg
  int64_t get_hash_value(int64_t chid);
  void check_dtl(uint64_t tenant_id);
  // for block and unblock
  int enforce_block(ObDtlFlowControl *dfc, int64_t ch_idx);

  int block_tenant_dfc(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size);
  int unblock_tenant_dfc(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size);
  int try_unblock_tenant_dfc(ObDtlFlowControl *dfc, int64_t ch_idx);
  int unblock_channel(ObDtlFlowControl *dfc, int64_t ch_idx);
  int unblock_channels(ObDtlFlowControl *dfc);

  OB_INLINE virtual void increase_channel_cnt(int64_t n_ch);
  OB_INLINE virtual void decrease_channel_cnt(int64_t n_ch);

  virtual void calc_max_buffer(int64_t max_parallel_cnt);
  uint64_t get_tenant_id() { return tenant_id_; }

  int64_t get_current_buffer_used() { return tenant_dfc_.get_used(); }
  int64_t get_current_blocked_cnt() { return tenant_dfc_.get_blocked_cnt(); }
  int64_t get_current_total_blocked_cnt() { return (ATOMIC_LOAD(&blocked_dfc_cnt_)); }
  int64_t get_current_buffer_cnt() { return tenant_dfc_.get_total_buffer_cnt(); }
  int64_t get_max_parallel() { return max_parallel_cnt_; }
  int64_t get_max_blocked_buffer_size() { return max_blocked_buffer_size_; }
  int64_t get_max_buffer_size() { return max_buffer_size_; }
  int64_t get_accumulated_blocked_cnt() { return tenant_dfc_.get_accumulated_blocked_cnt(); }
  int64_t get_channel_cnt() { return (ATOMIC_LOAD(&channel_total_cnt_)); }

  OB_INLINE ObDtlTenantMemManager *get_tenant_mem_manager() { return &tenant_mem_mgr_; }
private:
  static int init_channel_mem_manager();
  int clean_on_timeout();
  void check_dtl_buffer_size();

private:
  // global data flow control
  ObDtlFlowControl tenant_dfc_;
  uint64_t tenant_id_;
  int64_t blocked_dfc_cnt_;
  int64_t channel_total_cnt_;

  int64_t max_parallel_cnt_;
  // Exceeds this value, then block receives the msg
  int64_t max_blocked_buffer_size_;
  // Exceeding this value, data will not be buffered
  int64_t max_buffer_size_;
  static const int64_t THRESHOLD_SIZE = 2097152;
  static const int64_t MAX_BUFFER_CNT = 3;
  static const int64_t MAX_BUFFER_FACTOR = 2;
  static const int64_t DFC_CPU_RATIO = 10;
  // static const int64_t THRESHOLD_MAX_BUFFER_SIZE = 2097152;
  // // Suppose Max buffer size = MAX_BUFFER_FACTOR * max_blocked_buffer_size_
  // static const int64_t MAX_BUFFER_FACTOR = 2;
  const double OVERSOLD_RATIO = 0.8;

  ObDtlTenantMemManager tenant_mem_mgr_;
public:
  TO_STRING_KV(K_(tenant_id), K_(blocked_dfc_cnt), K_(channel_total_cnt));
};

class ObDfcServer
{
public:
  ObDfcServer()
  {}
  ~ObDfcServer() { destroy(); }

  int init();
  void destroy();

  int block_on_increase_size(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size);
  int unblock_on_decrease_size(ObDtlFlowControl *dfc, int64_t ch_idx, int64_t size);
  int unblock_channels(ObDtlFlowControl *dfc);
  int unblock_channel(ObDtlFlowControl *dfc, int64_t ch_idx);

  int register_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch);
  int unregister_dfc_channel(ObDtlFlowControl &dfc, ObDtlChannel* ch);

  int register_dfc(ObDtlFlowControl &dfc);
  int deregister_dfc(ObDtlFlowControl &dfc);

  ObDtlTenantMemManager *get_tenant_mem_manager(int64_t tenant_id);
private:
  int get_current_tenant_dfc(uint64_t tenant_id, ObTenantDfc *&tenant_dfc);
};

OB_INLINE int64_t ObTenantDfc::get_max_size_per_channel()
{
  // Here we do not take atomically, because it is just an estimation, and there seems to be no need to guarantee atomicity
  int64_t tmp_total_cnt = channel_total_cnt_;
  int64_t tmp_blocked_dfc_cnt = blocked_dfc_cnt_;
  // Concurrency inconsistency may result in less than 0
  tmp_blocked_dfc_cnt = tmp_total_cnt <= tmp_blocked_dfc_cnt ? tmp_total_cnt : tmp_blocked_dfc_cnt;
  int64_t tmp_unblock_dfc_cnt = tmp_total_cnt - tmp_blocked_dfc_cnt;
  tmp_total_cnt = 0 == tmp_total_cnt ? 1 : tmp_total_cnt;
  int64_t max_parallel_cnt = (0 == max_parallel_cnt_) ? 1 : max_parallel_cnt_;
  // Here is to limit a channel from using too many buffers
  int64_t tmp_max_size_per_channel = max_blocked_buffer_size_ / max_parallel_cnt * 2;
  int64_t max_size_per_channel = max_blocked_buffer_size_ / tmp_total_cnt * 8 / 10;
  if (0 != tmp_total_cnt && 0 != tmp_blocked_dfc_cnt) {
    max_size_per_channel = max_blocked_buffer_size_ * (1 + tmp_unblock_dfc_cnt * 8 / 10 / tmp_blocked_dfc_cnt) / tmp_total_cnt;
  }
  if (max_size_per_channel > tmp_max_size_per_channel) {
    max_size_per_channel = tmp_max_size_per_channel;
  }
  return max_size_per_channel;
}
// Here is the assumption that the entire worker has reached a balanced state, due to the worker number limiting the dfc quantity, so each op's dfc is considered to be related to max_dop
// That is, when receive processing happens simultaneously, only the dfc of max_dop needs buffering; buffering at other times is redundant, rather than looking at it from a channel perspective, otherwise the number of channels required would be related to the square of the channels, here we do not assume this
OB_INLINE bool ObTenantDfc::need_block(ObDtlFlowControl *dfc)
{
  // first judge whether current dfc need block, reduce concurrent effects
  // then judge whether global server need block
  bool need_block = false;
  if (nullptr != dfc && dfc->need_block()) {
    int64_t max_size_per_dfc = get_max_size_per_channel() * dfc->get_channel_count();
    need_block = dfc->get_used() >= max_size_per_dfc || tenant_dfc_.get_used() >= max_buffer_size_;
  #ifdef ERRSIM
    int ret = common::OB_SUCCESS;
    ret = OB_E(EventTable::EN_FORCE_DFC_BLOCK) ret;
    need_block = (common::OB_SUCCESS != ret) ? true : need_block;
    SQL_DTL_LOG(TRACE, "trace block", K(need_block), K(ret));
    ret = common::OB_SUCCESS;
  #endif
  }
  return need_block;
}

OB_INLINE bool ObTenantDfc::can_unblock(ObDtlFlowControl *dfc)
{
  bool can_unblock = false;
  // If there is one channel, continuously receiving relatively large msg, such as block, will it cause everything to be blocked, only waiting for this sql to finish, other sessions will continue
  if (nullptr != dfc && dfc->is_block()) {
    // Here are several strategies for unblocking operation
    // 1 directly send unblocking msg according to the current dfc status
    // 2 If overselling, then send unblock msg based on global usage situation
    // Note that if both the current dfc needs to meet the conditions and the global usage situation needs to be satisfied, then when dfc_server meets the conditions, it sends an unblocking status to all dfc
    // Otherwise it will lead to a situation where a certain channel, having reached its own unblock condition previously, did not send an unblocking message, resulting in no further messages being processed for that channel, thus leading to deadlock by never sending an unblocking message again
    // So dfc server must notify all current dfc of unblocking when the unblocking condition is met, I feel this is a bit redundant
    // Now only satisfying one of them will notify unblocking
    int64_t max_size_per_dfc = get_max_size_per_channel() * dfc->get_channel_count();
    can_unblock = dfc->can_unblock() ||
      (dfc->get_used() <= max_size_per_dfc / 2 && tenant_dfc_.get_used() < max_buffer_size_);
  }
  return can_unblock;
}

OB_INLINE void ObDtlCacheBufferInfo::set_buffer(ObDtlLinkedBuffer *buffer)
{
  buffer_ = buffer;
  if (nullptr != buffer) {
    ts_ = buffer->timeout_ts();
  }
}

OB_INLINE void ObTenantDfc::increase(int64_t size)
{
  tenant_dfc_.increase(size);
}

OB_INLINE void ObTenantDfc::decrease(int64_t size)
{
  tenant_dfc_.decrease(size);
}

OB_INLINE void ObTenantDfc::increase_channel_cnt(int64_t n_ch)
{
  ATOMIC_AAF(&channel_total_cnt_, n_ch);
}

OB_INLINE void ObTenantDfc::decrease_channel_cnt(int64_t n_ch)
{
  ATOMIC_SAF(&channel_total_cnt_, n_ch);
}

OB_INLINE void ObTenantDfc::increase_blocked_channel_cnt()
{
  ATOMIC_INC(&blocked_dfc_cnt_);
  tenant_dfc_.increase_blocked_cnt(1);
}

OB_INLINE void ObTenantDfc::decrease_blocked_channel_cnt(int64_t unblock_cnt)
{
  ATOMIC_SAF(&blocked_dfc_cnt_, unblock_cnt);
  tenant_dfc_.decrease_blocked_cnt(unblock_cnt);
}

} // dtl
} // sql
} // oceanbase

#endif /* OB_DTL_FC_SERVER_H */
