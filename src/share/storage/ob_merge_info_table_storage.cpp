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

#include "share/storage/ob_merge_info_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_zone_merge_info.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObMergeInfoTableStorage::ObMergeInfoTableStorage()
  : pool_(nullptr)
{
}

ObMergeInfoTableStorage::~ObMergeInfoTableStorage()
{
}

int ObMergeInfoTableStorage::init(ObSQLiteConnectionPool *pool)
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

int ObMergeInfoTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_MERGE_INFO, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObMergeInfoTableStorage::insert_or_update(const ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *upsert_sql =
      "INSERT INTO __all_merge_info "
      "(id, frozen_scn, global_broadcast_scn, is_merge_error, "
      " last_merged_scn, merge_status, error_type, suspend_merging, "
      " merge_start_time, last_merged_time) "
      "VALUES (0, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(id) DO UPDATE SET "
      "frozen_scn = excluded.frozen_scn, "
      "global_broadcast_scn = excluded.global_broadcast_scn, "
      "is_merge_error = excluded.is_merge_error, "
      "last_merged_scn = excluded.last_merged_scn, "
      "merge_status = excluded.merge_status, "
      "error_type = excluded.error_type, "
      "suspend_merging = excluded.suspend_merging, "
      "merge_start_time = excluded.merge_start_time, "
      "last_merged_time = excluded.last_merged_time;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(global_merge_info.frozen_scn().get_val_for_inner_table_field());
      b.bind_int64(global_merge_info.global_broadcast_scn().get_val_for_inner_table_field());
      b.bind_int64(global_merge_info.is_merge_error_.get_value());
      b.bind_int64(global_merge_info.last_merged_scn().get_val_for_inner_table_field());
      b.bind_int64(global_merge_info.merge_status_.get_value());
      b.bind_int64(global_merge_info.error_type_.get_value());
      b.bind_int64(global_merge_info.suspend_merging_.get_value());
      b.bind_int64(global_merge_info.merge_start_time_.get_value());
      b.bind_int64(global_merge_info.last_merged_time_.get_value());
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

int ObMergeInfoTableStorage::get(const uint64_t tenant_id, ObGlobalMergeInfo &global_merge_info)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  global_merge_info.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT frozen_scn, global_broadcast_scn, is_merge_error, "
      "       last_merged_scn, merge_status, error_type, suspend_merging, "
      "       merge_start_time, last_merged_time "
      "FROM __all_merge_info LIMIT 1;";

    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      uint64_t frozen_scn_val = reader.get_int64();
      uint64_t global_broadcast_scn_val = reader.get_int64();
      int64_t is_merge_error = reader.get_int64();
      uint64_t last_merged_scn_val = reader.get_int64();
      int64_t merge_status = reader.get_int64();
      int64_t error_type = reader.get_int64();
      int64_t suspend_merging = reader.get_int64();
      int64_t merge_start_time = reader.get_int64();
      int64_t last_merged_time = reader.get_int64();

      global_merge_info.frozen_scn_.set_scn(frozen_scn_val);
      global_merge_info.global_broadcast_scn_.set_scn(global_broadcast_scn_val);
      global_merge_info.is_merge_error_.set_val(is_merge_error, false);
      global_merge_info.last_merged_scn_.set_scn(last_merged_scn_val);
      global_merge_info.merge_status_.set_val(merge_status, false);
      global_merge_info.error_type_.set_val(error_type, false);
      global_merge_info.suspend_merging_.set_val(suspend_merging, false);
      global_merge_info.merge_start_time_.set_val(merge_start_time, false);
      global_merge_info.last_merged_time_.set_val(last_merged_time, false);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, nullptr, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret));
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
