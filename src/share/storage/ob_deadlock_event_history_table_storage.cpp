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

#include "share/storage/ob_deadlock_event_history_table_storage.h"
#include "share/storage/local_store.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace share
{

ObDeadlockEventHistoryTableStorage::ObDeadlockEventHistoryTableStorage()
  : store_(nullptr)
{
}

ObDeadlockEventHistoryTableStorage::~ObDeadlockEventHistoryTableStorage()
{
}

int ObDeadlockEventHistoryTableStorage::init(LocalStore *store)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(store_ = store)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!store_->is_inited()) {
    ret = OB_NOT_INIT;
  }
  if (OB_FAIL(ret)) {
    store_ = nullptr;
  }
  return ret;
}

int ObDeadlockEventHistoryTableStorage::insert(const ObDeadlockEventHistoryEntry &entry)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto insert_request = LocalRequest::insert(deadlock_history::schema())
        .set(deadlock_history::EventId, entry.event_id_)
        .set(deadlock_history::DetectorId, entry.detector_id_)
        .set(deadlock_history::ReportTime, entry.report_time_)
        .set(deadlock_history::CycleIdx, entry.cycle_idx_)
        .set(deadlock_history::CycleSize, entry.cycle_size_)
        .set(deadlock_history::Role, LocalValue::text(entry.role_.empty() ? "" : entry.role_.ptr(), entry.role_.length()))
        .set(deadlock_history::PriorityLevel, LocalValue::text(entry.priority_level_.empty() ? "" : entry.priority_level_.ptr(), entry.priority_level_.length()))
        .set(deadlock_history::Priority, entry.priority_)
        .set(deadlock_history::CreateTime, entry.create_time_)
        .set(deadlock_history::StartDelay, entry.start_delay_)
        .set(deadlock_history::Module, LocalValue::text(entry.module_.empty() ? "" : entry.module_.ptr(), entry.module_.length()))
        .set(deadlock_history::Visitor, LocalValue::text(entry.visitor_.empty() ? "" : entry.visitor_.ptr(), entry.visitor_.length()))
        .set(deadlock_history::Object, LocalValue::text(entry.object_.empty() ? "" : entry.object_.ptr(), entry.object_.length()))
        .set(deadlock_history::ExtraName1, LocalValue::text(entry.extra_name1_.empty() ? "" : entry.extra_name1_.ptr(), entry.extra_name1_.length()))
        .set(deadlock_history::ExtraValue1, LocalValue::text(entry.extra_value1_.empty() ? "" : entry.extra_value1_.ptr(), entry.extra_value1_.length()))
        .set(deadlock_history::ExtraName2, LocalValue::text(entry.extra_name2_.empty() ? "" : entry.extra_name2_.ptr(), entry.extra_name2_.length()))
        .set(deadlock_history::ExtraValue2, LocalValue::text(entry.extra_value2_.empty() ? "" : entry.extra_value2_.ptr(), entry.extra_value2_.length()))
        .set(deadlock_history::ExtraName3, LocalValue::text(entry.extra_name3_.empty() ? "" : entry.extra_name3_.ptr(), entry.extra_name3_.length()))
        .set(deadlock_history::ExtraValue3, LocalValue::text(entry.extra_value3_.empty() ? "" : entry.extra_value3_.ptr(), entry.extra_value3_.length()));
    ret = store_->execute(insert_request);
  }
  return ret;
}

int ObDeadlockEventHistoryTableStorage::insert_all(const ObIArray<ObDeadlockEventHistoryEntry> &entries)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (entries.empty()) {
    // do nothing
  } else {
    ret = store_->write([&](LocalTransaction &tx) -> int {
      for (int64_t i = 0; OB_SUCC(ret) && i < entries.count(); ++i) {
        const ObDeadlockEventHistoryEntry &entry = entries.at(i);
        const auto insert_request = LocalRequest::insert(deadlock_history::schema())
            .set(deadlock_history::EventId, entry.event_id_)
            .set(deadlock_history::DetectorId, entry.detector_id_)
            .set(deadlock_history::ReportTime, entry.report_time_)
            .set(deadlock_history::CycleIdx, entry.cycle_idx_)
            .set(deadlock_history::CycleSize, entry.cycle_size_)
            .set(deadlock_history::Role, LocalValue::text(entry.role_.ptr(), entry.role_.length()))
            .set(deadlock_history::PriorityLevel, LocalValue::text(entry.priority_level_.ptr(), entry.priority_level_.length()))
            .set(deadlock_history::Priority, entry.priority_)
            .set(deadlock_history::CreateTime, entry.create_time_)
            .set(deadlock_history::StartDelay, entry.start_delay_)
            .set(deadlock_history::Module, LocalValue::text(entry.module_.ptr(), entry.module_.length()))
            .set(deadlock_history::Visitor, LocalValue::text(entry.visitor_.ptr(), entry.visitor_.length()))
            .set(deadlock_history::Object, LocalValue::text(entry.object_.ptr(), entry.object_.length()))
            .set(deadlock_history::ExtraName1, LocalValue::text(entry.extra_name1_.ptr(), entry.extra_name1_.length()))
            .set(deadlock_history::ExtraValue1, LocalValue::text(entry.extra_value1_.ptr(), entry.extra_value1_.length()))
            .set(deadlock_history::ExtraName2, LocalValue::text(entry.extra_name2_.ptr(), entry.extra_name2_.length()))
            .set(deadlock_history::ExtraValue2, LocalValue::text(entry.extra_value2_.ptr(), entry.extra_value2_.length()))
            .set(deadlock_history::ExtraName3, LocalValue::text(entry.extra_name3_.ptr(), entry.extra_name3_.length()))
            .set(deadlock_history::ExtraValue3, LocalValue::text(entry.extra_value3_.ptr(), entry.extra_value3_.length()));
        ret = tx.execute(insert_request);
      }

      return ret;
    });
  }
  return ret;
}

int ObDeadlockEventHistoryTableStorage::delete_expired(int64_t report_time_before, int64_t limit)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto request = LocalRequest::erase(deadlock_history::schema())
        .lt(deadlock_history::ReportTime, static_cast<int64_t>(report_time_before)).order(deadlock_history::ReportTime).take(limit);
    ret = store_->execute(request);
  }
  return ret;
}

} // namespace share
} // namespace oceanbase

