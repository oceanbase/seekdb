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

#include "ob_lcl_utils.h"
#include "share/deadlock/ob_deadlock_detector_mgr.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

OB_SERIALIZE_MEMBER(ObLCLLabel, addr_, id_, priority_);

ObLCLLabel::ObLCLLabel(const uint64_t id,
                       const ObDetectorPriority &priority)
  :addr_(GCTX.self_addr()),
  id_(id),
  priority_(priority)
{
  // do nothing
}

ObLCLLabel::ObLCLLabel(const ObLCLLabel &rhs)
  :addr_(rhs.addr_),
  id_(rhs.id_),
  priority_(rhs.priority_)
{
  // do nothing
}

bool ObLCLLabel::is_valid() const
{
  return addr_.is_valid() && priority_.is_valid() && INVALID_VALUE != id_;
}

ObLCLLabel &ObLCLLabel::operator=(const ObLCLLabel &rhs)
{
  addr_ = rhs.addr_;
  id_ = rhs.id_;
  priority_ = rhs.priority_;
  return *this;
}

bool ObLCLLabel::operator==(const ObLCLLabel &rhs) const
{
  return priority_ == rhs.priority_ && addr_ == rhs.addr_ && id_ == rhs.id_;
}

bool ObLCLLabel::operator<(const ObLCLLabel &rhs) const
{
  bool ret = false;
  if (priority_ < rhs.priority_) {
    ret = true;
  } else if (priority_ == rhs.priority_) {
    if (addr_ < rhs.addr_) {
      ret = true;
    } else if (addr_ == rhs.addr_) {
      if (id_ < rhs.id_) {
        ret = true;
      }
    }
  }
  return ret;
}

}
}
}
