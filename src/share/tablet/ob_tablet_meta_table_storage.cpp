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
#include "lib/string/ob_sql_string.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "share/storage/ob_sqlite_table_schema.h"
#include <string.h>

namespace oceanbase
{
using namespace common;

namespace share
{

int ObTabletRuntimeInfoConstructor::operator()(
    share::ObSQLiteRowReader &reader,
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
    LOG_WARN("invalid status", K(ret), K(status));
  } else if (OB_FAIL(info.init(
      ObTabletID(tablet_id),
      compaction_scn,
      data_size,
      required_size,
      report_scn,
      (ObTabletRuntimeInfo::ScnStatus)status))) {
    LOG_WARN("fail to init tablet runtime info", K(ret),
        K(tablet_id), K(data_size), K(required_size));
  }

  return ret;
}


ObTabletMetaTableStorage::ObTabletMetaTableStorage()
  : pool_(nullptr)
{
}

ObTabletMetaTableStorage::~ObTabletMetaTableStorage()
{
}

int ObTabletMetaTableStorage::init(ObSQLiteConnectionPool *pool)
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

int ObTabletMetaTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_TABLET_META_TABLE, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
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
    LOG_WARN("not init", K(ret));
  } else if (tablet_ids.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "count", tablet_ids.count());
  } else {
    // Build SQL with IN clause
    ObSqlString sql;
    if (OB_FAIL(sql.append_fmt(
        "SELECT tablet_id, "
        "       compaction_scn, data_size, required_size, report_scn, status "
        "FROM __all_tablet_meta_table "
        "WHERE tablet_id IN ("))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
        const ObTabletID &tablet_id = tablet_ids.at(i);
        if (OB_UNLIKELY(!tablet_id.is_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid tablet id", K(ret), K(tablet_id));
        } else if (OB_FAIL(sql.append_fmt(
            "%s %ld",
            i == 0 ? "" : ",",
            tablet_id.id()))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql.append(") ORDER BY tablet_id;"))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObTabletRuntimeInfoConstructor constructor;
      auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
        ObTabletRuntimeInfo tablet_info;
        if (OB_FAIL(constructor(reader, tablet_info))) {
          LOG_WARN("failed to construct tablet runtime info", K(ret));
        }
        if (OB_SUCC(ret) && OB_FAIL(tablet_infos.push_back(tablet_info))) {
          LOG_WARN("failed to append tablet runtime info", K(ret));
        }
        return ret;
      };

      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->query(sql.ptr(), nullptr, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "count", tablet_infos.count());
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->begin_transaction())) {
      LOG_WARN("failed to begin transaction", K(ret));
    } else {
      // Use the overload with connection parameter
      ret = batch_update(guard.get_connection(), tablet_infos);

      // Commit or rollback transaction
      if (OB_FAIL(ret)) {
        int rollback_ret = guard->rollback();
        if (OB_SUCCESS != rollback_ret) {
          LOG_WARN("failed to rollback", K(rollback_ret));
        }
      } else {
        if (OB_FAIL(guard->commit())) {
          LOG_WARN("failed to commit", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_update(
    ObSQLiteConnection *conn,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "count", tablet_infos.count());
  } else {
    const char *upsert_sql =
      "INSERT OR REPLACE INTO __all_tablet_meta_table "
      "(gmt_create, gmt_modified, tablet_id, "
      " compaction_scn, data_size, required_size, report_scn, status) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

    int64_t current_time = ObTimeUtility::current_time();

    share::ObSQLiteStmt *stmt = nullptr;
    if (OB_FAIL(conn->prepare_execute(upsert_sql, stmt))) {
      LOG_WARN("failed to prepare batch execute", K(ret));
    } else {
      int64_t current_index = 0;

      while (OB_SUCC(ret) && current_index < tablet_infos.count()) {
        const ObTabletRuntimeInfo &tablet_info = tablet_infos.at(current_index++);
        if (OB_UNLIKELY(!tablet_info.is_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid tablet runtime info", K(ret), K(tablet_info));
        } else {
          auto binder = [&](share::ObSQLiteBinder &b) -> int {
            b.bind_int64(current_time); // gmt_create
            b.bind_int64(current_time); // gmt_modified
            b.bind_int64(tablet_info.get_tablet_id().id());
            b.bind_int64(tablet_info.get_snapshot_version());
            b.bind_int64(tablet_info.get_data_size());
            b.bind_int64(tablet_info.get_required_size());
            b.bind_int64(tablet_info.get_report_scn());
            b.bind_int(tablet_info.get_status());
            return OB_SUCCESS;
          };
          ret = conn->step_execute(stmt, binder);
          if (OB_FAIL(ret)) {
            LOG_WARN("failed to execute step", K(ret));
          }
        }
      }

      // Finalize statement (but don't commit/rollback - caller manages transaction)
      conn->finalize_execute(stmt);
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
    LOG_WARN("not init", K(ret));
  } else if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "count", tablet_infos.count());
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->begin_transaction())) {
      LOG_WARN("failed to begin transaction", K(ret));
    } else {
      // Use the overload with connection parameter
      ret = batch_remove(guard.get_connection(), tablet_infos);

      // Commit or rollback transaction
      if (OB_FAIL(ret)) {
        int rollback_ret = guard->rollback();
        if (OB_SUCCESS != rollback_ret) {
          LOG_WARN("failed to rollback", K(rollback_ret));
        }
      } else {
        if (OB_FAIL(guard->commit())) {
          LOG_WARN("failed to commit", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObTabletMetaTableStorage::batch_remove(
    ObSQLiteConnection *conn,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else if (tablet_infos.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "count", tablet_infos.count());
  } else {
    const char *delete_sql =
      "DELETE FROM __all_tablet_meta_table "
      "WHERE tablet_id = ?;";

    share::ObSQLiteStmt *stmt = nullptr;
    if (OB_FAIL(conn->prepare_execute(delete_sql, stmt))) {
      LOG_WARN("failed to prepare batch execute", K(ret));
    } else {
      int64_t current_index = 0;

      while (OB_SUCC(ret) && current_index < tablet_infos.count()) {
        const ObTabletRuntimeInfo &tablet_info = tablet_infos.at(current_index++);
        if (OB_UNLIKELY(!tablet_info.primary_keys_are_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid tablet runtime metadata key", K(ret), K(tablet_info));
        } else {
          auto binder = [&](share::ObSQLiteBinder &b) -> int {
            b.bind_int64(tablet_info.get_tablet_id().id());
            return OB_SUCCESS;
          };
          ret = conn->step_execute(stmt, binder);
          if (OB_FAIL(ret)) {
            LOG_WARN("failed to execute step", K(ret));
          }
        }
      }

      // Finalize statement (but don't commit/rollback - caller manages transaction)
      conn->finalize_execute(stmt);
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT data_size "
      "FROM __all_tablet_meta_table "
      "WHERE tablet_id = ?;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_int64(tablet_id.id());
      return OB_SUCCESS;
    };

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      int64_t data_size_val = reader.get_int64();
      if (data_size_val > 0) {
        data_size = data_size_val;
      }
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT report_scn, status "
      "FROM __all_tablet_meta_table "
      "WHERE tablet_id = ?;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_int64(tablet_id.id());
      return OB_SUCCESS;
    };

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      report_scn = reader.get_int64();
      status = reader.get_int64();
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT compaction_scn FROM __all_tablet_meta_table "
      "ORDER BY compaction_scn ASC LIMIT 1;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      return OB_SUCCESS;
    };

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      min_compaction_scn = reader.get_int64();
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT COUNT(*) as cnt "
      "FROM __all_tablet_meta_table "
      ";";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      return OB_SUCCESS;
    };

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      tablet_count = reader.get_int64();
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      LOG_WARN("failed to query", K(ret));
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
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (tablet_ids.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "tablet count", tablet_ids.count());
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->begin_transaction())) {
      LOG_WARN("failed to begin transaction", K(ret));
    } else {
      // Build SQL with IN clause
      ObSqlString sql;
      if (OB_FAIL(sql.append_fmt(
          "UPDATE __all_tablet_meta_table "
          "SET report_scn = %lu "
          "WHERE tablet_id IN (",
          report_scn))) {
        LOG_WARN("failed to append sql", K(ret));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
          const ObTabletID &tablet_id = tablet_ids.at(i);
          if (OB_FAIL(sql.append_fmt(
              "%s %ld",
              i == 0 ? "" : ",",
              tablet_id.id()))) {
            LOG_WARN("failed to append sql", K(ret));
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(sql.append_fmt(
              ") AND compaction_scn >= %lu AND report_scn < %lu AND status != %ld;",
              compaction_scn_min, report_scn, except_status))) {
            LOG_WARN("failed to append sql", K(ret));
          }
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(guard->execute(sql.ptr(), nullptr, &affected_rows))) {
          LOG_WARN("failed to execute update", K(ret));
        } else if (OB_FAIL(guard->commit())) {
          LOG_WARN("failed to commit transaction", K(ret));
        }
      } else {
        int tmp_ret = guard->rollback();
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("failed to rollback transaction", K(tmp_ret));
        }
      }
    }
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
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (tablet_ids.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), "tablet_ids count", tablet_ids.count());
  } else {
    // Build SQL with IN clause
    ObSqlString sql;
    if (OB_FAIL(sql.append_fmt(
        "UPDATE __all_tablet_meta_table "
        "SET report_scn = CASE WHEN compaction_scn > %lu THEN %lu ELSE compaction_scn END "
        "WHERE tablet_id IN (",
        major_frozen_scn, major_frozen_scn))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
        if (OB_FAIL(sql.append_fmt(
            "%s %ld",
            i == 0 ? "" : ",",
            tablet_ids.at(i).id()))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql.append_fmt(") AND report_scn < %lu;", major_frozen_scn))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->execute(sql.ptr(), nullptr, &affected_rows))) {
        LOG_WARN("failed to execute update", K(ret));
      }
    }
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *update_sql =
      "UPDATE __all_tablet_meta_table "
      "SET report_scn = ? "
      "WHERE tablet_id >= ? AND tablet_id <= ? "
      "  AND compaction_scn >= ? AND report_scn < ? AND status != ?;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_int64(report_scn);
      b.bind_int64(start_tablet_id.id());
      b.bind_int64(end_tablet_id.id());
      b.bind_int64(compaction_scn_min);
      b.bind_int64(report_scn);
      b.bind_int64(except_status);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(update_sql, binder, &affected_rows))) {
      LOG_WARN("failed to execute update", K(ret));
    }
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *update_sql =
      "UPDATE __all_tablet_meta_table "
      "SET status = ? "
      "WHERE tablet_id >= ? AND tablet_id <= ? AND status = ?;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_int64(to_status);
      b.bind_int64(start_tablet_id.id());
      b.bind_int64(end_tablet_id.id());
      b.bind_int64(from_status);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(update_sql, binder, &affected_rows))) {
      LOG_WARN("failed to execute update", K(ret));
    }
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
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT tablet_id "
      "FROM __all_tablet_meta_table "
      "WHERE tablet_id > ? "
      "ORDER BY tablet_id ASC "
      "LIMIT ?;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_int64(start_tablet_id.id());
      b.bind_int64(limit);
      return OB_SUCCESS;
    };

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      int64_t tablet_id_val = reader.get_int64();
      if (OB_FAIL(tablet_ids.push_back(common::ObTabletID(tablet_id_val)))) {
        LOG_WARN("failed to push back tablet_id", K(ret));
      }
      return ret;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else if (tablet_ids.count() <= 0) {
    // Empty input, return empty result
  } else {
    // Build SQL with IN clause
    ObSqlString sql;
    if (OB_FAIL(sql.append_fmt(
        "SELECT tablet_id "
        "FROM __all_tablet_meta_table "
        "WHERE tablet_id IN ("))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
        if (OB_FAIL(sql.append_fmt(
            "%s %ld",
            i == 0 ? "" : ",",
            tablet_ids.at(i).id()))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql.append_fmt(") AND report_scn < %lu;", report_scn_max))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
        int64_t tablet_id_val = reader.get_int64();
        if (OB_FAIL(result_tablet_ids.push_back(common::ObTabletID(tablet_id_val)))) {
          LOG_WARN("failed to push back tablet_id", K(ret));
        }
        return ret;
      };

      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->query(sql.ptr(), nullptr, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(batch_size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid batch size", K(ret), K(batch_size));
  } else {
    const char *select_sql =
      "SELECT tablet_id "
      "FROM __all_tablet_meta_table "
      "WHERE tablet_id > ? "
      "ORDER BY tablet_id ASC "
      "LIMIT 1 OFFSET ?;";

    auto binder = [&](share::ObSQLiteBinder &b) -> int {
      b.bind_int64(start_tablet_id.id());
      b.bind_int64(batch_size - 1);
      return OB_SUCCESS;
    };

    auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
      int64_t tablet_id_val = reader.get_int64();
      max_tablet_id = common::ObTabletID(tablet_id_val);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_ITER_END; // No more tablets
      } else {
        LOG_WARN("failed to query", K(ret));
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
    LOG_WARN("not init", K(ret));
  } else {
    ObSqlString sql;
    if (OB_FAIL(sql.append_fmt(
        "SELECT tablet_id, "
        "       compaction_scn, data_size, required_size, report_scn, status "
        "FROM __all_tablet_meta_table "
        "WHERE tablet_id > %ld AND tablet_id <= %ld",
        start_tablet_id.id(), end_tablet_id.id()))) {
      LOG_WARN("failed to append sql", K(ret));
    } else if (only_unreported && OB_FAIL(sql.append_fmt(" AND report_scn < %ld", compaction_scn))) {
      LOG_WARN("failed to append sql", K(ret));
    } else if (OB_FAIL(sql.append(" ORDER BY tablet_id"))) {
      LOG_WARN("failed to append ordering", K(ret));
    }

    if (OB_SUCC(ret)) {
      ObTabletRuntimeInfoConstructor constructor;
      auto row_processor = [&](share::ObSQLiteRowReader &reader) -> int {
        ObTabletRuntimeInfo tablet_info;
        if (OB_FAIL(constructor(reader, tablet_info))) {
          LOG_WARN("failed to construct tablet runtime info", K(ret));
        } else if (OB_FAIL(tablet_infos.push_back(tablet_info))) {
          LOG_WARN("failed to append tablet runtime info", K(ret));
        }
        return ret;
      };

      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->query(sql.ptr(), nullptr, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to query", K(ret));
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
