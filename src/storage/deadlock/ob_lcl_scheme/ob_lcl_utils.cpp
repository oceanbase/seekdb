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

namespace oceanbase
{
namespace share
{
namespace detector
{

ObLCLLabel::ObLCLLabel(const uint64_t id,
                       const ObDetectorPriority &priority)
  :id_(id),
  priority_(priority)
{
  // do nothing
}

ObLCLLabel::ObLCLLabel(const ObLCLLabel &rhs)
  :id_(rhs.id_),
  priority_(rhs.priority_)
{
  // do nothing
}

bool ObLCLLabel::is_valid() const
{
  return priority_.is_valid() && INVALID_VALUE != id_;
}

ObLCLLabel &ObLCLLabel::operator=(const ObLCLLabel &rhs)
{
  id_ = rhs.id_;
  priority_ = rhs.priority_;
  return *this;
}

bool ObLCLLabel::operator==(const ObLCLLabel &rhs) const
{
  return priority_ == rhs.priority_ && id_ == rhs.id_;
}

bool ObLCLLabel::operator<(const ObLCLLabel &rhs) const
{
  bool ret = false;
  if (priority_ < rhs.priority_) {
    ret = true;
  } else if (priority_ == rhs.priority_) {
    if (id_ < rhs.id_) {
      ret = true;
    }
  }
  return ret;
}

}
}
}
