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

#include "lib/container/ob_array.h"
#include "lib/lock/ob_spin_lock.h"
#include "share/ob_internal_table_change_version.h"
#include "share/scn.h"

namespace oceanbase
{
namespace observer
{
class ObServer;
}
namespace share
{

class InternalTableChangeNotifier
{
public:
  using ChangeVersion = InternalTableChangeVersion;
  using GtsProvider = int (*)(SCN &gts);

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
  int get_change_status(const uint64_t *table_ids,
      const ChangeVersion *last_versions,
      int64_t table_count,
      ChangeVersion *target_versions,
      SCN &max_commit_version,
      bool &changed) const;
  int is_change_visible(const SCN &commit_version, bool &is_visible) const;
  static bool is_change_visible(const SCN &commit_version, const SCN &gts)
  {
    return commit_version.is_valid() && gts.is_valid() && gts >= commit_version;
  }
  void mark_all_tables_changed();

private:
  friend class ::oceanbase::observer::ObServer;
  InternalTableChangeNotifier();
  ~InternalTableChangeNotifier();
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
  int get_change_version_(uint64_t table_id, ChangeVersion &version) const;

  common::ObArray<TableEntry> table_entries_;
  common::ObSpinLock lock_;  // protects initialization-time registrations
  bool is_inited_;
  bool is_sealed_;
  GtsProvider gts_provider_;
};

} // namespace share
} // namespace oceanbase
