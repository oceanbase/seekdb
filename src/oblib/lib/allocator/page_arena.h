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

#ifndef OCEANBASE_COMMON_PAGE_ARENA_H_
#define OCEANBASE_COMMON_PAGE_ARENA_H_

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/ob_memory_sanity.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/ob_define.h"
#include "lib/utility/ob_bits_utils.h"
#include "lib/utility/ob_mod_define.h"
#include "lib/utility/ob_utility.h"
namespace oceanbase
{
namespace common
{

inline int64_t sys_page_size()
{
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  static int64_t sz = si.dwPageSize;
  return sz;
#else
  static int64_t sz = sysconf(_SC_PAGE_SIZE);
  return sz;
#endif
}

// convenient function for memory alignment
inline size_t get_align_offset(void *p, const int64_t alignment)
{
  assert(alignment > 0 && alignment < UINT32_MAX);
  assert(ob_is_power_of_two(static_cast<uint32_t>(alignment)));
  return (alignment - (((uint64_t)p) & (alignment - 1))) & (alignment - 1);
}

struct DefaultPageAllocator: public ObIAllocator
{
  DefaultPageAllocator(const lib::ObLabel &label = ObModIds::OB_PAGE_ARENA)
    : attr_(label) {};
  DefaultPageAllocator(const lib::ObMemAttr &attr)
    : attr_(attr) {};
  virtual ~DefaultPageAllocator() {};
  void *alloc(const int64_t sz)
  {
    return alloc(sz, attr_);
  }
  void *alloc(const int64_t size, const ObMemAttr &attr)
  {
    return ob_malloc(size, attr);
  }
  void free(void *p) { ob_free(p); }
  void freed(const int64_t sz) {UNUSED(sz); /* mostly for effcient bulk stat reporting */ }
  void set_label(const lib::ObLabel &label) {attr_.label_ = label;};
  
  void set_ctx_id(int64_t ctx_id) { attr_.ctx_id_ = ctx_id; }
  void set_attr(const lib::ObMemAttr &attr) { attr_ = attr; }
  lib::ObLabel get_label() const { return attr_.label_; };
  void *mod_alloc(const int64_t sz, const lib::ObLabel &label)
  {
    ObMemAttr malloc_attr = attr_;
    malloc_attr.label_ = label;
    return ob_malloc(sz, malloc_attr);
  }
private:
  lib::ObMemAttr attr_;
};

struct ModulePageAllocator: public ObIAllocator
{
  ModulePageAllocator(const lib::ObLabel &label = ObModIds::OB_MODULE_PAGE_ALLOCATOR,
                      int64_t ctx_id = 0)
    : ModulePageAllocator(ObMemAttr(label, ctx_id)) {}
  ModulePageAllocator(const lib::ObMemAttr &attr)
    : allocator_(NULL), attr_(attr) {}
  ModulePageAllocator(const ModulePageAllocator &that)
    : allocator_(that.allocator_), attr_(that.attr_) {}
  explicit ModulePageAllocator(ObIAllocator &allocator,
                               const lib::ObLabel &label = ObModIds::OB_MODULE_PAGE_ALLOCATOR)
      : allocator_(&allocator), attr_()
   {
     attr_.label_ = label;
     attr_.ctx_id_ = 0;
   }
  virtual ~ModulePageAllocator() {}
  void set_label(const lib::ObLabel &label) { attr_.label_ = label; }
  
  void set_ctx_id(int64_t ctx_id) { attr_.ctx_id_ = ctx_id; }
  void set_attr(const lib::ObMemAttr &attr) { attr_ = attr; }
  const lib::ObMemAttr &get_attr() const { return attr_; }
  
  lib::ObLabel get_label() const { return attr_.label_; }
  void *alloc(const int64_t sz)
  {
    MemoryUsageTracker *tracker = resolve_memory_usage_tracker(attr_.ctx_id_);
    return nullptr != tracker
        ? tracked_alloc(sz, attr_, tracker)
        : ((nullptr != allocator_
            && !attr_.label_.is_valid()
            && 0 == attr_.ctx_id_)
                ? allocator_->alloc(sz) : alloc(sz, attr_));
  }
  void *alloc(const int64_t size, const ObMemAttr &attr)
  {
    MemoryUsageTracker *tracker = resolve_memory_usage_tracker(attr_.ctx_id_);
    return nullptr != tracker
        ? tracked_alloc(size, attr, tracker)
        : ((NULL == allocator_) ? ob_malloc(size, attr) : allocator_->alloc(size, attr));
  }
  void free(void *p)
  {
    MemoryUsageTracker *tracker = resolve_memory_usage_tracker(attr_.ctx_id_);
    if (nullptr != tracker) {
      tracked_free(p, tracker);
    } else if (NULL == allocator_) {
      ob_free(p);
    } else {
      allocator_->free(p);
    }
    p = NULL;
  }
  void freed(const int64_t sz) {UNUSED(sz); /* mostly for effcient bulk stat reporting */ }
  void set_allocator(ObIAllocator *allocator) { allocator_ = allocator; }
  ModulePageAllocator &operator=(const ModulePageAllocator &that) {
    if (this != &that) {
      allocator_ = that.allocator_;
      attr_ = that.attr_;
    }
    return *this;
  }
protected:
  void *tracked_alloc(const int64_t size,
                      const ObMemAttr &attr,
                      MemoryUsageTracker *tracker)
  {
    void *ptr = nullptr;
    if (NULL == allocator_) {
      ObMalloc fallback_allocator(attr);
      TrackedAllocator tracked_allocator(fallback_allocator, tracker, attr);
      ptr = tracked_allocator.alloc(size, attr);
    } else {
      TrackedAllocator tracked_allocator(*allocator_, tracker, attr);
      ptr = tracked_allocator.alloc(size, attr);
    }
    return ptr;
  }

  void tracked_free(void *ptr, MemoryUsageTracker *tracker)
  {
    if (NULL == allocator_) {
      ObMalloc fallback_allocator(attr_);
      TrackedAllocator tracked_allocator(fallback_allocator, tracker, attr_);
      tracked_allocator.free(ptr);
    } else {
      TrackedAllocator tracked_allocator(*allocator_, tracker, attr_);
      tracked_allocator.free(ptr);
    }
  }

  ObIAllocator *allocator_;
  lib::ObMemAttr attr_;
};

/**
 * A simple/fast allocator to avoid individual deletes/frees
 * Good for usage patterns that just:
 * load, use and free the entire container repeatedly.
 */
template <typename CharT = char, class PageAllocatorT = DefaultPageAllocator>
class PageArena
{
private: // types
  typedef PageArena<CharT, PageAllocatorT> Self;

  struct Page
  {
    static constexpr uint64_t MAGIC = 0x1234abcddbca4321;
    bool check_magic_code() { return MAGIC == magic_; }
    uint64_t magic_;
    Page *next_page_;
    char *alloc_end_;
    const char *page_end_;
    char buf_[0];

    Page() : magic_(MAGIC), next_page_(0), alloc_end_(), page_end_()
    {}
    explicit Page(const char *end)
        : magic_(MAGIC), next_page_(0), alloc_end_(), page_end_(end)
    {
      alloc_end_ = buf_;
    }

    inline int64_t remain() const { return page_end_ - alloc_end_; }
    inline int64_t used() const { return alloc_end_ - buf_ ; }
    inline int64_t raw_size() const { return page_end_ - buf_ + sizeof(Page); }
    inline int64_t reuse_size() const { return page_end_ - buf_; }

    inline CharT *alloc(int64_t sz)
    {
      CharT *ret = NULL;
      if (sz <= 0) {
        //alloc size is invalid
      } else if (sz <= remain()) {
        char *start = alloc_end_;
        alloc_end_ += sz;
        ret = (CharT *) start;
      }
      return ret;
    }

    inline CharT *alloc_down(int64_t sz)
    {
      page_end_ -= sz;
      return (CharT *)page_end_;
    }

    inline void reuse()
    {
      alloc_end_ = buf_;
    }
  };

  struct TracerContext {
    TracerContext()
        : header_(nullptr),
          cur_page_(),
          pages_(),
          used_(),
          total_()
    {}
    Page *header_;
    Page cur_page_;
    int64_t pages_;
    int64_t used_;
    int64_t total_;
  };

public:
  static const int64_t DEFAULT_PAGE_SIZE = OB_MALLOC_NORMAL_BLOCK_SIZE - sizeof(Page); // default 8KB
  static const int64_t DEFAULT_BIG_PAGE_SIZE = OB_MALLOC_BIG_BLOCK_SIZE; // default 2M

private: // data
  Page *cur_page_;
  Page *header_;
  Page *tailer_;
  int64_t page_limit_;  // capacity in bytes of an empty page
  int64_t page_size_;   // page size in number of bytes
  int64_t pages_;       // number of pages allocated
  int64_t used_;        // total number of bytes allocated by users
  int64_t total_;       // total number of bytes occupied by pages
  PageAllocatorT page_allocator_;
  TracerContext *tc_;
  bool enable_sanity_;
private: // helpers
  CharT *alloc_aligned_from_page(Page *page, const int64_t size,
                                 const int64_t alignment,
                                 int64_t &consumed_size) {
    CharT *ret = NULL;
    consumed_size = 0;
    if (NULL != page && size > 0) {
      const int64_t align_offset =
          get_align_offset(page->alloc_end_, alignment);
      if (size <= INT64_MAX - align_offset) {
        const int64_t adjusted_size = size + align_offset;
        CharT *raw = page->alloc(adjusted_size);
        if (NULL != raw) {
          ret = reinterpret_cast<CharT *>(reinterpret_cast<char *>(raw) +
                                          align_offset);
          consumed_size = adjusted_size;
        }
      }
    }
    return ret;
  }

  template <typename RawAllocator>
  CharT *alloc_with_sanity(const int64_t size,
                           const int64_t requested_alignment,
                           const RawAllocator &raw_allocator) {
    CharT *ret = NULL;
    if (!memory_sanity_enabled(enable_sanity_)) {
      ret = raw_allocator(size, requested_alignment);
    } else {
      SanityAllocLayout layout;
      if (memory_sanity_prepare_allocation(size, requested_alignment, layout)) {
        ret = raw_allocator(layout.storage_size_, layout.alignment_);
        if (NULL != ret) {
          memory_sanity_mark_allocated(ret, layout);
        }
      }
    }
    return ret;
  }

  const char *sanity_page_end(const Page *page) const
  {
    const char *normal_page_end = reinterpret_cast<const char *>(page) + page_size_;
    return page->page_end_ < normal_page_end ? normal_page_end : page->page_end_;
  }

  Page *insert_head(Page *page)
  {
    if (OB_ISNULL(page)) {
    } else {
      if (NULL != header_) {
        page->next_page_ = header_;
      }
      header_ = page;
    }
    return page;
  }

  Page *insert_tail(Page *page)
  {
    if (NULL != tailer_) {
      tailer_->next_page_ = page;
    }
    tailer_ = page;

    return page;
  }

  Page *alloc_new_page(const int64_t sz)
  {
    Page *page = NULL;
    void *ptr = page_allocator_.alloc(sz);

    if (NULL != ptr) {
      page  = new(ptr) Page((char *)ptr + sz);
      if (memory_sanity_enabled(enable_sanity_)) {
        memory_sanity_poison(page->buf_, page->page_end_ - page->buf_);
      }
      total_  += sz;
      ++pages_;
    } else {
      _OB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED,
                  "cannot allocate memory.sz=%ld, pages_=%ld,total_=%ld",
                  sz, pages_, total_);
    }

    return page;
  }
  void free_page(Page *page)
  {
    if (memory_sanity_enabled(enable_sanity_)) {
      memory_sanity_unpoison(page->buf_, sanity_page_end(page) - page->buf_);
    }
    page_allocator_.free(page);
  }

  Page *extend_page(const int64_t sz)
  {
    Page *page = cur_page_;
    if (NULL != page) {
      page = page->next_page_;
      if (NULL != page) {
        page->reuse();
      } else {
        page = alloc_new_page(sz);
        if (NULL == page) {
          _OB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED,
                      "extend_page sz =%ld cannot alloc new page", sz);
        } else {
          insert_tail(page);
        }
      }
    }
    return page;
  }

  inline bool lookup_next_page(const int64_t sz)
  {
    bool ret = false;
    if (NULL != cur_page_
        && NULL != cur_page_->next_page_
        && cur_page_->next_page_->reuse_size() >= sz) {
      cur_page_->next_page_->reuse();
      cur_page_ = cur_page_->next_page_;
      ret = true;
    }
    return ret;
  }

  inline bool ensure_cur_page()
  {
    if (NULL == cur_page_) {
      if (OB_LIKELY(NULL != (cur_page_ = alloc_new_page(page_size_)))) {
        Page **cur = &header_;
        while (*cur) {
          cur = &(*cur)->next_page_;
        }
        abort_unless(NULL == tailer_);
        *cur = tailer_ = cur_page_;
        page_limit_ = cur_page_->remain();
      }
    }

    return (NULL != cur_page_);
  }

  inline bool is_normal_overflow(const int64_t sz) const
  {
    return sz <= page_limit_;
  }

  inline bool is_large_page(const Page *page) const
  {
    return NULL == page ? false : page->raw_size() > page_size_;
  }

  CharT *alloc_big(const int64_t sz)
  {
    CharT *ptr = NULL;
    // big enough object to have their own page
    Page *p = alloc_new_page(sz + sizeof(Page));
    if (NULL != p) {
      insert_head(p);
      ptr = p->alloc(sz);
    }
    return ptr;
  }

  void free_large_pages()
  {
    Page **current = &header_;
    while (NULL != *current) {
      Page *entry = *current;
      abort_unless(entry->check_magic_code());
      if (is_large_page(entry)) {
        *current = entry->next_page_;
        pages_ -= 1;
        total_ -= entry->raw_size();
        free_page(entry);
        entry = NULL;
      } else {
        tailer_ = *current;
        current = &entry->next_page_;
      }

    }
    if (NULL == header_) {
      tailer_ = NULL;
    }
  }

  Self &assign(Self &rhs)
  {
    if (this != &rhs) {
      free();

      header_ = rhs.header_;
      cur_page_ = rhs.cur_page_;
      tailer_ = rhs.tailer_;

      pages_ = rhs.pages_;
      used_ = rhs.used_;
      total_ = rhs.total_;
      page_size_  = rhs.page_size_;
      page_limit_ = rhs.page_limit_;
      page_allocator_ = rhs.page_allocator_;
      enable_sanity_ = rhs.enable_sanity_;

    }
    return *this;
  }

public: // API
  /** constructor */
  PageArena(const int64_t page_size,
            const PageAllocatorT &alloc,
            const bool enable_sanity = false)
      : cur_page_(NULL), header_(NULL), tailer_(NULL),
        page_limit_(0), page_size_(page_size),
        pages_(0), used_(0), total_(0), page_allocator_(alloc), tc_(nullptr),
        enable_sanity_(enable_sanity)
  {
    if (page_size < (int64_t)sizeof(Page)) {
      _OB_LOG_RET(ERROR, OB_ERROR, "invalid page size(page_size=%ld, page=%ld)", page_size,
              (int64_t)sizeof(Page));
    }
  }
  PageArena(const int64_t page_size)
    : PageArena(page_size, PageAllocatorT(), true) {}
  PageArena() : PageArena(DEFAULT_PAGE_SIZE) {}
  virtual ~PageArena() { free(); }

  int init(const int64_t page_size, PageAllocatorT &alloc)
  {
    int ret = OB_SUCCESS;
    if (page_size < (int64_t)sizeof(Page)) {
      _OB_LOG(ERROR, "invalid page size(page_size=%ld, page=%ld)", page_size,
              (int64_t)sizeof(Page));
    } else {
      page_size_ = page_size;
      page_allocator_ = alloc;
    }
    return ret;
  }

  int mprotect_page_arena(int prot)
  {
    int ret = OB_SUCCESS;
    Page *page = NULL;
    Page *curr = header_;
    while (OB_SUCC(ret) && NULL != curr) {
      abort_unless(curr->check_magic_code());
      page = curr;
      curr = curr->next_page_;
      if (OB_FAIL(mprotect_page(page, page_size_, prot, "page_arena"))) {
      }
    }
    return ret;
  }

  Self &join(Self &rhs)
  {
    if (this != &rhs && rhs.used_ == 0) {
      if (NULL == header_) {
        assign(rhs);
      } else if (NULL != rhs.header_ && NULL != tailer_) {
        tailer_->next_page_ = rhs.header_;
        tailer_ = rhs.tailer_;

        pages_ += rhs.pages_;
        total_ += rhs.total_;
      }
      rhs.reset();
    }
    return *this;
  }

  int64_t page_size() const { return page_size_; }

  void set_label(const lib::ObLabel &label) { page_allocator_.set_label(label); }
  lib::ObLabel get_label() const { return page_allocator_.get_label(); }
  
  
  void set_ctx_id(int64_t ctx_id) { page_allocator_.set_ctx_id(ctx_id); }
  void set_attr(const lib::ObMemAttr &attr) { page_allocator_.set_attr(attr); }
  const PageAllocatorT &get_page_allocator() { return page_allocator_; }
  /** allocate sz bytes */
  CharT *_alloc(const int64_t sz)
  {
    CharT *ret = NULL;
    if (sz + sizeof(Page) <= page_size_) {
      ensure_cur_page();
      // common case
      if (NULL != cur_page_ && sz > 0) {
        if (sz <= cur_page_->remain()) {
          ret = cur_page_->alloc(sz);
        } else if (is_normal_overflow(sz)) {
          Page *new_page = extend_page(page_size_);
          if (NULL != new_page) {
            cur_page_ = new_page;
          }
          if (NULL != cur_page_) {
            ret = cur_page_->alloc(sz);
          }
        } else if (lookup_next_page(sz)) {
          ret = cur_page_->alloc(sz);
        } else {
          ret = alloc_big(sz);
        }
      }
    } else {
      ret = alloc_big(sz);
    }

    if (NULL != ret) {
      used_ += sz;
    }
    return ret;
  }
  CharT *alloc(const int64_t sz)
  {
    return alloc_with_sanity(sz, 0,
                             [this](const int64_t raw_size, const int64_t) {
                               return _alloc(raw_size);
                             });
  }
  CharT *alloc(const int64_t sz, const lib::ObMemAttr &attr)
  {
    UNUSED(attr);
    return alloc(sz);
  }

  template<class T>
  T *new_object()
  {
    T *ret = NULL;
    void *tmp = (void *)alloc_aligned(sizeof(T));
    if (NULL == tmp) {
      _OB_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "fail to alloc mem for T");
    } else {
      ret = new(tmp)T();
    }
    return ret;
  }

  /** allocate sz bytes */
  CharT *_alloc_aligned(const int64_t sz, const int64_t alignment = 16)
  {
    CharT *ret = NULL;
    assert(alignment > 0 && alignment < UINT32_MAX);
    assert(ob_is_power_of_two(static_cast<uint32_t>(alignment)));
    if (sz > 0 && sz <= INT64_MAX - (alignment - 1)) {
      const int64_t max_adjusted_size = sz + alignment - 1;
      ensure_cur_page();
      if (NULL != cur_page_) {
        int64_t consumed_size = 0;
        ret = alloc_aligned_from_page(cur_page_, sz, alignment, consumed_size);
        if (NULL == ret && is_normal_overflow(max_adjusted_size)) {
          Page *new_page = extend_page(page_size_);
          if (NULL != new_page) {
            cur_page_ = new_page;
          }
          if (NULL != cur_page_) {
            ret = alloc_aligned_from_page(cur_page_, sz, alignment,
                                          consumed_size);
          }
        } else if (NULL == ret && lookup_next_page(max_adjusted_size)) {
          ret =
              alloc_aligned_from_page(cur_page_, sz, alignment, consumed_size);
        }
        if (NULL == ret) {
          CharT *raw = alloc_big(max_adjusted_size);
          if (NULL != raw) {
            const int64_t align_offset = get_align_offset(raw, alignment);
            ret = reinterpret_cast<CharT *>(reinterpret_cast<char *>(raw) +
                                            align_offset);
            consumed_size = max_adjusted_size;
          }
        }
        if (NULL != ret) {
          used_ += consumed_size;
        }
      }
    }
    return ret;
  }

  CharT *alloc_aligned(const int64_t sz, const int64_t alignment = 16)
  {
    return alloc_with_sanity(
        sz, alignment,
        [this](const int64_t raw_size, const int64_t raw_alignment) {
          return _alloc_aligned(raw_size, raw_alignment);
        });
  }

  /**
   * allocate from the end of the page.
   * - allow better packing/space saving for certain scenarios
   */
  CharT *_alloc_down(const int64_t sz)
  {
    // common case
    CharT *ret = NULL;
    if (sz + sizeof(Page) <= page_size_) {
      ensure_cur_page();
      if (NULL != cur_page_ && sz > 0) {
        if (sz <= cur_page_->remain()) {
          ret = cur_page_->alloc_down(sz);
        } else if (is_normal_overflow(sz)) {
          Page *new_page = extend_page(page_size_);
          if (NULL != new_page) {
            cur_page_ = new_page;
          }
          if (NULL != cur_page_) {
            ret = cur_page_->alloc_down(sz);
          }
        } else if (lookup_next_page(sz)) {
          ret = cur_page_->alloc_down(sz);
        } else {
          ret = alloc_big(sz);
        }
      }
    } else {
      ret = alloc_big(sz);
    }

    if(NULL != ret){
      used_ += sz;
    }
    return ret;
  }

  CharT *alloc_down(const int64_t sz)
  {
    return alloc_with_sanity(sz, 0,
                             [this](const int64_t raw_size, const int64_t) {
                               return _alloc_down(raw_size);
                             });
  }

  /** realloc for newsz bytes */
  CharT *realloc(CharT *p, const int64_t oldsz, const int64_t newsz)
  {
    CharT *ret = NULL;
    if (OB_ISNULL(cur_page_)) {
    } else {
      ret = p;
      // if we're the last one on the current page with enough space
      if (!memory_sanity_enabled(enable_sanity_) &&
          reinterpret_cast<char *>(p) + oldsz == cur_page_->alloc_end_ &&
          reinterpret_cast<char *>(p) + newsz < cur_page_->page_end_) {
        cur_page_->alloc_end_ = (char *)p + newsz;
        ret = p;
      } else {
        ret = alloc(newsz);
        if (NULL != ret) {
          MEMCPY(ret, p, newsz > oldsz ? oldsz : newsz);
        }
      }
    }
    return ret;
  }

  /** duplicate a null terminated string s */
  CharT *dup(const char *s)
  {
    if (NULL == s) { return NULL; }

    int64_t len = strlen(s) + 1;
    CharT *copy = alloc(len);
    if (NULL != copy) {
      MEMCPY(copy, s, len);
    }
    return copy;
  }

  /** duplicate a buffer of size len */
  CharT *dup(const void *s, const int64_t len)
  {
    CharT *copy = NULL;
    if (NULL != s && len > 0) {
      copy = alloc(len);
      if (NULL != copy) {
        MEMCPY(copy, s, len);
      }
    }

    return copy;
  }

  /**
   * Aligned allocate sz bytes using best-fit strategy.
   *
   * @param sz
   * @param alignment which should be power of 2
   *
   * @return nullptr when failed
   */
  CharT *_alloc_aligned_bf(const int64_t sz, const int64_t alignment)
  {
    assert(alignment > 0 && alignment < UINT32_MAX);
    assert(ob_is_power_of_two(static_cast<uint32_t>(alignment)));
    CharT *ret = nullptr;
    if (sz > 0 && sz <= INT64_MAX - (alignment - 1)) {
      const int64_t max_adjusted_size = sz + alignment - 1;
      ensure_cur_page();
      // find the best page
      Page *page = header_;
      Page *best_page = nullptr;
      int64_t best_remain = 0;
      while (NULL != page) {
        const int64_t align_offset =
            get_align_offset(page->alloc_end_, alignment);
        const int64_t adjusted_size = sz + align_offset;
        if (adjusted_size <= page->remain()) {
          if (nullptr == best_page ||
              page->remain() - adjusted_size < best_remain) {
            best_page = page;
            best_remain = page->remain() - adjusted_size;
          }
        }
        page = page->next_page_;
      }

      int64_t consumed_size = 0;
      if (nullptr != best_page) {
        // found one page that best fits the aligned allocation
        ret = alloc_aligned_from_page(best_page, sz, alignment, consumed_size);
      } else if (is_normal_overflow(max_adjusted_size)) {
        Page *new_page = extend_page(page_size_);
        if (NULL != new_page) {
          cur_page_ = new_page;
          ret =
              alloc_aligned_from_page(cur_page_, sz, alignment, consumed_size);
        }
      }
      if (nullptr == ret) {
        CharT *raw = alloc_big(max_adjusted_size);
        if (nullptr != raw) {
          const int64_t align_offset = get_align_offset(raw, alignment);
          ret = reinterpret_cast<CharT *>(reinterpret_cast<char *>(raw) +
                                          align_offset);
          consumed_size = max_adjusted_size;
        }
      }
      if (nullptr != ret) {
        used_ += consumed_size;
      }
    }
    return ret;
  }

  CharT *alloc_aligned_bf(const int64_t sz, const int64_t alignment)
  {
    return alloc_with_sanity(
        sz, alignment,
        [this](const int64_t raw_size, const int64_t raw_alignment) {
          return _alloc_aligned_bf(raw_size, raw_alignment);
        });
  }


  /** free the whole arena */
  void free()
  {
    Page *page = NULL;

    while (NULL != header_) {
      abort_unless(header_->check_magic_code());
      page = header_;
      header_ = header_->next_page_;
      free_page(page);
      page = NULL;
    }
    page_allocator_.freed(total_);

    cur_page_ = NULL;
    tailer_ = NULL;
    used_ = 0;
    pages_ = 0;
    total_ = 0;
    tc_ = nullptr;
  }

  /** free the arena and remain one normal page */
  void free_remain_one_page()
  {
    Page *page = NULL;
    Page *remain_page = NULL;
    while (NULL != header_) {
      abort_unless(header_->check_magic_code());
      page = header_;
      if (NULL == remain_page && !is_large_page(page)) {
        remain_page = page;
        header_ = header_->next_page_;
      } else {
        header_ = header_->next_page_;
        free_page(page);
      }
      page = NULL;
    }
    if (NULL != remain_page && memory_sanity_enabled(enable_sanity_)) {
      memory_sanity_poison(remain_page->buf_,
                           sanity_page_end(remain_page) - remain_page->buf_);
    }
    header_ = cur_page_ = remain_page;
    if (NULL == cur_page_) {
      page_allocator_.freed(total_);
      total_ = 0;
      pages_ = 0;
    } else {
      cur_page_->next_page_ = NULL;
      page_allocator_.freed(total_ - cur_page_->raw_size());
      cur_page_->reuse();
      total_ = cur_page_->raw_size();
      pages_ = 1;
    }
    tailer_ = cur_page_;
    used_ = 0;
    tc_ = nullptr;
  }
  /**
   * free some of pages. remain memory can be reuse.
   *
   * @param sleep_pages force sleep when pages are freed every time.
   * @param sleep_interval_us sleep interval in microseconds.
   * @param remain_size keep size of memory pages less than %remain_size
   *
   */
  void partial_slow_free(const int64_t sleep_pages,
                         const int64_t sleep_interval_us, const int64_t remain_size = 0)
  {
    Page *page = NULL;

    int64_t current_sleep_pages = 0;

    while (NULL != header_ && (remain_size == 0 || total_ > remain_size)) {
      abort_unless(header_->check_magic_code());
      page = header_;
      header_ = header_->next_page_;

      total_ -= page->raw_size();

      free_page(page);

      ++current_sleep_pages;
      --pages_;

      if (sleep_pages > 0 && current_sleep_pages >= sleep_pages) {
#ifdef _WIN32
        // Windows: Sleep takes milliseconds
        ::Sleep(static_cast<DWORD>(sleep_interval_us / 1000));
#else
        ::usleep(static_cast<useconds_t>(sleep_interval_us));
#endif
        current_sleep_pages = 0;
      }
    }

    // reset allocate start point, important.
    // once slow_free called, all memory allocated before
    // CANNOT use anymore.
    cur_page_ = header_;
    if (NULL == header_) { tailer_ = NULL; }
    if (memory_sanity_enabled(enable_sanity_)) {
      Page *remain_page = header_;
      while (NULL != remain_page) {
        memory_sanity_poison(remain_page->buf_,
                             sanity_page_end(remain_page) - remain_page->buf_);
        remain_page = remain_page->next_page_;
      }
    }
    used_ = 0;
    tc_ = nullptr;
  }

  //[[deprecated("Arena is not allowed to call free(ptr), use free() instead")]]
  void free(CharT *ptr)
  {
    UNUSED(ptr);
  }

  // Alias for free() - used by some code
  void reset() { free(); }

  // Tracer is used to free memory in a arena allocator.  When call
  // set_tracer function, arena would record a snapshot which is used
  // to recover. As soon as responding revert_tracer function is
  // called, any follow up allocates would been freed and arena states
  // would rollback to the states of that snapshot. It's useful when
  // repeat doing something where use arena to allocate memory. Set a
  // tracer by using set_tracer function before each round of repeat and
  // invoke revert_tracer routine to free all allocates within current
  // round.
  bool set_tracer()
  {
    bool bret = true;
    if (header_ != nullptr) {
      if (OB_UNLIKELY(tc_ == nullptr)) {
        auto *pool = this;
        tc_ = OB_NEWx(TracerContext, pool);
        if (tc_ == nullptr) {
          bret = false;
        }
      }
      if (bret) {
        tc_->header_ = header_;
        tc_->cur_page_ = *cur_page_;
        tc_->cur_page_.next_page_ = cur_page_;
        tc_->pages_ = pages_;
        tc_->total_ = total_;
        tc_->used_ = used_;
      }
    }
    return bret;
  }

  bool revert_tracer()
  {
    bool bret = true;
    if (nullptr != tc_) {
      Page *traced_page = tc_->cur_page_.next_page_;
      char *traced_alloc_end = tc_->cur_page_.alloc_end_;
      const char *traced_page_end = tc_->cur_page_.page_end_;
      if (memory_sanity_enabled(enable_sanity_) && NULL != traced_page) {
        memory_sanity_poison(traced_alloc_end,
                             traced_page->alloc_end_ - traced_alloc_end);
        memory_sanity_poison(traced_page->page_end_,
                             traced_page_end - traced_page->page_end_);
      }
      // Free large pages from current header to header of trace pointer.
      // Free normal pages from trace pointer page to current page.
      // Restore current page and statistics information.

      // 1. free large pages
      Page *&header = header_;
      while (header != tc_->header_ && header != nullptr) {
        abort_unless(header_->check_magic_code());
        Page *next_header = header->next_page_;
        free_page(header);
        header = next_header;
      }

      // 2. free normal pages
      abort_unless(tc_->cur_page_.check_magic_code());
      tailer_ = cur_page_ = tc_->cur_page_.next_page_;
      Page *&page = cur_page_->next_page_;
      while (page != nullptr) {
        abort_unless(page->check_magic_code());
        Page *next_page = page->next_page_;
        free_page(page);
        page = next_page;
      }
      cur_page_->alloc_end_ = traced_alloc_end;
      cur_page_->page_end_ = traced_page_end;

      // 3. restore statistics
      pages_ = tc_->pages_;
      total_ = tc_->total_;
      used_ = tc_->used_;
    } else {
      // There are two cases for tc_ == nullptr
      // 1. Set_tracer has not been adjusted;
      // 2. I adjusted set_tracer but did not apply for any page at that time
      // The premise of the revert function call is that the user has adjusted set_tracer, so all pages need to be released here
      free();
    }
    return bret;
  }

  void fast_reuse()
  {
    if (memory_sanity_enabled(enable_sanity_)) {
      Page *page = header_;
      while (NULL != page) {
        memory_sanity_poison(page->buf_, sanity_page_end(page) - page->buf_);
        page = page->next_page_;
      }
    }
    used_ = 0;
    cur_page_ = header_;
    if (NULL != cur_page_) {
      cur_page_->reuse();
    }
    tc_ = nullptr;
  }

  void reuse()
  {
    free_large_pages();
    fast_reuse();
  }

  void dump() const
  {
    Page *page = header_;
    int64_t count = 0;
    while (NULL != page) {
      abort_unless(page->check_magic_code());
      _OB_LOG(INFO, "DUMP PAGEARENA page[%ld]:rawsize[%ld],used[%ld],remain[%ld]",
                count++, page->raw_size(), page->used(), page->remain());
      page = page->next_page_;
    }
  }

  /** stats accessors */
  int64_t pages() const { return pages_; }
  int64_t used() const { return used_; }
  int64_t total() const { return total_; }
private:
  DISALLOW_COPY_AND_ASSIGN(PageArena);
};

typedef PageArena<> CharArena;
typedef PageArena<unsigned char> ByteArena;
typedef PageArena<char, ModulePageAllocator> ModuleArena;

class ObArenaAllocator final : public ObIAllocator
{
public:
  ObArenaAllocator(const lib::ObLabel &label = ObModIds::OB_MODULE_PAGE_ALLOCATOR,
                   const int64_t page_size = OB_MALLOC_NORMAL_BLOCK_SIZE,
                   int64_t ctx_id = 0)
    : arena_(page_size, ModulePageAllocator(label, ctx_id), true), tracker_(nullptr) {}
  ObArenaAllocator(ObIAllocator &allocator,
                   const int64_t page_size = OB_MALLOC_NORMAL_BLOCK_SIZE,
                   const bool enable_sanity = false)
    : arena_(page_size, ModulePageAllocator(allocator), enable_sanity), tracker_(nullptr) {};
  ObArenaAllocator(const lib::ObMemAttr &attr,
                   const int64_t page_size = OB_MALLOC_NORMAL_BLOCK_SIZE)
    : arena_(page_size, ModulePageAllocator(attr), true), tracker_(nullptr) {}
  virtual ~ObArenaAllocator()
  {
    update_tracker(-arena_.total());
  };
public:
  virtual void *alloc(const int64_t sz)
  {
    const int64_t old_total = arena_.total();
    void *ptr = arena_.alloc_aligned(sz);
    update_tracker(arena_.total() - old_total);
    return ptr;
  }
  void *alloc(const int64_t size, const ObMemAttr &attr)
  {
    UNUSED(attr);
    return alloc(size);
  }
  virtual void *alloc_aligned(const int64_t sz, const int64_t align)
  {
    const int64_t old_total = arena_.total();
    void *ptr = arena_.alloc_aligned(sz, align);
    update_tracker(arena_.total() - old_total);
    return ptr;
  }
  virtual void *realloc(void *ptr, const int64_t oldsz, const int64_t newsz)
  {
    const int64_t old_total = arena_.total();
    void *new_ptr = arena_.realloc(reinterpret_cast<char*>(ptr), oldsz, newsz);
    update_tracker(arena_.total() - old_total);
    return new_ptr;
  }
  //[[deprecated("Arena is not allowed to call free(ptr), use clear() instead")]]
  virtual void free(void *ptr) { UNUSED(ptr); }
  virtual void clear()
  {
    const int64_t old_total = arena_.total();
    arena_.free();
    update_tracker(arena_.total() - old_total);
  }
  int64_t used() const { return arena_.used(); }
  int64_t total() const { return arena_.total(); }
  void reset() { clear(); }
  void reset_remain_one_page()
  {
    const int64_t old_total = arena_.total();
    arena_.free_remain_one_page();
    update_tracker(arena_.total() - old_total);
  }
  void reuse() override { arena_.reuse(); }
  virtual void set_label(const lib::ObLabel &label) { arena_.set_label(label); }
  virtual lib::ObLabel get_label() const { return arena_.get_label(); }
  
  bool set_tracer() { return arena_.set_tracer(); }
  bool revert_tracer()
  {
    const int64_t old_total = arena_.total();
    const bool reverted = arena_.revert_tracer();
    update_tracker(arena_.total() - old_total);
    return reverted;
  }
  void set_ctx_id(int64_t ctx_id) { arena_.set_ctx_id(ctx_id); }
  void set_memory_tracker(MemoryUsageTracker *tracker)
  {
    if (tracker_ != tracker) {
      update_tracker(-arena_.total());
      tracker_ = tracker;
      update_tracker(arena_.total());
    }
  }
  void set_attr(const ObMemAttr &attr) override
  {
    arena_.set_attr(attr);
  }
  ModuleArena &get_arena() { return arena_; }
  int64_t to_string(char *buf, int64_t len) const
  {
    int64_t printed = snprintf(buf, len, "pages=%ld, used=%ld, total=%ld",
        arena_.pages(), arena_.used(), arena_.total());
    return printed < len ? printed : len;
  }
  int mprotect_arena_allocator(int prot) { return arena_.mprotect_page_arena(prot); }
private:
  void update_tracker(const int64_t delta)
  {
    if (nullptr != tracker_ && 0 != delta) {
      tracker_->adjust(delta);
    }
  }

  ModuleArena arena_;
  MemoryUsageTracker *tracker_;
};

class ObSafeArenaAllocator : public ObIAllocator
{
public:
  ObSafeArenaAllocator(ObArenaAllocator &arena)
      : arena_(arena),
        lock_(ObLatchIds::OB_AREAN_ALLOCATOR_LOCK)
  {}
  virtual ~ObSafeArenaAllocator() {}
public:
  void *alloc(const int64_t sz) override
  {
    ObSpinLockGuard guard(lock_);
    return arena_.alloc(sz);
  }
  void *alloc(const int64_t sz, const ObMemAttr &attr) override
  {
    ObSpinLockGuard guard(lock_);
    return arena_.alloc(sz, attr);
  }
  //[[deprecated("Arena is not allowed to call free(ptr), use clear() instead")]]
  void free(void *ptr) override
  {
    UNUSED(ptr);
  }
  void clear()
  {
    ObSpinLockGuard guard(lock_);
    arena_.clear();
  }
  void reuse() override
  {
    ObSpinLockGuard guard(lock_);
    arena_.reuse();
  }
  int64_t total() const override
  {
    return arena_.total();
  }
  int64_t used() const override
  {
    return arena_.used();
  }
private:
  ObArenaAllocator &arena_;
  ObSpinLock lock_;
};

class ObAlignedArenaAllocator: public ObIAllocator
{
public:
  ObAlignedArenaAllocator(const int64_t alignment,
                          const lib::ObLabel &label = ObModIds::OB_MODULE_PAGE_ALLOCATOR,
                          const int64_t page_size = OB_MALLOC_NORMAL_BLOCK_SIZE,
                          int64_t ctx_id = 0)
      :arena_(page_size, ModulePageAllocator(label, ctx_id), true),
       alignment_(alignment)
  {}
  virtual ~ObAlignedArenaAllocator() = default;
  DISABLE_COPY_ASSIGN(ObAlignedArenaAllocator);

  virtual void *alloc(const int64_t size) override
  {
    return arena_.alloc_aligned_bf(size, alignment_);
  }

  virtual void* alloc(const int64_t size, const ObMemAttr &attr) override
  {
    UNUSED(attr);
    return arena_.alloc_aligned_bf(size, alignment_);
  }

  //[[deprecated("Arena is not allowed to call free(ptr), use reset() instead")]]
  virtual void free(void *ptr) override { UNUSED(ptr); }
  virtual int64_t total() const override { return arena_.total(); }
  virtual int64_t used() const override{ return arena_.used();}
  virtual void reset() override { arena_.free(); }
  virtual void reuse() override { arena_.reuse(); }

  virtual void set_attr(const ObMemAttr &attr) override
  {
    arena_.set_attr(attr);
  }
private:
  ModuleArena arena_;
  int64_t alignment_;
};

} // end namespace common
} // end namespace oceanbase

#endif // end if OCEANBASE_COMMON_PAGE_ARENA_H_
