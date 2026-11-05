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

#ifndef OCEANBASE_SHARE_OB_CASCAD_MEMBER_LIST_H_
#define OCEANBASE_SHARE_OB_CASCAD_MEMBER_LIST_H_

#include "common/ob_member_list.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_unify_serialize.h"
#include "ob_cascad_member.h"

namespace oceanbase
{
namespace share
{
class ObCascadMemberList
{
  OB_UNIS_VERSION(1);
public:
  ObCascadMemberList();
  virtual ~ObCascadMemberList();
public:
  void reset();
  int add_member(const ObCascadMember &member);
  int64_t get_member_number() const;
  int get_member_by_index(const int64_t index, ObCascadMember &member) const;
  bool contains(const common::ObAddr &server) const;
  int deep_copy(const ObCascadMemberList &member_list);
  TO_STRING_KV(K(member_array_));
private:
  typedef common::ObSEArray<ObCascadMember, 1> ObCascadMemberArray;
  ObCascadMemberArray member_array_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_CASCAD_MEMBER_LIST_H_
