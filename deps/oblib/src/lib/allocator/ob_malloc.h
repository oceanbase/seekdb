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

#ifndef OCEANBASE_COMMON_OB_MALLOC_H_
#define OCEANBASE_COMMON_OB_MALLOC_H_
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <stdint.h>
#include "lib/allocator/ob_allocator.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_tc_malloc.h"
#include "lib/time/ob_time_utility.h"
#include "lib/alloc/ob_malloc_allocator.h"
#ifdef _WIN32
namespace oceanbase { namespace common { extern bool g_ob_log_main_entered; } }
#endif

namespace oceanbase
{
namespace common
{

enum ObMallocBackend : int8_t
{
  OB_MALLOC_BACKEND_UNINITIALIZED = -1,
  OB_MALLOC_BACKEND_OBMALLOC = 0,
  OB_MALLOC_BACKEND_JEMALLOC,
  OB_MALLOC_BACKEND_UNKNOWN,
};

const char *ob_malloc_backend_env_name();
const char *ob_malloc_backend_name(const ObMallocBackend backend);
ObMallocBackend parse_ob_malloc_backend(const char *name);
ObMallocBackend initialize_ob_malloc_backend();
bool restore_malloc_backend_after_fork();
extern std::atomic<int8_t> g_ob_malloc_backend;
#if defined(__APPLE__)
bool configure_darwin_malloc_zone(const ObMallocBackend backend);
#endif

inline ObMallocBackend get_ob_malloc_backend()
{
  ObMallocBackend backend = static_cast<ObMallocBackend>(
      g_ob_malloc_backend.load(std::memory_order_relaxed));
  if (OB_UNLIKELY(OB_MALLOC_BACKEND_UNINITIALIZED == backend)) {
    backend = initialize_ob_malloc_backend();
  }
  return backend;
}

inline bool is_ob_malloc_backend(const ObMallocBackend backend)
{
  return OB_MALLOC_BACKEND_OBMALLOC == backend;
}

inline bool is_jemalloc_backend(const ObMallocBackend backend)
{
  return OB_MALLOC_BACKEND_JEMALLOC == backend;
}

inline bool is_ob_malloc_backend()
{
  return is_ob_malloc_backend(get_ob_malloc_backend());
}

inline bool is_jemalloc_backend()
{
  return is_jemalloc_backend(get_ob_malloc_backend());
}

#ifdef _WIN32
// Magic tag placed before every system-malloc'd block during static init.
// ob_free/ob_realloc check for this tag to distinguish system-allocated
// memory from OB-allocator-managed memory, avoiding use-after-free if
// the block is freed after main() enters (when the OB allocator is active).
static constexpr uint64_t OB_SYS_ALLOC_MAGIC = 0xDEAD5741C0DE5741ULL;
#endif
inline void ob_print_mod_memory_usage(bool print_to_std = false,
                                      bool print_glibc_malloc_stats = false)
{
  UNUSEDx(print_to_std, print_glibc_malloc_stats);
}

inline void *ob_malloc(const int64_t nbyte, const ObMemAttr &attr)
{
  void *ptr = NULL;
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_LIKELY(is_ob_malloc_backend(backend))) {
    auto allocator = lib::ObMallocAllocator::get_instance();
    if (!OB_ISNULL(allocator)) {
      ptr = allocator->alloc(nbyte, attr);
#ifndef _WIN32
      if (OB_ISNULL(ptr)) {
        LIB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "allocate memory fail", K(attr), K(nbyte));
      }
#endif
    }
#ifdef _WIN32
    else {
      ptr = ::malloc(nbyte);
    }
#endif
  } else if (OB_LIKELY(is_jemalloc_backend(backend))) {
    if (OB_LIKELY(nbyte > 0)) {
      ptr = jemalloc_malloc(static_cast<size_t>(nbyte));
    }
    if (OB_ISNULL(ptr)) {
      LIB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "allocate memory fail", K(attr), K(nbyte));
    }
  }
  return ptr;
}

inline void ob_free(void *ptr)
{
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_LIKELY(is_ob_malloc_backend(backend))) {
    if (OB_LIKELY(lib::ObMallocAllocator::is_inited_)) {
      auto *allocator = lib::ObMallocAllocator::get_instance();
      abort_unless(!OB_ISNULL(allocator));
      allocator->free(ptr);
      ptr = NULL;
    }
#ifdef _WIN32
    else if (ptr != nullptr) {
      ::free(ptr);
    }
#endif
  } else if (OB_LIKELY(is_jemalloc_backend(backend))) {
    jemalloc_free(ptr);
  }
}

inline void *ob_realloc(void *ptr, const int64_t nbyte, const ObMemAttr &attr)
{
  void *nptr = NULL;
  const ObMallocBackend backend = get_ob_malloc_backend();
  if (OB_LIKELY(is_ob_malloc_backend(backend))) {
    if (OB_LIKELY(lib::ObMallocAllocator::is_inited_)) {
      auto *allocator = lib::ObMallocAllocator::get_instance();
      if (!OB_ISNULL(allocator)) {
        nptr = allocator->realloc(ptr, nbyte, attr);
#ifndef _WIN32
        if (OB_ISNULL(nptr)) {
          LIB_LOG_RET(ERROR, OB_ALLOCATE_MEMORY_FAILED, "allocate memory fail", K(attr), K(nbyte));
        }
#endif
      }
    }
#ifdef _WIN32
    else {
      nptr = ::realloc(ptr, nbyte);
    }
#endif
  } else if (OB_LIKELY(is_jemalloc_backend(backend))) {
    if (OB_LIKELY(nbyte >= 0)) {
      nptr = jemalloc_realloc(ptr, static_cast<size_t>(nbyte));
    }
    if (OB_ISNULL(nptr) && nbyte > 0) {
      LIB_LOG_RET(ERROR, OB_ALLOCATE_MEMORY_FAILED, "allocate memory fail", K(attr), K(nbyte));
    }
  }
  return nptr;
}

int64_t ob_malloc_usable_size(void *ptr);

class MemoryUsageTracker
{
public:
  MemoryUsageTracker() : live_bytes_(0) {}
  ~MemoryUsageTracker() = default;

  void alloc(const int64_t size)
  {
    if (size > 0) {
      live_bytes_.fetch_add(size, std::memory_order_relaxed);
    }
  }

  void free(const int64_t size)
  {
    if (size > 0) {
      live_bytes_.fetch_sub(size, std::memory_order_relaxed);
    }
  }

  void adjust(const int64_t delta)
  {
    if (0 != delta) {
      live_bytes_.fetch_add(delta, std::memory_order_relaxed);
    }
  }

  int64_t used() const { return live_bytes_.load(std::memory_order_relaxed); }
  void reset() { live_bytes_.store(0, std::memory_order_relaxed); }

private:
  std::atomic<int64_t> live_bytes_;
};

typedef MemoryUsageTracker *(*MemoryUsageTrackerResolver)(const int64_t ctx_id);
void set_memory_usage_tracker_resolver(const int64_t ctx_id,
                                       MemoryUsageTrackerResolver resolver);
MemoryUsageTracker *resolve_memory_usage_tracker(const int64_t ctx_id);

class TrackedAllocator final : public ObIAllocator
{
public:
  TrackedAllocator()
    : allocator_(nullptr), tracker_(nullptr), attr_()
  {}

  TrackedAllocator(ObIAllocator &allocator,
                   MemoryUsageTracker *tracker,
                   const ObMemAttr &attr = ObMemAttr())
    : allocator_(&allocator), tracker_(tracker), attr_(attr)
  {}

  ~TrackedAllocator() override = default;

  void configure(ObIAllocator &allocator,
                 MemoryUsageTracker *tracker,
                 const ObMemAttr &attr)
  {
    allocator_ = &allocator;
    tracker_ = tracker;
    attr_ = attr;
  }

  void set_tracker(MemoryUsageTracker *tracker) { tracker_ = tracker; }
  void set_attr(const ObMemAttr &attr) { attr_ = attr; }
  bool is_tracking() const { return nullptr != tracker_; }
  MemoryUsageTracker *get_tracker() const { return tracker_; }

  void *alloc(const int64_t size) override
  {
    return alloc(size, attr_);
  }

  void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    void *ptr = nullptr;
    if (OB_ISNULL(allocator_)) {
    } else if (OB_ISNULL(tracker_)) {
      ptr = allocator_->alloc(size, attr);
    } else if (size > 0 && size <= INT64_MAX - static_cast<int64_t>(sizeof(Header))) {
      void *raw_ptr = allocator_->alloc(size + sizeof(Header), attr);
      if (OB_NOT_NULL(raw_ptr)) {
        Header *header = new (raw_ptr) Header(tracker_, size);
        ptr = header + 1;
        tracker_->alloc(size);
      }
    }
    return ptr;
  }

  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr) override
  {
    return realloc_with_attr(const_cast<void *>(ptr), size, attr);
  }

  void *realloc(void *ptr, const int64_t old_size, const int64_t new_size) override
  {
    UNUSED(old_size);
    return realloc_with_attr(ptr, new_size, attr_);
  }

  void free(void *ptr) override
  {
    if (OB_ISNULL(allocator_) || OB_ISNULL(ptr)) {
    } else if (OB_ISNULL(tracker_)) {
      allocator_->free(ptr);
    } else {
      Header *header = static_cast<Header *>(ptr) - 1;
      abort_unless(header->is_valid());
      MemoryUsageTracker *allocation_tracker = header->tracker_;
      const int64_t requested_size = header->requested_size_;
      header->invalidate();
      allocator_->free(header);
      allocation_tracker->free(requested_size);
    }
  }

  int64_t total() const override { return nullptr != allocator_ ? allocator_->total() : 0; }
  int64_t used() const override { return nullptr != allocator_ ? allocator_->used() : 0; }

  void *raw_pointer(void *ptr) const
  {
    return nullptr != ptr && nullptr != tracker_
        ? static_cast<void *>(static_cast<Header *>(ptr) - 1)
        : ptr;
  }

  static constexpr int64_t header_size() { return sizeof(Header); }

private:
  struct alignas(std::max_align_t) Header
  {
    static constexpr uint64_t MAGIC = 0x545241434b454431ULL;
    Header(MemoryUsageTracker *tracker, const int64_t requested_size)
      : magic_(MAGIC), tracker_(tracker), requested_size_(requested_size)
    {}
    bool is_valid() const { return MAGIC == magic_ && nullptr != tracker_; }
    void invalidate() { magic_ = 0; }

    uint64_t magic_;
    MemoryUsageTracker *tracker_;
    int64_t requested_size_;
  };

  void *realloc_with_attr(void *ptr, const int64_t size, const ObMemAttr &attr)
  {
    void *new_ptr = nullptr;
    if (OB_ISNULL(ptr)) {
      new_ptr = alloc(size, attr);
    } else if (0 == size) {
      free(ptr);
    } else if (OB_ISNULL(allocator_)) {
    } else if (OB_ISNULL(tracker_)) {
      new_ptr = allocator_->realloc(ptr, size, attr);
    } else if (size > 0 && size <= INT64_MAX - static_cast<int64_t>(sizeof(Header))) {
      Header *old_header = static_cast<Header *>(ptr) - 1;
      abort_unless(old_header->is_valid());
      const int64_t old_size = old_header->requested_size_;
      MemoryUsageTracker *allocation_tracker = old_header->tracker_;
      void *raw_ptr = allocator_->realloc(old_header, size + sizeof(Header), attr);
      if (OB_NOT_NULL(raw_ptr)) {
        Header *new_header = static_cast<Header *>(raw_ptr);
        new_header->magic_ = Header::MAGIC;
        new_header->tracker_ = allocation_tracker;
        new_header->requested_size_ = size;
        allocation_tracker->adjust(size - old_size);
        new_ptr = new_header + 1;
      }
    }
    return new_ptr;
  }

private:
  ObIAllocator *allocator_;
  MemoryUsageTracker *tracker_;
  ObMemAttr attr_;
};

void *ob_malloc_align(
    const int64_t alignment, const int64_t nbyte,
    const ObMemAttr &attr);
void ob_free_align(void *ptr);

// Deprecated interface
inline void *ob_malloc(const int64_t nbyte, const lib::ObLabel &label)
{
  ObMemAttr attr;
  attr.label_ = label;
  return ob_malloc(nbyte, attr);
}

// Deprecated interface
void *ob_malloc_align(
    const int64_t alignment,
    const int64_t nbyte, const lib::ObLabel &label);

////////////////////////////////////////////////////////////////
class ObMalloc : public ObIAllocator
{
public:
  ObMalloc() {};
  explicit ObMalloc(const lib::ObLabel &label)
  {
    memattr_.label_ = label;
  };
  explicit ObMalloc(ObMemAttr attr)
    : memattr_(attr)
  {}
  virtual ~ObMalloc() {};
public:
  void set_label(const lib::ObLabel &label) {memattr_.label_ = label;};
  void set_attr(const ObMemAttr &attr) { memattr_ = attr; }
  void *alloc(const int64_t sz)
  {
    return ob_malloc(sz, memattr_);
  }
  void *alloc(const int64_t size, const ObMemAttr &attr)
  {
    return ob_malloc(size, attr);
  }
  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr) override
  {
    return ob_realloc(const_cast<void *>(ptr), size, attr);
  }
  void *realloc(void *ptr, const int64_t oldsz, const int64_t newsz) override
  {
    UNUSED(oldsz);
    return ob_realloc(ptr, newsz, memattr_);
  }
  void free(void *ptr) { ob_free(ptr); };
private:
  ObMemAttr memattr_;
};
typedef ObMalloc ObTCMalloc;

class MemoryContextMalloc final : public ObIAllocator
{
public:
  explicit MemoryContextMalloc(const ObMemAttr &attr)
    : allocator_(attr),
      live_bytes_(0)
  {}
  ~MemoryContextMalloc() override = default;

  void *alloc(const int64_t size) override
  {
    void *ptr = allocator_.alloc(size);
    account_alloc(ptr);
    return ptr;
  }

  void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    void *ptr = allocator_.alloc(size, attr);
    account_alloc(ptr);
    return ptr;
  }

  void *realloc(const void *ptr, const int64_t size, const ObMemAttr &attr) override
  {
    return realloc_with_attr(const_cast<void *>(ptr), size, attr);
  }

  void *realloc(void *ptr, const int64_t old_size, const int64_t new_size) override
  {
    UNUSED(old_size);
    if (nullptr != ptr && 0 == new_size) {
      free(ptr);
      return nullptr;
    }
    const int64_t old_usable_size = usable_size(ptr);
    void *new_ptr = allocator_.realloc(ptr, old_size, new_size);
    if (nullptr != new_ptr) {
      account_realloc(old_usable_size, usable_size(new_ptr));
    }
    return new_ptr;
  }

  void free(void *ptr) override
  {
    if (nullptr != ptr) {
      const int64_t allocation_size = usable_size(ptr);
      allocator_.free(ptr);
      live_bytes_.fetch_sub(allocation_size, std::memory_order_relaxed);
    }
  }

  int64_t total() const override { return live_bytes_.load(std::memory_order_relaxed); }
  int64_t used() const override { return total(); }

private:
  void *realloc_with_attr(void *ptr, const int64_t size, const ObMemAttr &attr)
  {
    if (nullptr != ptr && 0 == size) {
      free(ptr);
      return nullptr;
    }
    const int64_t old_usable_size = usable_size(ptr);
    void *new_ptr = allocator_.realloc(ptr, size, attr);
    if (nullptr != new_ptr) {
      account_realloc(old_usable_size, usable_size(new_ptr));
    }
    return new_ptr;
  }

  void account_alloc(void *ptr)
  {
    if (nullptr != ptr) {
      live_bytes_.fetch_add(usable_size(ptr), std::memory_order_relaxed);
    }
  }

  void account_realloc(const int64_t old_usable_size, const int64_t new_usable_size)
  {
    live_bytes_.fetch_add(new_usable_size - old_usable_size, std::memory_order_relaxed);
  }

  int64_t usable_size(void *ptr) const
  {
    return ob_malloc_usable_size(ptr);
  }

private:
  ObMalloc allocator_;
  std::atomic<int64_t> live_bytes_;
};

class ObMemBuf
{
public:
  ObMemBuf()
    : buf_ptr_(NULL),
      buf_size_(OB_MALLOC_NORMAL_BLOCK_SIZE),
      label_(ObModIds::OB_MOD_DO_NOT_USE_ME)
  {

  }

  explicit ObMemBuf(const int64_t default_size)
    : buf_ptr_(NULL),
      buf_size_(default_size),
      label_(ObModIds::OB_MOD_DO_NOT_USE_ME)
  {

  }

  virtual ~ObMemBuf()
  {
    if (NULL != buf_ptr_) {
      ob_free(buf_ptr_);
      buf_ptr_ = NULL;
    }
  }

  inline char *get_buffer()
  {
    return buf_ptr_;
  }

  int64_t get_buffer_size() const
  {
    return buf_size_;
  }

  int ensure_space(const int64_t size, const lib::ObLabel &label = nullptr);

private:
  char *buf_ptr_;
  int64_t buf_size_;
  lib::ObLabel label_;
};

class ObMemBufAllocatorWrapper : public ObIAllocator
{
public:
  ObMemBufAllocatorWrapper(ObMemBuf &mem_buf, const lib::ObLabel &label = nullptr)
      : mem_buf_(mem_buf), label_(label) {}
public:
  virtual void *alloc(int64_t sz) override
  {
    char *ptr = NULL;
    if (OB_SUCCESS == mem_buf_.ensure_space(sz, label_)) {
      ptr = mem_buf_.get_buffer();
    }
    return ptr;
  }
  virtual void *alloc(int64_t sz, const ObMemAttr &attr) override
  {
    UNUSEDx(attr);
    return alloc(sz);
  }
  virtual void free(void *ptr) override
  {
    UNUSED(ptr);
  }
private:
  ObMemBuf &mem_buf_;
  lib::ObLabel label_;
};


class ObRawBufAllocatorWrapper : public ObIAllocator
{
public:
  ObRawBufAllocatorWrapper(char *mem_buf, int64_t mem_buf_len) : mem_buf_(mem_buf),
                                                                 mem_buf_len_(mem_buf_len) {}
public:
  virtual void *alloc(int64_t sz) override
  {
    char *ptr = NULL;
    if (mem_buf_len_ >= sz) {
      ptr = mem_buf_;
    }
    return ptr;
  }
  virtual void *alloc(int64_t sz, const ObMemAttr &attr) override
  {
    UNUSEDx(attr);
    return alloc(sz);
  }
  virtual void free(void *ptr) override
  {
    UNUSED(ptr);
  }
private:
  char *mem_buf_;
  int64_t mem_buf_len_;
};

template <typename T>
void ob_delete(T *&ptr)
{
  if (NULL != ptr) {
    ptr->~T();
    ob_free(ptr);
    ptr = NULL;
  }
}

}
}

extern "C" void *ob_zalloc(const int64_t nbyte);
extern "C" void ob_zfree(void *ptr);

#define OB_NEW(T, label, ...)                  \
  ({                                            \
    T* ret = NULL;                              \
    void *buf = oceanbase::common::ob_malloc(sizeof(T), label); \
    if (OB_NOT_NULL(buf))                       \
    {                                           \
      ret = new(buf) T(__VA_ARGS__);            \
    }                                           \
    ret;                                        \
  })

#define OB_NEW_ALIGN32(T, label, ...)          \
  ({                                            \
    T* ret = NULL;                              \
    void *buf = ob_malloc_align(32, sizeof(T), label);   \
    if (OB_NOT_NULL(buf))                       \
    {                                           \
      ret = new(buf) T(__VA_ARGS__);            \
    }                                           \
    ret;                                        \
  })

#define OB_NEWx(T, pool, ...)                   \
  ({                                            \
    T* ret = NULL;                              \
    if (OB_NOT_NULL(pool)) {                    \
      void *_buf_ = (pool)->alloc(sizeof(T));   \
      if (OB_NOT_NULL(_buf_))                   \
      {                                         \
        ret = new(_buf_) T(__VA_ARGS__);        \
      }                                         \
    }                                           \
    ret;                                        \
  })

#define OB_NEW_ARRAY(T, pool, count)            \
  ({                                            \
    T* ret = NULL;                              \
    if (OB_NOT_NULL(pool) && count > 0) {       \
      int64_t _size_ = sizeof(T) * count;       \
      void *_buf_ = (pool)->alloc(_size_);      \
      if (OB_NOT_NULL(_buf_))                   \
      {                                         \
        ret = new(_buf_) T[count];              \
      }                                         \
    }                                           \
    ret;                                        \
  })

#define OB_DELETE(T, label, ptr)               \
  do{                                           \
    if (NULL != ptr)                            \
    {                                           \
      ptr->~T();                                \
      ob_free(ptr);                             \
      ptr = NULL;                               \
    }                                           \
  } while(0)

#define OB_DELETE_ALIGN32(T, label, ptr)       \
  do{                                           \
    if (NULL != ptr)                            \
    {                                           \
      ptr->~T();                                \
      ob_free_align(ptr);                       \
      ptr = NULL;                               \
    }                                           \
  } while(0)

#define OB_DELETEx(T, pool, ptr)                \
  do {                                          \
    if (NULL != ptr)                            \
    {                                           \
      ptr->~T();                                \
      abort_unless(!OB_ISNULL(pool));           \
      (pool)->free(ptr);                        \
      ptr = NULL;                               \
    }                                           \
  } while(0)                                    \


#endif /* OCEANBASE_SRC_COMMON_OB_MALLOC_H_ */
