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

#ifndef OB_DTL_TASK_H
#define OB_DTL_TASK_H

#include <stdint.h>
#include <lib/ob_define.h>
#include "lib/container/ob_array_serialization.h"

namespace oceanbase {

// forward declarations
namespace sql { namespace dtl {
class ObDtlChannel;
class ObDtlBasicChannel;
}}  // sql


namespace sql {
namespace dtl {

enum DTL_CHAN_ROLE { DTL_CR_PUSHER, DTL_CR_PULLER };
enum DTL_CHAN_STATE { DTL_CS_RUN, DTL_CS_DRAINED, DTL_CS_UNREGISTER };

struct ObDtlChannelInfo {
  OB_UNIS_VERSION(1);
public:
  uint64_t chid_;
  // Describe role of this channel of this task. A typical task may
  // have some pusher(producer) channels and some puller(consumer)
  // channels whereas task at top only has puller and bottom task only
  // has pusher.
  DTL_CHAN_ROLE role_;
  // no need to serialize
  DTL_CHAN_STATE state_;

  TO_STRING_KV(K_(chid), K_(role), K(state_));
};

class ObDtlChSet
{
  OB_UNIS_VERSION(1);
public:
  static constexpr int64_t MAX_CHANS = 65536; // nearly unlimited
public:
  ObDtlChSet() = default;
  ~ObDtlChSet() = default;
  int reserve(int64_t size) { return ch_info_set_.reserve(size); }
  int add_channel_info(const dtl::ObDtlChannelInfo &info);
  int get_channel_info(int64_t chan_idx, ObDtlChannelInfo &ci) const;
  int64_t count() const { return ch_info_set_.count(); }
  int assign(const ObDtlChSet &other);
  void reset() { ch_info_set_.reset(); }
  common::ObIArray<dtl::ObDtlChannelInfo> &get_ch_info_set() { return ch_info_set_; }
  TO_STRING_KV(K_(ch_info_set));
protected:
  common::ObSEArray<dtl::ObDtlChannelInfo, 12> ch_info_set_;
};

// A single server can still run several local SQC task groups.  Preserve the
// task layout used to map those groups onto DTL channels.
class ObDtlTaskLayout
{
  OB_UNIS_VERSION(1);
public:
  ObDtlTaskLayout()
    : total_task_cnt_(0), prefix_task_counts_()
  {}

  void reset()
  {
    total_task_cnt_ = 0;
    prefix_task_counts_.reset();
  }
  int assign(const ObDtlTaskLayout &other);

  common::ObIArray<int64_t> &get_prefix_task_counts() { return prefix_task_counts_; }
  TO_STRING_KV(K_(total_task_cnt), K_(prefix_task_counts));
public:
  int64_t total_task_cnt_;
  common::ObSEArray<int64_t, 8> prefix_task_counts_;
};

class ObDtlChTotalInfo
{
  OB_UNIS_VERSION(1);
public:
  ObDtlChTotalInfo()
    : start_channel_id_(0), transmit_task_layout_(), receive_task_layout_(),
      channel_count_(0), is_local_shuffle_(false)
  {}
  int assign(const ObDtlChTotalInfo &other);
  void reset()
  {
    start_channel_id_ = 0;
    transmit_task_layout_.reset();
    receive_task_layout_.reset();
    channel_count_ = 0;
  }
  bool is_valid() const
  {
    return transmit_task_layout_.prefix_task_counts_.count()
             <= transmit_task_layout_.total_task_cnt_
        && receive_task_layout_.prefix_task_counts_.count()
             <= receive_task_layout_.total_task_cnt_
        && channel_count_ == transmit_task_layout_.total_task_cnt_
                           * receive_task_layout_.total_task_cnt_;
  }
  TO_STRING_KV(K_(start_channel_id),
              K_(transmit_task_layout),
              K_(receive_task_layout),
              K_(channel_count),
              K_(is_local_shuffle));
public:
  int64_t start_channel_id_;
  ObDtlTaskLayout transmit_task_layout_;
  ObDtlTaskLayout receive_task_layout_;
  int64_t channel_count_;
  bool is_local_shuffle_;
};

}  // dtl
}  // sql
}  // oceanbase


#endif /* OB_DTL_TASK_H */
