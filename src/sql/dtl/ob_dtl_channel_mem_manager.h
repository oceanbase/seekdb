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

#ifndef OB_DTL_CHANNEL_MEM_MANAGER_H
#define OB_DTL_CHANNEL_MEM_MANAGER_H

#include "lib/queue/ob_lighty_queue.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/ob_fifo_allocator.h"
#include "sql/dtl/ob_dtl_linked_buffer.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/alloc/alloc_func.h"
#include "share/config/ob_server_config.h"
#include "src/sql/dtl/ob_dtl_mem_manager.h"

namespace oceanbase {
namespace sql {
namespace dtl {

//class ObDtlLinkedBuffer;

class ObDtlMemManager;
class ObDtlChannelMemManager
{
public:
  explicit ObDtlChannelMemManager(ObDtlMemManager &mem_mgr);
  virtual ~ObDtlChannelMemManager() { destroy(); }

  int init();
  void destroy();

public:
  ObDtlLinkedBuffer *alloc(int64_t chid, int64_t size);
  int free(ObDtlLinkedBuffer *buf, bool auto_free = true);

  void set_seqno(int64_t seqno) { seqno_ = seqno; }
  int64_t get_seqno() { return seqno_; }
  TO_STRING_KV(K_(size_per_buffer));

  OB_INLINE int64_t get_alloc_cnt() { return alloc_cnt_; }
  OB_INLINE int64_t get_free_cnt() { return free_cnt_; }
  OB_INLINE int64_t get_free_queue_length() { return free_queue_.size(); }

  OB_INLINE int64_t get_real_alloc_cnt() { return real_alloc_cnt_; }
  OB_INLINE int64_t get_real_free_cnt() { return real_free_cnt_; }

  OB_INLINE void increase_alloc_cnt() { ATOMIC_INC(&alloc_cnt_); }
  OB_INLINE void increase_free_cnt() { ATOMIC_INC(&free_cnt_); }


  int64_t get_total_memory_size() { return allocator_.used(); }

  int get_max_mem_percent();
  void update_max_memory_percent();
  int64_t get_buffer_size() { return size_per_buffer_; }
  int auto_free_on_time(int64_t cur_max_reserve_count);

  OB_INLINE int64_t queue_cnt() { return free_queue_.size(); }

private:
  int64_t get_used_memory_size();
  int64_t get_max_dtl_memory_size();
  int64_t get_max_memory_limit_size();
  void real_free(ObDtlLinkedBuffer *buf);
private:
  int64_t size_per_buffer_;
  int64_t seqno_;
  static const int64_t MAX_CAPACITY = 16;
  common::ObLightyQueue free_queue_;
  common::ObFIFOAllocator allocator_;

  int64_t pre_alloc_cnt_;
  double max_mem_percent_;

  // some statistics
  int64_t alloc_cnt_;
  int64_t free_cnt_;

  int64_t real_alloc_cnt_;
  int64_t real_free_cnt_;
  ObDtlMemManager &mem_mgr_;
  int64_t mem_used_;
  int64_t last_update_memory_time_;
};

OB_INLINE int64_t ObDtlChannelMemManager::get_max_dtl_memory_size()
{
  if (0 == max_mem_percent_) {
    get_max_mem_percent();
  }
  return get_max_memory_limit_size() * max_mem_percent_ / 100;
}

OB_INLINE int64_t ObDtlChannelMemManager::get_max_memory_limit_size()
{
  static const int64_t DTL_MEMORY_PERCENTAGE = 140;
  const int64_t memory_budget = lib::get_memory_budget();
  const int64_t quotient = memory_budget / 100;
  const int64_t remainder_charge =
      memory_budget % 100 * DTL_MEMORY_PERCENTAGE / 100;
  return quotient > (INT64_MAX - remainder_charge) / DTL_MEMORY_PERCENTAGE
      ? INT64_MAX
      : quotient * DTL_MEMORY_PERCENTAGE + remainder_charge;
}

OB_INLINE void ObDtlChannelMemManager::update_max_memory_percent()
{
  size_per_buffer_ = GCONF.dtl_buffer_size;
  get_max_mem_percent();
}

} // dtl
} // sql
} // oceanbase

#endif /* OB_DTL_CHANNEL_MEM_MANAGER_H */
