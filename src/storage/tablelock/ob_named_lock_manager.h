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
#include <set>
#include <string>

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
// lock state is logged, replayed, or recovered after a restart.
class NamedLockManager final
{
public:
  // Keep the boundary of the former __all_dbms_lock_allocated.name
  // VARCHAR(128) column. The legacy implementation had no lock-count limit.
  static constexpr int64_t MAX_LOCK_NAME_LENGTH = 128;
  static constexpr int64_t LOCK_NOT_EXIST_RELEASE_RESULT = -1;
  static constexpr int64_t LOCK_NOT_OWN_RELEASE_RESULT = 0;
  static constexpr int64_t LOCK_RELEASED_RESULT = 1;

  NamedLockManager();
  ~NamedLockManager();

  int init();
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
  int get_counts(int64_t &lock_count, int64_t &waiter_count);

private:
  struct LockNameLess
  {
    bool operator()(const std::string &lhs, const std::string &rhs) const
    {
      const common::ObCharsetType charset_type = common::ObCharset::get_default_charset();
      const common::ObCollationType collation_type =
          common::ObCharset::get_default_collation(charset_type);
      return common::ObCharset::strcmpsp(collation_type,
                                         lhs.data(), lhs.length(),
                                         rhs.data(), rhs.length(),
                                         false /* cmp_endspace */) < 0;
    }
  };

  struct LockInfo
  {
    LockInfo() : owner_id_(), ref_count_(0) {}
    LockInfo(const ObTableLockOwnerID &owner_id, const int64_t ref_count)
      : owner_id_(owner_id), ref_count_(ref_count) {}

    ObTableLockOwnerID owner_id_;
    int64_t ref_count_;
  };

  typedef std::map<std::string, LockInfo, LockNameLess> LockMap;
  typedef std::set<std::string, LockNameLess> LockNameSet;
  typedef std::map<ObTableLockOwnerID, LockNameSet> OwnerLockMap;
  typedef std::map<ObTableLockOwnerID, ObTableLockOwnerID> WaitForMap;

  bool would_deadlock_(const ObTableLockOwnerID &waiter,
                       const ObTableLockOwnerID &blocker) const;
  void remove_waiter_(const ObTableLockOwnerID &owner_id);

private:
  static constexpr int64_t WAIT_SLICE_US = 100 * 1000L;

  common::ObThreadCond cond_;
  LockMap lock_map_;
  OwnerLockMap owner_lock_map_;
  WaitForMap wait_for_map_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(NamedLockManager);
};

} // namespace tablelock
} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_TABLELOCK_OB_NAMED_LOCK_MANAGER_H_
