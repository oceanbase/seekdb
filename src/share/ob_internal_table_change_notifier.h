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

#ifndef OCEANBASE_SHARE_OB_INTERNAL_TABLE_CHANGE_NOTIFIER_H_
#define OCEANBASE_SHARE_OB_INTERNAL_TABLE_CHANGE_NOTIFIER_H_

#include "lib/container/ob_array.h"
#include "lib/function/ob_function.h"
#include "lib/lock/ob_spin_lock.h"
#include "logservice/ob_log_base_type.h"

namespace oceanbase
{
namespace share
{

class ObInternalTableChangeNotifier : public logservice::ObILocalLogHandler
{
public:
  enum class Module {
    TIMEZONE = 0,
    GIS,
    MAX
  };
  using ModuleCallback = common::ObFunction<int()>;

  static ObInternalTableChangeNotifier &get_instance();

  int init();
  void destroy();

  // Registrations happen during server initialization. seal() freezes and
  // sorts the registry so commit and timer threads can use lock-free binary
  // lookup afterwards.
  int register_table(uint64_t table_id);
  int seal();

  // change_seq is a process-local change hint, not a persistent version or
  // SCN. Consumers only compare it for equality and advance their own
  // last-seen value after a successful refresh.
  void notify_table_changed(uint64_t table_id);
  int get_change_seq(uint64_t table_id, uint64_t &change_seq) const;
  void mark_all_tables_changed();

  int register_module(Module module, ModuleCallback callback);

  // Notify one cache owner after its backing inner table changes.
  int notify(Module module);

  // ObILocalLogHandler — called by ObLocalLogHandlerSet when LS switches role.
  void deactivate() override;
  int activate() override;

private:
  ObInternalTableChangeNotifier();
  ~ObInternalTableChangeNotifier();
  DISALLOW_COPY_AND_ASSIGN(ObInternalTableChangeNotifier);

  static constexpr int MAX_MODULE = static_cast<int>(Module::MAX);
  struct ModuleEntry {
    ModuleCallback callback_;
    ModuleEntry() : callback_() {}
  };

  struct TableEntry {
    uint64_t table_id_;
    uint64_t change_seq_;

    TableEntry() : table_id_(common::OB_INVALID_ID), change_seq_(1) {}
    explicit TableEntry(const uint64_t table_id) : table_id_(table_id), change_seq_(1) {}
    TO_STRING_KV(K_(table_id), K_(change_seq));
  };

  struct TableEntryCompare {
    bool operator()(const TableEntry &left, const TableEntry &right) const
    {
      return left.table_id_ < right.table_id_;
    }
  };

  const TableEntry *find_table_entry_(uint64_t table_id) const;
  TableEntry *find_table_entry_(uint64_t table_id);

  ModuleEntry entries_[MAX_MODULE];
  common::ObArray<TableEntry> table_entries_;
  common::ObSpinLock lock_;  // protects initialization-time registrations
  bool is_inited_;
  bool is_sealed_;
};

} // namespace share
} // namespace oceanbase

#endif
