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

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_NAMED_LOCK_MANAGER_H_
#define OCEANBASE_STORAGE_TABLELOCK_OB_NAMED_LOCK_MANAGER_H_

#include <map>
#include <new>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

#include "lib/allocator/ob_malloc.h"
#include "lib/charset/ob_charset.h"
#include "lib/lock/ob_thread_cond.h"
#include "storage/tablelock/ob_table_lock_owner_id.h"

namespace oceanbase
{
namespace transaction
{
namespace tablelock
{

// MySQL-compatible named locks are local to the server process and owned by a
// session. They deliberately do not use the transactional table-lock path: no
// lock state is logged, replayed, or recovered after a restart. Waiting is
// bounded by the timeout supplied by the SQL layer, so this manager does not
// maintain a deadlock wait-for graph.
class NamedLockManager final
{
public:
  struct LockSnapshot
  {
    LockSnapshot()
      : lock_name_(), lock_id_(0), owner_id_(), ref_count_(0),
        create_timestamp_(0)
    {}
    LockSnapshot(const std::string &lock_name,
                 const uint64_t lock_id,
                 const ObTableLockOwnerID &owner_id,
                 const int64_t ref_count,
                 const int64_t create_timestamp)
      : lock_name_(lock_name), lock_id_(lock_id), owner_id_(owner_id),
        ref_count_(ref_count), create_timestamp_(create_timestamp)
    {}

    std::string lock_name_;
    uint64_t lock_id_;
    ObTableLockOwnerID owner_id_;
    int64_t ref_count_;
    int64_t create_timestamp_;
  };

public:
  // Keep the boundary of the former __all_dbms_lock_allocated.name
  // VARCHAR(128) column. The legacy implementation had no lock-count limit.
  static constexpr int64_t MAX_LOCK_NAME_LENGTH = 128;
  static constexpr int64_t DEFAULT_MEMORY_LIMIT = 64L * 1024L * 1024L;
  static constexpr int64_t LOCK_NOT_EXIST_RELEASE_RESULT = -1;
  static constexpr int64_t LOCK_NOT_OWN_RELEASE_RESULT = 0;
  static constexpr int64_t LOCK_RELEASED_RESULT = 1;

  NamedLockManager();
  ~NamedLockManager();

  int init(const int64_t memory_limit = DEFAULT_MEMORY_LIMIT);
  void destroy();

  int acquire(const common::ObString &lock_name,
              const ObTableLockOwnerID &owner_id,
              const int64_t timeout_us);
  int release(const common::ObString &lock_name,
              const ObTableLockOwnerID &owner_id,
              int64_t &release_result);
  int release_all(const ObTableLockOwnerID &owner_id,
                  int64_t &release_count);
  int is_free(const common::ObString &lock_name, bool &is_free);
  int get_owner(const common::ObString &lock_name,
                ObTableLockOwnerID &owner_id);
  int has_lock(const ObTableLockOwnerID &owner_id, bool &has_lock);
  int get_lock_snapshot(std::vector<LockSnapshot> &snapshot);

private:
  // Allocate every STL node independently so erasing a lock immediately
  // returns its memory to the OceanBase allocator. The limit is checked
  // against actual live allocation bytes, including allocator rounding.
  class QuotaAllocator
  {
  public:
    QuotaAllocator()
      : allocator_(common::ObMemAttr("NamedLockMgr")), limit_(0)
    {}

    void set_limit(const int64_t limit) { limit_ = limit; }
    void reset() { limit_ = 0; }

    void *alloc(const int64_t size)
    {
      void *ptr = NULL;
      const int64_t used = allocator_.used();
      if (size > 0 && used <= limit_ && size <= limit_ - used) {
        ptr = allocator_.alloc(size);
        if (OB_NOT_NULL(ptr) && allocator_.used() > limit_) {
          allocator_.free(ptr);
          ptr = NULL;
        }
      }
      return ptr;
    }

    void free(void *ptr) { allocator_.free(ptr); }
    int64_t used() const { return allocator_.used(); }

  private:
    common::MemoryContextMalloc allocator_;
    int64_t limit_;
  };

  template <typename T>
  class StlAllocator
  {
  public:
    typedef T value_type;
    typedef std::true_type propagate_on_container_move_assignment;
    typedef std::false_type is_always_equal;
    template <typename U> struct rebind { typedef StlAllocator<U> other; };

    StlAllocator() noexcept : allocator_(NULL) {}
    explicit StlAllocator(QuotaAllocator &allocator) noexcept
      : allocator_(&allocator)
    {}
    template <typename U>
    StlAllocator(const StlAllocator<U> &other) noexcept
      : allocator_(other.allocator_)
    {}

    T *allocate(const std::size_t count)
    {
      T *ptr = NULL;
      if (OB_ISNULL(allocator_)
          || count > static_cast<std::size_t>(INT64_MAX) / sizeof(T)
          || OB_ISNULL(ptr = static_cast<T *>(allocator_->alloc(count * sizeof(T))))) {
        throw std::bad_alloc();
      }
      return ptr;
    }

    void deallocate(T *ptr, const std::size_t) noexcept
    {
      if (OB_NOT_NULL(allocator_)) {
        allocator_->free(ptr);
      }
    }

    template <typename U>
    bool operator==(const StlAllocator<U> &other) const noexcept
    {
      return allocator_ == other.allocator_;
    }
    template <typename U>
    bool operator!=(const StlAllocator<U> &other) const noexcept
    {
      return !(*this == other);
    }

  private:
    template <typename U> friend class StlAllocator;
    QuotaAllocator *allocator_;
  };

  typedef StlAllocator<char> LockCharAllocator;
  typedef std::basic_string<char, std::char_traits<char>, LockCharAllocator> LockName;

  struct LockNameLess
  {
    typedef void is_transparent;

    bool operator()(const LockName &lhs, const LockName &rhs) const
    {
      return less(lhs.data(), lhs.length(), rhs.data(), rhs.length());
    }
    bool operator()(const LockName &lhs, const common::ObString &rhs) const
    {
      return less(lhs.data(), lhs.length(), rhs.ptr(), rhs.length());
    }
    bool operator()(const common::ObString &lhs, const LockName &rhs) const
    {
      return less(lhs.ptr(), lhs.length(), rhs.data(), rhs.length());
    }

  private:
    static bool less(const char *lhs, const int64_t lhs_length,
                     const char *rhs, const int64_t rhs_length)
    {
      const common::ObCharsetType charset_type = common::ObCharset::get_default_charset();
      const common::ObCollationType collation_type =
          common::ObCharset::get_default_collation(charset_type);
      return common::ObCharset::strcmpsp(collation_type,
                                         lhs, lhs_length,
                                         rhs, rhs_length,
                                         false /* cmp_endspace */) < 0;
    }
  };

  struct LockInfo
  {
    LockInfo()
      : owner_id_(), ref_count_(0), lock_id_(0), create_timestamp_(0)
    {}
    LockInfo(const ObTableLockOwnerID &owner_id,
             const int64_t ref_count,
             const uint64_t lock_id,
             const int64_t create_timestamp)
      : owner_id_(owner_id), ref_count_(ref_count), lock_id_(lock_id),
        create_timestamp_(create_timestamp)
    {}

    ObTableLockOwnerID owner_id_;
    int64_t ref_count_;
    uint64_t lock_id_;
    int64_t create_timestamp_;
  };

  typedef StlAllocator<std::pair<const LockName, LockInfo> > LockMapAllocator;
  typedef StlAllocator<LockName> LockNameSetAllocator;
  typedef std::set<LockName, LockNameLess, LockNameSetAllocator> LockNameSet;
  typedef StlAllocator<std::pair<const ObTableLockOwnerID, LockNameSet> > OwnerLockMapAllocator;
  typedef std::map<LockName, LockInfo, LockNameLess, LockMapAllocator> LockMap;
  typedef std::map<ObTableLockOwnerID, LockNameSet,
                   std::less<ObTableLockOwnerID>, OwnerLockMapAllocator> OwnerLockMap;

  int create_lock_(const common::ObString &lock_name,
                   const ObTableLockOwnerID &owner_id);

private:
  static constexpr int64_t WAIT_SLICE_US = 100 * 1000L;

  common::ObThreadCond cond_;
  QuotaAllocator allocator_;
  LockMap lock_map_;
  OwnerLockMap owner_lock_map_;
  uint64_t next_lock_id_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(NamedLockManager);
};

} // namespace tablelock
} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TABLELOCK_OB_NAMED_LOCK_MANAGER_H_
