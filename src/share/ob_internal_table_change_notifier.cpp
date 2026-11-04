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
        LOG_WARN("invalid commit version for internal table change", K(table_id), K(commit_version));
      } else {
        // Notifications may race and arrive out of commit order. Keep the
        // largest version so an older notification cannot lower the watermark.
        (void)entry->commit_version_.inc_update(commit_version);
      }
      (void)ATOMIC_AAF(&entry->change_seq_, 1);
    }
  }
}

int InternalTableChangeNotifier::get_change_version_(
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

int InternalTableChangeNotifier::get_change_status(
    const uint64_t *table_ids,
    const ChangeVersion *last_versions,
    const int64_t table_count,
    ChangeVersion *target_versions,
    SCN &max_commit_version,
    bool &changed) const
{
  int ret = OB_SUCCESS;
  max_commit_version = SCN::min_scn();
  changed = false;
  if (OB_UNLIKELY(table_count < 0)
      || OB_UNLIKELY(table_count > 0
          && (OB_ISNULL(table_ids) || OB_ISNULL(last_versions) || OB_ISNULL(target_versions)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments to check internal table change status", K(ret), K(table_count));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < table_count; ++i) {
      if (OB_FAIL(get_change_version_(table_ids[i], target_versions[i]))) {
        LOG_WARN("failed to get internal table change version", K(ret), "table_id", table_ids[i]);
      } else {
        changed = changed || target_versions[i] != last_versions[i];
        max_commit_version = SCN::max(max_commit_version, target_versions[i].commit_version_);
      }
    }
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
    is_visible = is_change_visible(commit_version, gts);
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

} // namespace share
} // namespace oceanbase
