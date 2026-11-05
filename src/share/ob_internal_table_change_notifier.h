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

#pragma once

#include <initializer_list>

#include "lib/container/ob_array.h"
#include "lib/lock/ob_spin_lock.h"
#include "share/ob_internal_table_change_version.h"
#include "share/scn.h"

namespace oceanbase
{
namespace share
{

class InternalTableChangeNotifier
{
public:
  using ChangeVersion = InternalTableChangeVersion;
  using GtsProvider = int (*)(SCN &gts);

  InternalTableChangeNotifier();
  ~InternalTableChangeNotifier();

  int init();
  void destroy();

  // Registration happens during server initialization. seal() freezes and
  // sorts the registry so commit and timer threads can use lock-free lookup.
  int register_table(uint64_t table_id);
  int seal();
  int set_gts_provider(GtsProvider provider);

  // Publish the committed MVCC version after a table-lock transaction ends.
  // Consumers wait for GTS to reach it before starting an inner-SQL refresh.
  void notify_table_changed(uint64_t table_id, const SCN &commit_version);
  void mark_all_tables_changed();
  // Read a table's current change version and check commit visibility.
  int get_change_version(uint64_t table_id, ChangeVersion &version) const;
  int is_change_visible(const SCN &commit_version, bool &is_visible) const;

private:
  DISALLOW_COPY_AND_ASSIGN(InternalTableChangeNotifier);

  struct TableEntry {
    uint64_t table_id_;
    uint64_t change_seq_;
    SCN commit_version_;

    TableEntry()
        : table_id_(common::OB_INVALID_ID), change_seq_(1), commit_version_(SCN::min_scn()) {}
    explicit TableEntry(const uint64_t table_id)
        : table_id_(table_id), change_seq_(1), commit_version_(SCN::min_scn()) {}
    TO_STRING_KV(K_(table_id), K_(change_seq), K_(commit_version));
  };

  struct TableEntryCompare {
    bool operator()(const TableEntry &left, const TableEntry &right) const
    {
      return left.table_id_ < right.table_id_;
    }
  };

  const TableEntry *find_table_entry_(uint64_t table_id) const;
  TableEntry *find_table_entry_(uint64_t table_id);
  static bool is_commit_version_visible(const SCN &commit_version,
                                       const SCN &gts)
  {
    return commit_version.is_valid() && gts.is_valid() && gts >= commit_version;
  }

  common::ObArray<TableEntry> table_entries_;
  common::ObSpinLock lock_;  // protects initialization-time registrations
  bool is_inited_;
  bool is_sealed_;
  GtsProvider gts_provider_;
};

// A consumer-local cursor for the internal tables it refreshes. Each consumer
// owns its own watcher, even when multiple consumers watch the same table.
// Calls that capture and acknowledge a target must be serialized by the owner.
class InternalTableChangeWatcher
{
public:
  InternalTableChangeWatcher() = default;

  enum CheckResult {
    NO_CHANGE,
    CHANGE_NOT_VISIBLE,
    REFRESH_ALLOWED,
  };

  ~InternalTableChangeWatcher() = default;
  DISALLOW_COPY_AND_ASSIGN(InternalTableChangeWatcher);

  // Must be called during module initialization, before the notifier is sealed.
  // Calling it while watched_tables_ is non-empty triggers OB_ASSERT.
  int init_watched_tables(std::initializer_list<uint64_t> table_ids);

  // Captures the current target versions and checks GTS visibility. The caller
  // must call mark_refreshed() only after the associated refresh succeeds.
  int check_changed(CheckResult &result);

  // Advances only to the target captured by the most recent REFRESH_ALLOWED
  // result, leaving notifications received during the refresh for next time.
  int mark_refreshed();

private:
  struct WatchedTable {
    uint64_t table_id_;
    InternalTableChangeVersion processed_version_;
    InternalTableChangeVersion target_version_;

    WatchedTable()
        : table_id_(common::OB_INVALID_ID), processed_version_(),
          target_version_() {}
  };

  common::ObArray<WatchedTable> watched_tables_{};
  SCN target_commit_version_ = SCN::min_scn();
  bool target_captured_ = false;
};

} // namespace share
} // namespace oceanbase
