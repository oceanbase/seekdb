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

#ifndef NDEBUG
#define MEMCHK_LEVEL 1
#endif

namespace oceanbase
{
namespace lib
{

#define ALIGN_UP(x, y) (((x) + ((y) - 1)) / (y) * (y))

// Compatibility sizing constants. These values are intentionally frozen at
// the legacy allocator layout so existing subsystem sizing remains unchanged.
static const uint64_t MEMCHK_CHUNK_ALIGN_BITS = 20;
static const uint64_t MEMCHK_CHUNK_ALIGN = 2UL << MEMCHK_CHUNK_ALIGN_BITS;
static const uint32_t AOBJECT_TAIL_SIZE = 16;
static const uint32_t AOBJECT_LABEL_SIZE = 15;
static const uint32_t MIN_AOBJECT_SIZE = 16;
static const uint32_t AOBJECT_CELL_BYTES = 8;
static const uint32_t INTACT_ACHUNK_SIZE = 1U << 21;
static const int64_t INVISIBLE_CHARACTER = char(127);
static const int64_t ALLOC_ABLOCK_CONCURRENCY = 4;

static const uint32_t AOBJECT_HEADER_SIZE = 48;
static const uint32_t AOBJECT_META_SIZE = AOBJECT_HEADER_SIZE + AOBJECT_TAIL_SIZE;
static const uint32_t INTACT_NORMAL_AOBJECT_SIZE = 8L << 10;
static const uint32_t INTACT_MIDDLE_AOBJECT_SIZE = 64L << 10;
static const int32_t AOBJECT_BACKTRACE_COUNT = 16;
static const int32_t AOBJECT_BACKTRACE_SIZE = sizeof(void *) * AOBJECT_BACKTRACE_COUNT;
static const int32_t AOBJECT_EXTRA_INFO_SIZE = AOBJECT_BACKTRACE_SIZE;
static const int32_t MAX_BACKTRACE_LENGTH = 512;

static const uint32_t ABLOCK_HEADER_SIZE = 48;
static const uint32_t ABLOCK_SIZE = INTACT_NORMAL_AOBJECT_SIZE;
static const uint32_t ACHUNK_PURE_HEADER_SIZE = 104;
static const uint32_t ACHUNK_HEADER_SIZE = 16L << 10;
static const uint32_t ACHUNK_SIZE = INTACT_ACHUNK_SIZE - ACHUNK_HEADER_SIZE;
static const uint64_t BLOCKS_PER_CHUNK = ACHUNK_SIZE / ABLOCK_SIZE;
static const uint64_t ABLOCK_ALIGN = 1L << 12;

STATIC_ASSERT(AOBJECT_META_SIZE == 64, "AOBJECT_META_SIZE compatibility value changed");
STATIC_ASSERT(ABLOCK_HEADER_SIZE == 48, "ABLOCK_HEADER_SIZE compatibility value changed");
STATIC_ASSERT(ACHUNK_PURE_HEADER_SIZE == 104, "ACHUNK_PURE_HEADER_SIZE compatibility value changed");
STATIC_ASSERT(ACHUNK_SIZE == 2080768, "ACHUNK_SIZE compatibility value changed");

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
  ObLabel() : str_(nullptr) {}

  template<std::size_t N>
  ObLabel(const char (&str)[N])
  {
    STATIC_ASSERT(N - 1 <= AOBJECT_LABEL_SIZE,
        "label length longer than 15 is not allowed!");
    str_ = str;
  }

  template <typename T, typename DUMP_T=
            typename std::enable_if<std::is_convertible<T, const char*>::value>::type>
  ObLabel(T str) : str_(str) {}

  template<std::size_t N>
  ObLabel& operator=(const char (&str)[N])
  {
    STATIC_ASSERT(N - 1 <= AOBJECT_LABEL_SIZE,
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
      alloc_extra_info_(false)
  {}
  int64_t to_string(char* buf, const int64_t buf_len) const;
public:
  union { // FARM COMPAT WHITELIST
    char padding__[4];
    struct {
      struct {
        uint8_t alloc_extra_info_ : 1;
      };
    };
  };
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

class ObLightBacktraceGuard
{
public:
  explicit ObLightBacktraceGuard(const bool enable) : last_(tl_enable())
  {
    tl_enable() = enable;
  }
  ~ObLightBacktraceGuard() { tl_enable() = last_; }
  static bool is_enabled() { return tl_enable(); }
private:
  static bool &tl_enable()
  {
    static __thread bool enable = true;
    return enable;
  }
  const bool last_;
};

class ObUnmanagedMemoryStat
{
public:
  class DisableGuard
  {
  public:
    DisableGuard() : last_(tl_disabled()) { tl_disabled() = true; }
    ~DisableGuard() { tl_disabled() = last_; }
    static bool &tl_disabled()
    {
      static __thread bool disabled = false;
      return disabled;
    }
  private:
    bool last_;
  };

  struct Stat
  {
    int64_t inc_hold_;
    int64_t dec_hold_;
    int64_t inc_size_;
    int64_t dec_size_;
    int64_t inc_cnt_;
    int64_t dec_cnt_;
  };

  static ObUnmanagedMemoryStat &get_instance()
  {
    static ObUnmanagedMemoryStat instance;
    return instance;
  }
  static bool is_disabled() { return DisableGuard::tl_disabled(); }
  void inc(const int64_t size);
  void dec(const int64_t size);
  int64_t get_total_hold();
  int format_dist(char *buf, int64_t buf_len, int64_t &pos);
private:
  ObUnmanagedMemoryStat()
  {
    char *ptr = reinterpret_cast<char *>(this);
    for (size_t i = 0; i < sizeof(*this); ++i) {
      ptr[i] = 0;
    }
  }
  constexpr static int N = 64;
  Stat stat_[N];
};

#define UNMAMAGED_MEMORY_STAT ObUnmanagedMemoryStat::get_instance()

extern int64_t get_unmanaged_memory_size();

} // end namespace lib
} // end namespace oceanbase

#endif /* _ALLOC_STRUCT_H_ */
