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

#include "share/storage/ob_zone_merge_info_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_zone_merge_info.h"
#include "lib/string/ob_sql_string.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObZoneMergeInfoTableStorage::ObZoneMergeInfoTableStorage()
  : pool_(nullptr)
{
}

ObZoneMergeInfoTableStorage::~ObZoneMergeInfoTableStorage()
{
}

int ObZoneMergeInfoTableStorage::init(ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_ = pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else if (OB_FAIL(create_table_if_not_exists())) {
    LOG_WARN("failed to create table", K(ret));
  }
  if (OB_FAIL(ret)) {
    pool_ = NULL;
  }
  return ret;
}

int ObZoneMergeInfoTableStorage::create_table_if_not_exists()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("pool not set", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_ZONE_MERGE_INFO, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObZoneMergeInfoTableStorage::insert_or_update(const ObZoneMergeInfo &zone_merge_info)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *upsert_sql =
      "INSERT INTO __all_zone_merge_info "
      "(id, all_merged_scn, broadcast_scn, frozen_scn, "
      " is_merging, last_merged_time, last_merged_scn, merge_start_time, merge_status) "
      "VALUES (0, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(id) DO UPDATE SET "
      "all_merged_scn = excluded.all_merged_scn, "
      "broadcast_scn = excluded.broadcast_scn, "
      "frozen_scn = excluded.frozen_scn, "
      "is_merging = excluded.is_merging, "
      "last_merged_time = excluded.last_merged_time, "
      "last_merged_scn = excluded.last_merged_scn, "
      "merge_start_time = excluded.merge_start_time, "
      "merge_status = excluded.merge_status;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(zone_merge_info.all_merged_scn().get_val_for_inner_table_field());
      b.bind_int64(zone_merge_info.broadcast_scn().get_val_for_inner_table_field());
      b.bind_int64(zone_merge_info.frozen_scn().get_val_for_inner_table_field());
      b.bind_int64(zone_merge_info.is_merging_.get_value());
      b.bind_int64(zone_merge_info.last_merged_time_.get_value());
      b.bind_int64(zone_merge_info.last_merged_scn().get_val_for_inner_table_field());
      b.bind_int64(zone_merge_info.merge_start_time_.get_value());
      b.bind_int64(zone_merge_info.merge_status_.get_value());
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(upsert_sql, binder))) {
      LOG_WARN("failed to execute upsert", K(ret));
    }
  }
  return ret;
}

int ObZoneMergeInfoTableStorage::get(const uint64_t tenant_id, ObZoneMergeInfo &zone_merge_info)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // Reuse get_all since table is keyed only by tenant_id
    ObSEArray<ObZoneMergeInfo, 1> infos;
    if (OB_FAIL(get_all(tenant_id, infos))) {
      LOG_WARN("failed to get all zone merge infos", K(ret), K(tenant_id));
    } else if (infos.empty()) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (OB_FAIL(zone_merge_info.assign_value(infos.at(0)))) {
      LOG_WARN("fail to assign zone merge info value", KR(ret));
    }
  }
  return ret;
}

int ObZoneMergeInfoTableStorage::get_all(const uint64_t tenant_id, ObIArray<ObZoneMergeInfo> &zone_merge_infos)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  zone_merge_infos.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *sql =
        "SELECT all_merged_scn, broadcast_scn, frozen_scn, "
        "       is_merging, last_merged_time, last_merged_scn, merge_start_time, merge_status "
        "FROM __all_zone_merge_info";

    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      ObZoneMergeInfo zone_merge_info;
      uint64_t all_merged_scn_val = reader.get_int64();
      uint64_t broadcast_scn_val = reader.get_int64();
      uint64_t frozen_scn_val = reader.get_int64();
      int64_t is_merging = reader.get_int64();
      int64_t last_merged_time = reader.get_int64();
      uint64_t last_merged_scn_val = reader.get_int64();
      int64_t merge_start_time = reader.get_int64();
      int64_t merge_status = reader.get_int64();

      zone_merge_info.all_merged_scn_.set_scn(all_merged_scn_val);
      zone_merge_info.broadcast_scn_.set_scn(broadcast_scn_val);
      zone_merge_info.frozen_scn_.set_scn(frozen_scn_val);
      zone_merge_info.is_merging_.set_val(is_merging, false);
      zone_merge_info.last_merged_time_.set_val(last_merged_time, false);
      zone_merge_info.last_merged_scn_.set_scn(last_merged_scn_val);
      zone_merge_info.merge_start_time_.set_val(merge_start_time, false);
      zone_merge_info.merge_status_.set_val(merge_status, false);

      if (OB_FAIL(zone_merge_infos.push_back(zone_merge_info))) {
        LOG_WARN("failed to push back zone merge info", K(ret));
      }
      return ret;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(sql, nullptr, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret));
      } else {
        ret = OB_SUCCESS; // No rows is acceptable
      }
    }
  }
  return ret;
}

int ObZoneMergeInfoTableStorage::remove(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *delete_sql = "DELETE FROM __all_zone_merge_info;";

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(delete_sql, nullptr))) {
      LOG_WARN("failed to execute delete", K(ret));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
