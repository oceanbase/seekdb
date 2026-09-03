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
#include <mutex>
#include <stdint.h>
#include "lib/ob_abort.h"
#include "lib/alloc/alloc_func.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/allocator/ob_jemalloc.h"
#include "lib/allocator/ob_tc_malloc.h"
#include "lib/time/ob_time_utility.h"
#include "lib/alloc/ob_malloc_allocator.h"

namespace oceanbase
{
namespace common
{

bool restore_allocator_after_fork();
#if defined(__APPLE__) && defined(OB_HAVE_BUNDLED_JEMALLOC)
bool configure_darwin_malloc_zone();
#endif

inline void ob_print_mod_memory_usage(bool print_to_std = false,
                                      bool print_glibc_malloc_stats = false)
{
  UNUSEDx(print_to_std, print_glibc_malloc_stats);
}

inline void *ob_malloc(const int64_t nbyte, const ObMemAttr &attr)
{
  void *ptr = NULL;
  auto *allocator = lib::ObMallocAllocator::get_instance();
  if (OB_NOT_NULL(allocator)) {
    ptr = allocator->alloc(nbyte, attr);
  }
  if (OB_ISNULL(ptr)) {
    LIB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED,
                "allocate memory fail", K(attr), K(nbyte));
  }
  return ptr;
}

inline void ob_free(void *ptr)
{
  auto *allocator = lib::ObMallocAllocator::get_instance();
  abort_unless(OB_NOT_NULL(allocator));
  allocator->free(ptr);
}

inline void *ob_realloc(void *ptr, const int64_t nbyte, const ObMemAttr &attr)
{
  void *nptr = NULL;
  auto *allocator = lib::ObMallocAllocator::get_instance();
  if (OB_NOT_NULL(allocator)) {
    nptr = allocator->realloc(ptr, nbyte, attr);
  }
  if (OB_ISNULL(nptr) && nbyte > 0) {
    LIB_LOG_RET(ERROR, OB_ALLOCATE_MEMORY_FAILED,
                "allocate memory fail", K(attr), K(nbyte));
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
  MemoryContextMalloc(ObIAllocator &allocator,
                      const ObMemAttr &attr,
                      const bool thread_safe)
    : allocator_(&allocator),
      attr_(attr),
      head_(nullptr),
      live_bytes_(0),
      thread_safe_(thread_safe)
  {}
  ~MemoryContextMalloc() override
  {
    reset();
  }

  void *alloc(const int64_t size) override
  {
    return alloc(size, attr_);
  }

  void *alloc(const int64_t size, const ObMemAttr &attr) override
  {
    void *ptr = nullptr;
    if (size > 0 && size <= INT64_MAX - static_cast<int64_t>(sizeof(Header))) {
      const int64_t raw_size = size + sizeof(Header);
      void *raw_ptr = allocator_->alloc(raw_size, attr);
      if (nullptr != raw_ptr) {
        Header *header = new (raw_ptr) Header(this, allocation_size(raw_ptr, raw_size));
        {
          OptionalLockGuard guard(*this);
          link_locked(header);
          live_bytes_.fetch_add(header->allocation_size_, std::memory_order_relaxed);
        }
        ptr = header + 1;
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
    if (nullptr != ptr) {
      Header *header = to_header(ptr);
      abort_unless(header->is_valid());
      header->owner_->free_owned(header);
    }
  }

  int64_t total() const override { return live_bytes_.load(std::memory_order_relaxed); }
  int64_t used() const override { return total(); }

  void reset() override
  {
    Header *list = nullptr;
    {
      OptionalLockGuard guard(*this);
      list = head_;
      head_ = nullptr;
      live_bytes_.store(0, std::memory_order_relaxed);
    }
    while (nullptr != list) {
      Header *next = list->next_;
      abort_unless(list->is_valid() && this == list->owner_);
      list->invalidate();
      allocator_->free(list);
      list = next;
    }
  }

private:
  struct alignas(std::max_align_t) Header
  {
    static constexpr uint64_t MAGIC = 0x4d434d414c4c4f43ULL;

    Header(MemoryContextMalloc *owner, const int64_t allocation_size)
      : magic_(MAGIC),
        owner_(owner),
        prev_(nullptr),
        next_(nullptr),
        allocation_size_(allocation_size)
    {}

    bool is_valid() const
    {
      return MAGIC == magic_ && nullptr != owner_ && allocation_size_ > 0;
    }

    void invalidate()
    {
      magic_ = 0;
      owner_ = nullptr;
      prev_ = nullptr;
      next_ = nullptr;
      allocation_size_ = 0;
    }

    uint64_t magic_;
    MemoryContextMalloc *owner_;
    Header *prev_;
    Header *next_;
    int64_t allocation_size_;
  };

  static_assert(0 == sizeof(Header) % alignof(std::max_align_t),
                "MemoryContextMalloc must preserve malloc alignment");

  class OptionalLockGuard
  {
  public:
    explicit OptionalLockGuard(MemoryContextMalloc &owner) : owner_(owner)
    {
      if (owner_.thread_safe_) {
        owner_.mutex_.lock();
      }
    }
    ~OptionalLockGuard()
    {
      if (owner_.thread_safe_) {
        owner_.mutex_.unlock();
      }
    }
  private:
    MemoryContextMalloc &owner_;
  };

  void *realloc_with_attr(void *ptr, const int64_t size, const ObMemAttr &attr)
  {
    void *new_ptr = nullptr;
    if (nullptr == ptr) {
      new_ptr = alloc(size, attr);
    } else if (0 == size) {
      free(ptr);
    } else if (size > 0 && size <= INT64_MAX - static_cast<int64_t>(sizeof(Header))) {
      Header *header = to_header(ptr);
      abort_unless(header->is_valid());
      new_ptr = header->owner_->realloc_owned(header, size, attr);
    }
    return new_ptr;
  }

  void *realloc_owned(Header *header, const int64_t size, const ObMemAttr &attr)
  {
    abort_unless(nullptr != header && header->is_valid() && this == header->owner_);
    const int64_t old_allocation_size = header->allocation_size_;
    {
      OptionalLockGuard guard(*this);
      unlink_locked(header);
    }

    const int64_t raw_size = size + sizeof(Header);
    void *raw_ptr = allocator_->realloc(header, raw_size, attr);
    if (nullptr == raw_ptr) {
      OptionalLockGuard guard(*this);
      link_locked(header);
      return nullptr;
    }

    Header *new_header = new (raw_ptr) Header(this, allocation_size(raw_ptr, raw_size));
    {
      OptionalLockGuard guard(*this);
      link_locked(new_header);
      live_bytes_.fetch_add(new_header->allocation_size_ - old_allocation_size,
                            std::memory_order_relaxed);
    }
    return new_header + 1;
  }

  void free_owned(Header *header)
  {
    abort_unless(nullptr != header && header->is_valid() && this == header->owner_);
    const int64_t old_allocation_size = header->allocation_size_;
    {
      OptionalLockGuard guard(*this);
      unlink_locked(header);
      live_bytes_.fetch_sub(old_allocation_size, std::memory_order_relaxed);
      header->invalidate();
    }
    allocator_->free(header);
  }

  static Header *to_header(void *ptr)
  {
    return static_cast<Header *>(ptr) - 1;
  }

  static int64_t allocation_size(void *raw_ptr, const int64_t requested_size)
  {
    const int64_t usable_size = ob_malloc_usable_size(raw_ptr);
    return usable_size > 0 ? usable_size : requested_size;
  }

  void link_locked(Header *header)
  {
    abort_unless(nullptr != header && header->is_valid() && this == header->owner_);
    header->prev_ = nullptr;
    header->next_ = head_;
    if (nullptr != head_) {
      head_->prev_ = header;
    }
    head_ = header;
  }

  void unlink_locked(Header *header)
  {
    abort_unless(nullptr != header && header->is_valid() && this == header->owner_);
    if (nullptr != header->prev_) {
      header->prev_->next_ = header->next_;
    } else {
      abort_unless(head_ == header);
      head_ = header->next_;
    }
    if (nullptr != header->next_) {
      header->next_->prev_ = header->prev_;
    }
  }

private:
  ObIAllocator *allocator_;
  ObMemAttr attr_;
  Header *head_;
  std::atomic<int64_t> live_bytes_;
  const bool thread_safe_;
  std::mutex mutex_;
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
