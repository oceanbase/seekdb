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
 
#ifndef DEPS_OBLIB_SRC_COMMON_META_PROGRAMMING_OB_MOVER_H
#define DEPS_OBLIB_SRC_COMMON_META_PROGRAMMING_OB_MOVER_H

#include <type_traits>
#include "ob_type_traits.h"
namespace oceanbase
{
namespace common
{
namespace meta
{

// this structure is desgined for indicate that the wrappered object could be moved rather than copied.
// rvalue is not allowed in oceanbase.
template <typename  T>
struct ObMover {
  ObMover(T &obj) : obj_(obj) {}
  T &get_object() { return obj_; }
  // if T has to_string, ObMover support to_string also
  template <typename T2 = T, ENABLE_IF_HAS_TO_STRING(T2)>
  int64_t to_string(char *buf, const int64_t buf_len) const {
    return obj_.to_string(buf, buf_len);
  }
  // if T serializable, ObMover is serializable also
  template <typename T2 = T, ENABLE_IF_SERIALIZEABLE(T2)>
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const {
    return obj_.serialize(buf, buf_len, pos);
  }
  template <typename T2 = T, ENABLE_IF_SERIALIZEABLE(T2)>
  int deserialize(const char *buf, const int64_t buf_len, int64_t &pos) {
    return obj_.deserialize(buf, buf_len, pos);
  }
  template <typename T2 = T, ENABLE_IF_SERIALIZEABLE(T2)>
  int64_t get_serialize_size() const {
    return obj_.get_serialize_size();
  }
private:
  T &obj_;
};

}
}
}
#endif
