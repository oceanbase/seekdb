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

#include "share/tablet/ob_tablet_meta_table_storage.h"
#include "lib/oblog/ob_log.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_print_utils.h"
#include "share/storage/local_store.h"

namespace oceanbase
{
using namespace common;

namespace share
{

int ObTabletRuntimeInfoConstructor::operator()(
    share::LocalRowReader &reader,
    ObTabletRuntimeInfo &info)
{
  int ret = OB_SUCCESS;
  info.reset();

  int64_t tablet_id = reader.get_int64();
  int64_t compaction_scn = reader.get_int64();
  int64_t data_size = reader.get_int64();
  int64_t required_size = reader.get_int64();
  int64_t report_scn = reader.get_int64();
  int status = reader.get_int();

  if (OB_UNLIKELY(!ObTabletRuntimeInfo::is_status_valid((ObTabletRuntimeInfo::ScnStatus)status))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(info.init(
      ObTabletID(tablet_id),
      compaction_scn,
      data_size,
      required_size,
      report_scn,
      (ObTabletRuntimeInfo::ScnStatus)status))) {
  }

  return ret;
}

ObTabletMetaTableStorage::ObTabletMetaTableStorage()
  : store_(nullptr)
{
}

ObTabletMetaTableStorage::~ObTabletMetaTableStorage()
{
}

int ObTabletMetaTableStorage::init(LocalStore *store)
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

int ObTabletMetaTableStorage::batch_get(
    const ObIArray<common::ObTabletID> &tablet_ids,
    ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (tablet_ids.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    auto request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::TabletId, tablet_meta::CompactionScn, tablet_meta::DataSize, tablet_meta::RequiredSize, tablet_meta::ReportScn, tablet_meta::Status}).in(tablet_meta::TabletId, tablet_ids).order(tablet_meta::TabletId);
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
      if (!tablet_ids.at(i).is_valid()) { ret = OB_INVALID_ARGUMENT; }
    }

    if (OB_SUCC(ret)) {
      ObTabletRuntimeInfoConstructor constructor;
      auto row_processor = [&](share::LocalRowReader &reader) -> int {
        ObTabletRuntimeInfo tablet_info;
        if (OB_FAIL(constructor(reader, tablet_info))) {
        }
        if (OB_SUCC(ret) && OB_FAIL(tablet_infos.push_back(tablet_info))) {
        }
        return ret;
      };

      if (OB_FAIL(store_->query(request, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
        } else {
          ret = OB_SUCCESS; // No rows is acceptable
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update(
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = store_->write([&](LocalTransaction &tx) { return batch_update(tx, tablet_infos); });
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update(
    LocalTransaction &tx,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t current_time = ObTimeUtility::current_time();

    int64_t current_index = 0;

    while (OB_SUCC(ret) && current_index < tablet_infos.count()) {
      const ObTabletRuntimeInfo &tablet_info = tablet_infos.at(current_index++);
      if (OB_UNLIKELY(!tablet_info.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const auto upsert_request = LocalRequest::upsert(tablet_meta::schema())
            .set(tablet_meta::GmtCreate, current_time)
            .set(tablet_meta::GmtModified, current_time)
            .set(tablet_meta::TabletId, tablet_info.get_tablet_id().id())
            .set(tablet_meta::CompactionScn, tablet_info.get_snapshot_version())
            .set(tablet_meta::DataSize, tablet_info.get_data_size())
            .set(tablet_meta::RequiredSize, tablet_info.get_required_size())
            .set(tablet_meta::ReportScn, tablet_info.get_report_scn())
            .set(tablet_meta::Status, tablet_info.get_status());
        ret = tx.execute(upsert_request);
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_remove(
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = store_->write([&](LocalTransaction &tx) { return batch_remove(tx, tablet_infos); });
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_remove(
    LocalTransaction &tx,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    int64_t current_index = 0;

    while (OB_SUCC(ret) && current_index < tablet_infos.count()) {
      const ObTabletRuntimeInfo &tablet_info = tablet_infos.at(current_index++);
      if (OB_UNLIKELY(!tablet_info.primary_keys_are_valid())) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        const auto delete_request = LocalRequest::erase(tablet_meta::schema())
            .eq(tablet_meta::TabletId, tablet_info.get_tablet_id().id());
        ret = tx.execute(delete_request);
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::get_data_size(const common::ObTabletID &tablet_id,
    int64_t &data_size)
{
  int ret = OB_SUCCESS;
  data_size = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::DataSize})
        .eq(tablet_meta::TabletId, tablet_id.id());

    auto row_processor = [&](share::LocalRowReader &reader) -> int {
      int64_t data_size_val = reader.get_int64();
      if (data_size_val > 0) {
        data_size = data_size_val;
      }
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
      } else {
        ret = OB_SUCCESS; // No rows is acceptable, data_size remains 0
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::get_report_scn_and_status(const common::ObTabletID &tablet_id,
    int64_t &report_scn,
    int64_t &status)
{
  int ret = OB_SUCCESS;
  report_scn = 0;
  status = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::ReportScn, tablet_meta::Status})
        .eq(tablet_meta::TabletId, tablet_id.id());

    auto row_processor = [&](share::LocalRowReader &reader) -> int {
      report_scn = reader.get_int64();
      status = reader.get_int64();
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
      } else {
        ret = OB_ENTRY_NOT_EXIST; // No rows found
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::get_min_compaction_scn(uint64_t &min_compaction_scn)
{
  int ret = OB_SUCCESS;
  min_compaction_scn = UINT64_MAX;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request =
      LocalRequest::select(tablet_meta::schema(), {tablet_meta::CompactionScn}).order(tablet_meta::CompactionScn).take(int64_t(1));

    auto row_processor = [&](share::LocalRowReader &reader) -> int {
      min_compaction_scn = reader.get_int64();
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
      } else {
        ret = OB_SUCCESS; // No rows is acceptable, min_compaction_scn remains UINT64_MAX
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::get_tablet_count(int64_t &tablet_count)
{
  int ret = OB_SUCCESS;
  tablet_count = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request =
      LocalRequest::select(tablet_meta::schema(), {}).aggregate(local::Aggregate::Count);

    auto row_processor = [&](share::LocalRowReader &reader) -> int {
      tablet_count = reader.get_int64();
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update_report_scn(
    const ObIArray<ObTabletID> &tablet_ids,
    const uint64_t report_scn,
    const uint64_t compaction_scn_min,
    const int64_t except_status,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (!is_inited()) { ret = OB_NOT_INIT; }
  else if (tablet_ids.count() <= 0) { ret = OB_INVALID_ARGUMENT; }
  else {
    const auto request = LocalRequest::update(tablet_meta::schema()).set(tablet_meta::ReportScn, static_cast<int64_t>(report_scn)).in(tablet_meta::TabletId, tablet_ids).ge(tablet_meta::CompactionScn, static_cast<int64_t>(compaction_scn_min)).lt(tablet_meta::ReportScn, static_cast<int64_t>(report_scn)).ne(tablet_meta::Status, except_status);
    ret = store_->execute(request, &affected_rows);
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update_report_scn_unequal(
    const ObIArray<common::ObTabletID> &tablet_ids,
    const uint64_t major_frozen_scn,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (!is_inited()) { ret = OB_NOT_INIT; }
  else if (tablet_ids.count() <= 0) { ret = OB_INVALID_ARGUMENT; }
  else {
    const auto request = LocalRequest::update(tablet_meta::schema()).set(tablet_meta::ReportScn, static_cast<int64_t>(major_frozen_scn), tablet_meta::CompactionScn).in(tablet_meta::TabletId, tablet_ids).lt(tablet_meta::ReportScn, static_cast<int64_t>(major_frozen_scn));
    ret = store_->execute(request, &affected_rows);
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update_report_scn_range(const common::ObTabletID &start_tablet_id,
    const common::ObTabletID &end_tablet_id,
    const uint64_t report_scn,
    const uint64_t compaction_scn_min,
    const int64_t except_status,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto update_request = LocalRequest::update(tablet_meta::schema())
        .set(tablet_meta::ReportScn, report_scn)
        .ge(tablet_meta::TabletId, start_tablet_id.id())
        .le(tablet_meta::TabletId, end_tablet_id.id())
        .ge(tablet_meta::CompactionScn, compaction_scn_min)
        .lt(tablet_meta::ReportScn, report_scn)
        .ne(tablet_meta::Status, except_status);

    ret = store_->execute(update_request, &affected_rows);
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update_status_range(const common::ObTabletID &start_tablet_id,
    const common::ObTabletID &end_tablet_id,
    const int64_t from_status,
    const int64_t to_status,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto update_request = LocalRequest::update(tablet_meta::schema())
        .set(tablet_meta::Status, to_status)
        .ge(tablet_meta::TabletId, start_tablet_id.id())
        .le(tablet_meta::TabletId, end_tablet_id.id())
        .eq(tablet_meta::Status, from_status);

    ret = store_->execute(update_request, &affected_rows);
  }
  return ret;
}

int ObTabletMetaTableStorage::get_tablet_ids(const common::ObTabletID &start_tablet_id,
    const int64_t limit,
    ObIArray<common::ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  tablet_ids.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    const auto select_request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::TabletId})
        .gt(tablet_meta::TabletId, start_tablet_id.id())
        .order(tablet_meta::TabletId)
        .take(limit);

    auto row_processor = [&](share::LocalRowReader &reader) -> int {
      int64_t tablet_id_val = reader.get_int64();
      if (OB_FAIL(tablet_ids.push_back(common::ObTabletID(tablet_id_val)))) {
      }
      return ret;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
      } else {
        ret = OB_SUCCESS; // No rows is acceptable
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::get_tablet_ids_with_report_scn_before(
    const ObIArray<common::ObTabletID> &tablet_ids,
    const uint64_t report_scn_max,
    ObIArray<common::ObTabletID> &result_tablet_ids)
{
  int ret = OB_SUCCESS;
  result_tablet_ids.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (tablet_ids.count() <= 0) {
    // Empty input, return empty result
  } else {
    auto request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::TabletId}).in(tablet_meta::TabletId, tablet_ids).lt(tablet_meta::ReportScn, static_cast<int64_t>(report_scn_max));

    if (OB_SUCC(ret)) {
      auto row_processor = [&](share::LocalRowReader &reader) -> int {
        int64_t tablet_id_val = reader.get_int64();
        if (OB_FAIL(result_tablet_ids.push_back(common::ObTabletID(tablet_id_val)))) {
        }
        return ret;
      };

      if (OB_FAIL(store_->query(request, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
        } else {
          ret = OB_SUCCESS; // No rows is acceptable
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::get_max_tablet_id_in_range(const common::ObTabletID &start_tablet_id,
    const int64_t batch_size,
    common::ObTabletID &max_tablet_id)
{
  int ret = OB_SUCCESS;
  max_tablet_id.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const auto select_request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::TabletId})
        .gt(tablet_meta::TabletId, start_tablet_id.id())
        .order(tablet_meta::TabletId)
        .take(int64_t(1))
        .skip(batch_size - 1);

    auto row_processor = [&](share::LocalRowReader &reader) -> int {
      int64_t tablet_id_val = reader.get_int64();
      max_tablet_id = common::ObTabletID(tablet_id_val);
      return OB_SUCCESS;
    };

    if (OB_FAIL(store_->query(select_request, row_processor))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_ITER_END; // No more tablets
      } else {
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::range_scan_for_compaction(const common::ObTabletID &start_tablet_id,
    const common::ObTabletID &end_tablet_id,
    const int64_t compaction_scn,
    const bool only_unreported,
    ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
  } else {
    auto request = LocalRequest::select(tablet_meta::schema(), {tablet_meta::TabletId, tablet_meta::CompactionScn, tablet_meta::DataSize, tablet_meta::RequiredSize, tablet_meta::ReportScn, tablet_meta::Status}).gt(tablet_meta::TabletId, static_cast<int64_t>(start_tablet_id.id())).le(tablet_meta::TabletId, static_cast<int64_t>(end_tablet_id.id())).order(tablet_meta::TabletId);
    if (only_unreported) { request.lt(tablet_meta::ReportScn, compaction_scn); }

    if (OB_SUCC(ret)) {
      ObTabletRuntimeInfoConstructor constructor;
      auto row_processor = [&](share::LocalRowReader &reader) -> int {
        ObTabletRuntimeInfo tablet_info;
        if (OB_FAIL(constructor(reader, tablet_info))) {
        } else if (OB_FAIL(tablet_infos.push_back(tablet_info))) {
        }
        return ret;
      };

      if (OB_FAIL(store_->query(request, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
        } else {
          ret = OB_SUCCESS; // No rows is acceptable
        }
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
