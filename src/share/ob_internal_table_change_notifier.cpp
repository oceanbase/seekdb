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

#define USING_LOG_PREFIX SHARE
#include "share/ob_internal_table_change_notifier.h"
#include "lib/utility/ob_sort.h"
#include "lib/oblog/ob_log_module.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
namespace share
{

ObInternalTableChangeNotifier &ObInternalTableChangeNotifier::get_instance()
{
  static ObInternalTableChangeNotifier instance;
  return instance;
}

ObInternalTableChangeNotifier::ObInternalTableChangeNotifier()
  : table_entries_(),
    lock_(common::ObLatchIds::DEFAULT_SPIN_LOCK),
    is_inited_(false),
    is_sealed_(false)
{
}

ObInternalTableChangeNotifier::~ObInternalTableChangeNotifier()
{
  destroy();
}

int ObInternalTableChangeNotifier::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    // already inited, no-op
  } else {
    ATOMIC_STORE(&is_sealed_, false);
    ATOMIC_STORE(&is_inited_, true);
    LOG_INFO("ObInternalTableChangeNotifier inited");
  }
  return ret;
}

void ObInternalTableChangeNotifier::destroy()
{
  common::ObSpinLockGuard guard(lock_);
  for (int i = 0; i < MAX_MODULE; i++) {
    entries_[i].callback_.reset();
  }
  ATOMIC_STORE(&is_inited_, false);
  ATOMIC_STORE(&is_sealed_, false);
  table_entries_.destroy();
  LOG_INFO("ObInternalTableChangeNotifier destroyed");
}

int ObInternalTableChangeNotifier::register_table(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  bool found = false;
  if (OB_UNLIKELY(!is_inner_table(table_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("only inner tables can be registered", K(ret), K(table_id));
  } else if (!ATOMIC_LOAD(&is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table change notifier is not initialized", K(ret), K(table_id));
  } else {
    common::ObSpinLockGuard guard(lock_);
    for (int64_t i = 0; !found && i < table_entries_.count(); ++i) {
      found = table_id == table_entries_.at(i).table_id_;
    }
    if (found) {
      // Idempotent even after seal: the registration does not change the registry.
    } else if (ATOMIC_LOAD(&is_sealed_)) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("cannot add table after notifier is sealed", K(ret), K(table_id));
    } else if (OB_FAIL(table_entries_.push_back(TableEntry(table_id)))) {
      LOG_WARN("failed to register internal table", K(ret), K(table_id));
    }
  }
  return ret;
}

int ObInternalTableChangeNotifier::seal()
{
  int ret = OB_SUCCESS;
  if (!ATOMIC_LOAD(&is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table change notifier is not initialized", K(ret));
  } else {
    common::ObSpinLockGuard guard(lock_);
    if (!ATOMIC_LOAD(&is_sealed_)) {
      if (table_entries_.count() > 1) {
        lib::ob_sort(
            &table_entries_.at(0),
            &table_entries_.at(0) + table_entries_.count(),
            TableEntryCompare());
      }
      ATOMIC_STORE(&is_sealed_, true);
      LOG_INFO("internal table change notifier sealed", "table_count", table_entries_.count());
    }
  }
  return ret;
}

const ObInternalTableChangeNotifier::TableEntry *
ObInternalTableChangeNotifier::find_table_entry_(const uint64_t table_id) const
{
  const TableEntry *entry = nullptr;
  int64_t left = 0;
  int64_t right = table_entries_.count();
  while (nullptr == entry && left < right) {
    const int64_t mid = left + (right - left) / 2;
    const TableEntry &candidate = table_entries_.at(mid);
    if (table_id < candidate.table_id_) {
      right = mid;
    } else if (table_id > candidate.table_id_) {
      left = mid + 1;
    } else {
      entry = &candidate;
    }
  }
  return entry;
}

ObInternalTableChangeNotifier::TableEntry *
ObInternalTableChangeNotifier::find_table_entry_(const uint64_t table_id)
{
  return const_cast<TableEntry *>(
      static_cast<const ObInternalTableChangeNotifier *>(this)->find_table_entry_(table_id));
}

void ObInternalTableChangeNotifier::notify_table_changed(const uint64_t table_id)
{
  if (ATOMIC_LOAD(&is_inited_) && ATOMIC_LOAD(&is_sealed_)) {
    TableEntry *entry = find_table_entry_(table_id);
    if (nullptr != entry) {
      (void)ATOMIC_AAF(&entry->change_seq_, 1);
    }
  }
}

int ObInternalTableChangeNotifier::get_change_seq(
    const uint64_t table_id,
    uint64_t &change_seq) const
{
  int ret = OB_SUCCESS;
  const TableEntry *entry = nullptr;
  if (!ATOMIC_LOAD(&is_inited_)) {
    ret = OB_NOT_INIT;
  } else if (!ATOMIC_LOAD(&is_sealed_)) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_ISNULL(entry = find_table_entry_(table_id))) {
    ret = OB_ENTRY_NOT_EXIST;
  } else {
    change_seq = ATOMIC_LOAD(&entry->change_seq_);
  }
  return ret;
}

void ObInternalTableChangeNotifier::mark_all_tables_changed()
{
  if (ATOMIC_LOAD(&is_inited_) && ATOMIC_LOAD(&is_sealed_)) {
    for (int64_t i = 0; i < table_entries_.count(); ++i) {
      (void)ATOMIC_AAF(&table_entries_.at(i).change_seq_, 1);
    }
  }
}

int ObInternalTableChangeNotifier::register_module(
    table::ObModuleDataArg::ObExecModule module,
    ModuleCallback callback)
{
  int ret = OB_SUCCESS;
  int idx = static_cast<int>(module);
  if (idx < 0 || idx >= MAX_MODULE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid module type", K(ret), K(idx));
  } else if (!callback.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid callback", K(ret), K(idx));
  } else {
    common::ObSpinLockGuard guard(lock_);
    entries_[idx].callback_ = callback;
    LOG_INFO("registered module callback", K(idx));
  }
  return ret;
}

int ObInternalTableChangeNotifier::notify(
    table::ObModuleDataArg::ObExecModule module)
{
  int ret = OB_SUCCESS;
  int idx = static_cast<int>(module);
  if (idx < 0 || idx >= MAX_MODULE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid module type", K(ret), K(idx));
  } else {
    LOG_INFO("[NOTIFIER] notifying module", K(idx));
    int tmp_ret = entries_[idx].callback_();
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("module callback failed", K(tmp_ret), K(idx));
      ret = tmp_ret;
    }
  }
  return ret;
}

void ObInternalTableChangeNotifier::deactivate()
{
}

int ObInternalTableChangeNotifier::activate()
{
  int ret = OB_SUCCESS;

  mark_all_tables_changed();
  LOG_INFO("[NOTIFIER] LS promoted to leader, notifying all modules");
  for (int mod = 0; mod < MAX_MODULE; mod++) {
    if (entries_[mod].callback_.is_valid()) {
      int tmp_ret = notify(static_cast<table::ObModuleDataArg::ObExecModule>(mod));
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("module notify failed on leader promotion", K(tmp_ret), K(mod));
        if (OB_SUCCESS == ret) { ret = tmp_ret; }
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
