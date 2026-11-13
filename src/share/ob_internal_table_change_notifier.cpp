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
#include "lib/atomic/ob_atomic.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_sort.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
namespace share
{

InternalTableChangeNotifier::InternalTableChangeNotifier()
  : table_entries_(),
    lock_(common::ObLatchIds::DEFAULT_SPIN_LOCK),
    is_inited_(false),
    is_sealed_(false),
    gts_provider_(nullptr)
{
}

InternalTableChangeNotifier::~InternalTableChangeNotifier()
{
  destroy();
}

int InternalTableChangeNotifier::init()
{
  int ret = OB_SUCCESS;
  if (ATOMIC_LOAD(&is_inited_)) {
  } else {
    ATOMIC_STORE(&is_sealed_, false);
    ATOMIC_STORE(&is_inited_, true);
    LOG_INFO("InternalTableChangeNotifier inited");
  }
  return ret;
}

void InternalTableChangeNotifier::destroy()
{
  common::ObSpinLockGuard guard(lock_);
  ATOMIC_STORE(&is_inited_, false);
  ATOMIC_STORE(&is_sealed_, false);
  gts_provider_ = nullptr;
  table_entries_.destroy();
  LOG_INFO("InternalTableChangeNotifier destroyed");
}

int InternalTableChangeNotifier::register_table(const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  bool found = false;
  if (OB_UNLIKELY(!common::is_inner_table(table_id))) {
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
    } else if (ATOMIC_LOAD(&is_sealed_)) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("cannot add table after notifier is sealed", K(ret), K(table_id));
    } else if (OB_FAIL(table_entries_.push_back(TableEntry(table_id)))) {
      LOG_WARN("failed to register internal table", K(ret), K(table_id));
    }
  }
  return ret;
}

int InternalTableChangeNotifier::seal()
{
  int ret = OB_SUCCESS;
  if (!ATOMIC_LOAD(&is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table change notifier is not initialized", K(ret));
  } else {
    common::ObSpinLockGuard guard(lock_);
    if (!ATOMIC_LOAD(&is_sealed_)) {
      if (table_entries_.count() > 1) {
        lib::ob_sort(&table_entries_.at(0),
                     &table_entries_.at(0) + table_entries_.count(),
                     TableEntryCompare());
      }
      ATOMIC_STORE(&is_sealed_, true);
      LOG_INFO("internal table change notifier sealed",
               "table_count", table_entries_.count());
    }
  }
  return ret;
}

int InternalTableChangeNotifier::set_gts_provider(const GtsProvider provider)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(provider)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("GTS provider must not be null", K(ret));
  } else if (!ATOMIC_LOAD(&is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table change notifier is not initialized", K(ret));
  } else {
    common::ObSpinLockGuard guard(lock_);
    if (ATOMIC_LOAD(&is_sealed_)) {
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("cannot set GTS provider after notifier is sealed", K(ret));
    } else {
      gts_provider_ = provider;
    }
  }
  return ret;
}

const InternalTableChangeNotifier::TableEntry *
InternalTableChangeNotifier::find_table_entry_(const uint64_t table_id) const
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

InternalTableChangeNotifier::TableEntry *
InternalTableChangeNotifier::find_table_entry_(const uint64_t table_id)
{
  return const_cast<TableEntry *>(
      static_cast<const InternalTableChangeNotifier *>(this)->find_table_entry_(table_id));
}

void InternalTableChangeNotifier::notify_table_changed(
    const uint64_t table_id,
    const SCN &commit_version)
{
  if (ATOMIC_LOAD(&is_inited_) && ATOMIC_LOAD(&is_sealed_)) {
    TableEntry *entry = find_table_entry_(table_id);
    if (nullptr != entry) {
      if (OB_UNLIKELY(!commit_version.is_valid())) {
        LOG_WARN_RET(OB_INVALID_ARGUMENT,
            "invalid commit version for internal table change", K(table_id), K(commit_version));
      } else {
        // Notifications may race and arrive out of commit order. Keep the
        // largest version so an older notification cannot lower the watermark.
        (void)entry->commit_version_.inc_update(commit_version);
      }
      (void)ATOMIC_AAF(&entry->change_seq_, 1);
    }
  }
}

int InternalTableChangeNotifier::get_change_version(
    const uint64_t table_id,
    ChangeVersion &version) const
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
    // The writer stores the version before incrementing the sequence. Reading
    // the sequence first makes its watermark visible; a racing later write is
    // conservative and will be checked again on the next timer tick.
    version.change_seq_ = ATOMIC_LOAD(&entry->change_seq_);
    version.commit_version_ = entry->commit_version_.atomic_load();
  }
  return ret;
}

int InternalTableChangeNotifier::is_change_visible(
    const SCN &commit_version,
    bool &is_visible) const
{
  int ret = OB_SUCCESS;
  is_visible = false;
  SCN gts;
  if (OB_UNLIKELY(!commit_version.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid commit version for visibility check", K(ret), K(commit_version));
  } else if (OB_ISNULL(gts_provider_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("GTS provider is not initialized for internal table refresh", K(ret));
  } else if (OB_FAIL(gts_provider_(gts))) {
    LOG_WARN("failed to get GTS for internal table refresh", K(ret));
  } else {
    is_visible = is_commit_version_visible(commit_version, gts);
  }
  return ret;
}

void InternalTableChangeNotifier::mark_all_tables_changed()
{
  if (ATOMIC_LOAD(&is_inited_) && ATOMIC_LOAD(&is_sealed_)) {
    for (int64_t i = 0; i < table_entries_.count(); ++i) {
      (void)ATOMIC_AAF(&table_entries_.at(i).change_seq_, 1);
    }
  }
}

int InternalTableChangeWatcher::init_watched_tables(
    const std::initializer_list<uint64_t> table_ids)
{
  int ret = OB_SUCCESS;
  OB_ASSERT(watched_tables_.count() == 0);
  InternalTableChangeNotifier *notifier = GCTX.internal_table_change_notifier_;
  if (OB_ISNULL(notifier)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table change notifier is not initialized", K(ret));
  } else {
    for (const uint64_t table_id : table_ids) {
      if (OB_FAIL(notifier->register_table(table_id))) {
        LOG_WARN("failed to register table with internal table change notifier",
                 K(ret), K(table_id));
      } else {
        WatchedTable watched_table;
        watched_table.table_id_ = table_id;
        if (OB_FAIL(watched_tables_.push_back(watched_table))) {
          LOG_WARN("failed to save watched table", K(ret), K(table_id));
        }
      }
      if (OB_FAIL(ret)) {
        break;
      }
    }
  }
  if (OB_FAIL(ret)) {
    watched_tables_.reset();
    target_captured_ = false;
  }
  return ret;
}

int InternalTableChangeWatcher::check_changed(CheckResult &result)
{
  int ret = OB_SUCCESS;
  bool changed = false;
  bool is_visible = false;
  result = NO_CHANGE;
  InternalTableChangeNotifier *notifier = GCTX.internal_table_change_notifier_;
  OB_ASSERT(nullptr != notifier);
  OB_ASSERT(watched_tables_.count() > 0);
  target_captured_ = false;
  target_commit_version_ = SCN::min_scn();
  for (int64_t i = 0; OB_SUCC(ret) && i < watched_tables_.count(); ++i) {
    WatchedTable &watched_table = watched_tables_.at(i);
    if (OB_FAIL(notifier->get_change_version(
            watched_table.table_id_, watched_table.target_version_))) {
      LOG_WARN("failed to get watched internal table version", K(ret),
               "table_id", watched_table.table_id_);
    } else {
      changed = changed || watched_table.target_version_ !=
                               watched_table.processed_version_;
      target_commit_version_ =
          SCN::max(target_commit_version_,
                   watched_table.target_version_.commit_version_);
    }
  }
  if (OB_SUCC(ret) && !changed) {
    result = NO_CHANGE;
  } else if (OB_SUCC(ret) && OB_FAIL(notifier->is_change_visible(
                                 target_commit_version_, is_visible))) {
    LOG_WARN("failed to check watched internal table visibility", K(ret),
             K(target_commit_version_));
  } else if (OB_SUCC(ret) && !is_visible) {
    result = CHANGE_NOT_VISIBLE;
  } else if (OB_SUCC(ret)) {
    result = REFRESH_ALLOWED;
    target_captured_ = true;
  }
  return ret;
}

int InternalTableChangeWatcher::mark_refreshed()
{
  int ret = OB_SUCCESS;
  if (watched_tables_.count() <= 0) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table watcher is not registered", K(ret));
  } else if (!target_captured_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("no refreshable target captured by internal table watcher",
             K(ret));
  } else {
    for (int64_t i = 0; i < watched_tables_.count(); ++i) {
      WatchedTable &watched_table = watched_tables_.at(i);
      watched_table.processed_version_ = watched_table.target_version_;
    }
    target_captured_ = false;
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
