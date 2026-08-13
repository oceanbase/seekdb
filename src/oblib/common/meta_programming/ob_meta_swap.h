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
 
#ifndef SRC_COMMON_META_PROGRAMMING_OB_META_SWAP_H
#define SRC_COMMON_META_PROGRAMMING_OB_META_SWAP_H

#include "ob_type_traits.h"

namespace oceanbase
{
namespace common
{
namespace meta
{

// try use member swap() method first
template <typename T, typename Member, ENABLE_IF_SWAPABLE(Member)>
void swap(T &lhs, T &rhs, Member &member) noexcept
{
  if (OB_LIKELY((char *)&member >= (char *)&lhs && (char *)&member < (char *)&lhs + sizeof(lhs))) {
    member.swap(*(Member *)((char *)&rhs + ((char *)&member - (char *)&lhs)));
  } else {
    ob_abort();
  }
}

// otherwise decay to standard swap, that is : temp = a; a = b; b = temp;
template <typename T, typename Member, ENABLE_IF_NOT_SWAPABLE(Member)>
void swap(T &lhs, T &rhs, Member &member) noexcept
{
  if (OB_LIKELY((char *)&member >= (char *)&lhs && (char *)&member < (char *)&lhs + sizeof(lhs))) {
    std::swap(member, *(Member *)((char *)&rhs + ((char *)&member - (char *)&lhs)));
  } else {
    ob_abort();
  }
}

template <typename T, typename Head, typename ...Others>
void swap(T &lhs, T &rhs, Head &head_member, Others &...other_members) noexcept
{
  swap(lhs, rhs, head_member);
  swap(lhs, rhs, other_members...);
}

}
}
}
#endif
