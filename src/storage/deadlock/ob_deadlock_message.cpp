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

#include "ob_deadlock_message.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

using namespace common;

OB_SERIALIZE_MEMBER(ObDeadLockCollectInfoMessage, dest_key_, collected_info_);
OB_SERIALIZE_MEMBER(ObDeadLockNotifyParentMessage, parent_addr_, parent_key_,
                                                   src_addr_, src_key_, action_);

int ObDeadLockCollectInfoMessage::append(const ObDetectorInnerReportInfo &info)
{
  return collected_info_.push_back(info);
}

int ObDeadLockCollectInfoMessage::set_dest_key(const UserBinaryKey &dest_key)
{
  dest_key_ = dest_key;
  return OB_SUCCESS;
}


bool ObDeadLockCollectInfoMessage::is_valid() const
{
  return dest_key_.is_valid();
}

int ObDeadLockCollectInfoMessage::assign(const ObDeadLockCollectInfoMessage &rhs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(collected_info_.assign(rhs.collected_info_))) {
    DETECT_LOG(WARN, "fail to copy collected info");
  } else {
    dest_key_ = rhs.dest_key_;
  }
  return ret;
}

const UserBinaryKey &ObDeadLockCollectInfoMessage::get_dest_key() const
{
  return dest_key_;
}

const ObSArray<ObDetectorInnerReportInfo> &ObDeadLockCollectInfoMessage::get_collected_info() const
{
  return collected_info_;
}

int ObDeadLockNotifyParentMessage::set_args(const ObAddr &parent_addr,
                                            const UserBinaryKey &parent_key,
                                            const ObAddr &src_addr,
                                            const UserBinaryKey &src_key)
{
  int ret = OB_SUCCESS;

  if (!parent_addr.is_valid() ||
      !parent_key.is_valid() ||
      !src_addr.is_valid() ||
      !src_key.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    parent_addr_ = parent_addr;
    parent_key_ = parent_key;
    src_addr_ = src_addr;
    src_key_ = src_key;
  }

  return ret;
}

bool ObDeadLockNotifyParentMessage::is_valid() const
{
  return parent_addr_.is_valid() &&
         parent_key_.is_valid() &&
         src_addr_.is_valid() &&
         src_key_.is_valid();
}

}
}
}
