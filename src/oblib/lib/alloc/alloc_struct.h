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

#ifndef _ALLOC_STRUCT_H_
#define _ALLOC_STRUCT_H_

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <type_traits>
#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/utility/ob_platform_utils.h"

namespace oceanbase
{
namespace lib
{

inline uint64_t align_up(uint64_t x, uint64_t align)
{
  return (x + (align - 1)) / align * align;
}

inline uint64_t align_up2(uint64_t x, uint64_t align)
{
  return (x + (align - 1)) & ~(align - 1);
}

static ssize_t get_page_size()
{
  return ob_get_page_size();
}

enum ObAllocPrio
{
  OB_NORMAL_ALLOC,
  OB_HIGH_ALLOC
};

struct ObLabel
{
  static constexpr uint32_t MAX_LENGTH = 15;

  ObLabel() : str_(nullptr) {}

  template<std::size_t N>
  ObLabel(const char (&str)[N])
  {
    STATIC_ASSERT(N - 1 <= MAX_LENGTH,
        "label length longer than 15 is not allowed!");
    str_ = str;
  }

  template <typename T, typename DUMP_T=
            typename std::enable_if<std::is_convertible<T, const char*>::value>::type>
  ObLabel(T str) : str_(str) {}

  template<std::size_t N>
  ObLabel& operator=(const char (&str)[N])
  {
    STATIC_ASSERT(N - 1 <= MAX_LENGTH,
        "label length longer than 15 is not allowed!");
    str_ = str;
    return *this;
  }

  template <typename T, typename DUMP_T=
            typename std::enable_if<std::is_convertible<T, const char*>::value>::type>
  ObLabel& operator=(T str)
  {
    str_ = str;
    return *this;
  }

  bool operator==(const ObLabel &other) const;

  template<typename T>
  bool operator==(const T &value) const
  {
    return operator==(ObLabel(value));
  }

  template<typename T>
  bool operator!=(const T &value) const
  {
    return !(*this == value);
  }

  operator const char*() const;
  bool is_valid() const { return nullptr != str_ && '\0' != str_[0]; }
  int64_t to_string(char *buf, const int64_t buf_len) const;
  const char *str_;
};

struct ObMemAttr
{
  ObLabel label_;
  uint64_t ctx_id_;
  int32_t sub_ctx_id_;
  ObAllocPrio prio_;
  explicit ObMemAttr(
      ObLabel label = ObLabel(),
      uint64_t ctx_id = 0,
      ObAllocPrio prio = OB_NORMAL_ALLOC)
    : label_(label),
      ctx_id_(ctx_id),
      prio_(prio),
      padding__{}
  {}
  int64_t to_string(char* buf, const int64_t buf_len) const;
public:
  char padding__[4]; // FARM COMPAT WHITELIST
};

// Retained because third-party adapters use the thread-local attribute as
// their own propagation channel even though jemalloc does not consume labels.
class ObMallocHookAttrGuard
{
public:
  explicit ObMallocHookAttrGuard(const ObMemAttr& attr);
  ~ObMallocHookAttrGuard();
  static ObMemAttr &get_tl_mem_attr()
  {
    static thread_local ObMemAttr tl_mem_attr("glibc_malloc", ObCtxIds::GLIBC);
    return tl_mem_attr;
  }
private:
  ObMemAttr old_attr_;
};

} // end namespace lib
} // end namespace oceanbase

#endif /* _ALLOC_STRUCT_H_ */
