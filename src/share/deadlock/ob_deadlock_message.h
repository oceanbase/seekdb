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

#ifndef OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_COLLECT_INFO_MESSAGE_H
#define OCEANBASE_SHARE_DEADLOCK_OB_DEADLOCK_COLLECT_INFO_MESSAGE_H

#include "ob_deadlock_detector_common_define.h"
#include "lib/string/ob_string_holder.h"

namespace oceanbase
{
namespace share
{
namespace detector
{

class ObDeadLockCollectInfoMessage
{
  OB_UNIS_VERSION(1);
public:
  ObDeadLockCollectInfoMessage() = default;
  ~ObDeadLockCollectInfoMessage() = default;
  ObDeadLockCollectInfoMessage &operator=(const ObDeadLockCollectInfoMessage &) = delete;
  int assign(const ObDeadLockCollectInfoMessage &rhs);
  int set_dest_key(const UserBinaryKey &dest_key);
  int append(const ObDetectorInnerReportInfo &info);
  bool is_valid() const;
  const UserBinaryKey &get_dest_key() const;
  const common::ObSArray<ObDetectorInnerReportInfo> &get_collected_info() const;
  TO_STRING_KV(K_(dest_key), K_(collected_info));
private:
  UserBinaryKey dest_key_;
  common::ObSArray<ObDetectorInnerReportInfo> collected_info_;
};

class ObDeadLockNotifyParentMessage
{
OB_UNIS_VERSION(1);
public:
  ObDeadLockNotifyParentMessage() = default;
  ~ObDeadLockNotifyParentMessage() = default;
  int set_args(const common::ObAddr &parent_addr,
               const UserBinaryKey &dest_key,
               const common::ObAddr &src_addr,
               const UserBinaryKey &src_key);
  const UserBinaryKey &get_parent_key() const { return parent_key_; }
  const common::ObAddr &get_src_addr() const { return src_addr_; }
  const UserBinaryKey &get_src_key() const { return src_key_; }
  bool is_valid() const;
  TO_STRING_KV(K_(parent_addr), K_(parent_key), K_(src_addr), K_(src_key), K_(action));
private:
  common::ObAddr parent_addr_;
  UserBinaryKey parent_key_;
  common::ObAddr src_addr_;
  UserBinaryKey src_key_;
  ObStringHolder action_;
};

}
}
}

#endif
