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

#include "ob_cascad_member_list.h"

namespace oceanbase
{
using namespace common;

namespace share
{
ObCascadMemberList::ObCascadMemberList()
    : member_array_()
{}

ObCascadMemberList::~ObCascadMemberList()
{}

void ObCascadMemberList::reset()
{
  member_array_.reset();
}


int ObCascadMemberList::add_member(const ObCascadMember &member)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!member.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (member_array_.count() >= OB_MAX_CHILD_MEMBER_NUMBER) {
    ret = OB_SIZE_OVERFLOW;
  }

  if (OB_SUCC(ret)) {
    if (contains(member.get_server())) {
      ret = OB_ENTRY_EXIST;
    } else if (OB_FAIL(member_array_.push_back(member))) {
      COMMON_LOG(ERROR, "member_array_ push_back failed", K(ret), K(member));
    }
  }

  return ret;
}



int64_t ObCascadMemberList::get_member_number() const
{
  return member_array_.count();
}


int ObCascadMemberList::get_member_by_index(const int64_t index, ObCascadMember &member) const
{
  int ret = OB_SUCCESS;
  if (index < 0 || index >= member_array_.count()) {
    ret = OB_SIZE_OVERFLOW;
  }
  if (OB_SUCC(ret)) {
    member = member_array_[index];
  }
  return ret;
}


bool ObCascadMemberList::contains(const common::ObAddr &server) const
{
  int bool_ret = false;
  for (int64_t i = 0; i < member_array_.count(); ++i) {
    if (member_array_[i].get_server() == server) {
      bool_ret = true;
      break;
    }
  }
  return bool_ret;
}



int ObCascadMemberList::deep_copy(const ObCascadMemberList &member_list)
{
  int ret = OB_SUCCESS;
  reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < member_list.get_member_number(); ++i) {
    ObCascadMember member;
    if (OB_FAIL(member_list.get_member_by_index(i, member))) {
      COMMON_LOG(WARN, "get_member_by_index failed", K(ret), K(i));
    } else if (OB_FAIL(add_member(member))) {
      COMMON_LOG(WARN, "add_member failed", K(ret));
    }
  }
  return ret;
}



OB_DEF_SERIALIZE(ObCascadMemberList)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  if (NULL == buf || pos < 0 || pos > buf_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCCESS != (ret = serialization::encode_i64(buf, buf_len, new_pos, get_member_number()))) {
    COMMON_LOG(WARN, "encode member_number failed", K(ret), "member_number", get_member_number());
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < get_member_number(); ++i) {
      if (OB_FAIL(member_array_[i].serialize(buf, buf_len, new_pos))) {
        COMMON_LOG(WARN, "member serialize failed", K(ret), K(i), "member", member_array_[i]);
      }
    }
  }
  if (OB_SUCC(ret)) {
    pos = new_pos;
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObCascadMemberList)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;
  int64_t member_number = 0;
  if (NULL == buf || pos < 0 || pos > data_len) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_SUCCESS != (ret = serialization::decode_i64(buf, data_len, new_pos, &member_number))) {
    COMMON_LOG(WARN, "decode member_number failed", K(ret), K(member_number));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < member_number; ++i) {
      ObCascadMember member;
      if (OB_FAIL(member.deserialize(buf, data_len, new_pos))) {
        COMMON_LOG(WARN, "decode member failed", K(ret), K(member_number));
      } else if (OB_FAIL(add_member(member))) {
        COMMON_LOG(WARN, "add_member failed", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    pos = new_pos;
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObCascadMemberList)
{
  int64_t serialize_size = serialization::encoded_length_i64(get_member_number());
  for (int64_t i = 0; i < get_member_number(); ++i) {
    serialize_size += member_array_[i].get_serialize_size();
  }
  return serialize_size;
}

} // namespace common
} // namespace oceanbase
