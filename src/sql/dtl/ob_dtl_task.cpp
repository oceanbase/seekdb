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

#define USING_LOG_PREFIX SQL_DTL

#include "ob_dtl_task.h"
#include "sql/dtl/ob_dtl.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace sql {
namespace dtl {

OB_SERIALIZE_MEMBER(ObDtlChannelInfo, chid_, role_);
OB_SERIALIZE_MEMBER(ObDtlChSet, ch_info_set_);
OB_SERIALIZE_MEMBER(ObDtlTaskLayout, total_task_cnt_, prefix_task_counts_);
OB_SERIALIZE_MEMBER(ObDtlChTotalInfo, start_channel_id_, transmit_task_layout_,
                    receive_task_layout_, channel_count_, is_local_shuffle_);


int ObDtlChSet::add_channel_info(const ObDtlChannelInfo &info)
{
  int ret = OB_SUCCESS;
  if (ch_info_set_.count() >= MAX_CHANS) {
    ret = OB_SIZE_OVERFLOW;
    LOG_WARN("chan set full", "count", ch_info_set_.count(), K(ret));
  } else if (OB_FAIL(ch_info_set_.push_back(info))) {
    LOG_WARN("fail push back channel info", K(info), K(ret));
  }
  return ret;
}

int ObDtlChSet::get_channel_info(int64_t chan_idx, ObDtlChannelInfo &ci) const
{
  int ret = OB_SUCCESS;
  if (chan_idx < 0 || chan_idx >= ch_info_set_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid idx", K(chan_idx), "count", ch_info_set_.count());
  } else {
    ci = ch_info_set_.at(chan_idx);
  }
  return ret;
}

int ObDtlChSet::assign(const ObDtlChSet &other)
{
  int ret = OB_SUCCESS;
  ch_info_set_.reuse();
  if (0 < other.ch_info_set_.count()) {
    if (OB_FAIL(ch_info_set_.prepare_allocate(other.ch_info_set_.count()))) {
      LOG_WARN("failed to prepare alloc", K(ret));
    } else {
      for (int64_t i = 0; i < other.ch_info_set_.count(); ++i) {
        ch_info_set_.at(i) = other.ch_info_set_.at(i);
      }
    }
  }
  return ret;
}

int ObDtlTaskLayout::assign(const ObDtlTaskLayout &other)
{
  int ret = OB_SUCCESS;
  total_task_cnt_ = other.total_task_cnt_;
  prefix_task_counts_.reuse();
  if (0 < other.prefix_task_counts_.count()) {
    OZ(prefix_task_counts_.prepare_allocate(other.prefix_task_counts_.count()));
    for (int64_t i = 0; i < other.prefix_task_counts_.count() && OB_SUCC(ret); ++i) {
      prefix_task_counts_.at(i) = other.prefix_task_counts_.at(i);
    }
  }
  return ret;
}

int ObDtlChTotalInfo::assign(const ObDtlChTotalInfo &other)
{
  int ret = OB_SUCCESS;
  start_channel_id_ = other.start_channel_id_;
  channel_count_ = other.channel_count_;
  OZ(transmit_task_layout_.assign(other.transmit_task_layout_));
  OZ(receive_task_layout_.assign(other.receive_task_layout_));
  is_local_shuffle_ = other.is_local_shuffle_;
  return ret;
}


}  // dtl
}  // sql
}  // oceanbase
