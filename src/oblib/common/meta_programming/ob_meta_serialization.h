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
 
#ifndef DEPS_OBLIB_SRC_COMMON_META_PROGRAMMING_OB_META_SERIALIZATION_H
#define DEPS_OBLIB_SRC_COMMON_META_PROGRAMMING_OB_META_SERIALIZATION_H

#include "ob_meta_define.h"
#include "lib/ob_errno.h"
#include "ob_type_traits.h"

namespace oceanbase
{
namespace common
{
namespace meta
{

template <typename T>
class MetaSerializer
{
public:
  MetaSerializer(ObIAllocator &alloc, const T &data)
  : alloc_(alloc),
  data_(const_cast<T &>(data)) {}
  MetaSerializer(const MetaSerializer &rhs)
  : alloc_(rhs.alloc_),
  data_(rhs.data_) {}
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const
  {
    return data_.serialize(buf, buf_len, pos);
  }
  template <typename T2 = T,
            typename std::enable_if<OB_TRAIT_SERIALIZEABLE(T2), bool>::type = true>
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
  {
    return data_.deserialize(buf, buf_len, pos);
  }
  template <typename T2 = T,
            typename std::enable_if<!OB_TRAIT_SERIALIZEABLE(T2) &&
                                    OB_TRAIT_DEEP_SERIALIZEABLE(T2), bool>::type = true>
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos)
  {
    return data_.deserialize(alloc_, buf, buf_len, pos);
  }
  int64_t get_serialize_size() const { return data_.get_serizalize_size(); }
private:
  ObIAllocator &alloc_;
  T &data_;
};

}
}
}
#endif
