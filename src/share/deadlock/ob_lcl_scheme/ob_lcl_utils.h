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

#ifndef OCEANBASE_SHARE_DEADLOCK_OB_LCL_SCHEME_OB_LCL_UTILS_H
#define OCEANBASE_SHARE_DEADLOCK_OB_LCL_SCHEME_OB_LCL_UTILS_H
#include "share/deadlock/ob_deadlock_detector_common_define.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

class ObLCLLabel
{
  OB_UNIS_VERSION(1);
public:
  ObLCLLabel() : id_(INVALID_VALUE), priority_(INVALID_VALUE) {}
  ObLCLLabel(const uint64_t id,
             const ObDetectorPriority &priority);
  ObLCLLabel(const ObLCLLabel &rhs);
  ~ObLCLLabel() = default;
  bool is_valid() const;
  ObLCLLabel &operator=(const ObLCLLabel &rhs);
  bool operator==(const ObLCLLabel &rhs) const;
  bool operator<(const ObLCLLabel &rhs) const;
  const common::ObAddr &get_addr() const { return addr_; }
  uint64_t get_id() const { return id_; }
  const ObDetectorPriority &get_priority() const { return priority_; }
  TO_STRING_KV(K_(addr), K_(id), K_(priority));
private:
  common::ObAddr addr_;
  uint64_t id_;
  ObDetectorPriority priority_;
};

}
}
}
#endif
