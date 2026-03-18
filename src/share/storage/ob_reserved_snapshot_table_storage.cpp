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

#include "share/storage/ob_reserved_snapshot_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/net/ob_addr.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObReservedSnapshotTableStorage::ObReservedSnapshotTableStorage()
  : pool_(nullptr)
{
}

ObReservedSnapshotTableStorage::~ObReservedSnapshotTableStorage()
{
}

int ObReservedSnapshotTableStorage::init(ObSQLiteConnectionPool *pool)
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

int ObReservedSnapshotTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_RESERVED_SNAPSHOT, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObReservedSnapshotTableStorage::insert_or_update(
    const uint64_t tenant_id,
    const ObIArray<ObReservedSnapshotEntry> &entries)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (entries.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("entries is empty", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      // Prepare batch execute
      const char *insert_sql =
        "INSERT INTO __all_reserved_snapshot "
        "(snapshot_type, create_time, snapshot_version, status) "
        "VALUES (?, ?, ?, ?) "
        "ON CONFLICT(snapshot_type) DO UPDATE SET "
        "create_time = excluded.create_time, "
        "snapshot_version = excluded.snapshot_version;";

      // Begin transaction for batch insert
      if (OB_FAIL(guard->begin_transaction())) {
        LOG_WARN("failed to begin transaction", K(ret));
      } else {
        ObSQLiteStmt *stmt = nullptr;
        if (OB_FAIL(guard->prepare_execute(insert_sql, stmt))) {
          LOG_WARN("failed to prepare execute", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < entries.count(); ++i) {
            const ObReservedSnapshotEntry &entry = entries.at(i);
            auto binder = [&entry](ObSQLiteBinder &b) -> int {
              b.bind_int64(entry.snapshot_type_);
              b.bind_int64(entry.create_time_);
              b.bind_int64(entry.snapshot_version_);
              b.bind_int64(entry.status_);
              return OB_SUCCESS;
            };

            int64_t affected_rows = 0;
            if (OB_FAIL(guard->step_execute(stmt, binder, &affected_rows))) {
              LOG_WARN("failed to step execute", K(ret), K(i));
            }
          }

          // Finalize statement
          guard->finalize_execute(stmt);

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
    }
  }
  return ret;
}

int ObReservedSnapshotTableStorage::update_status(
    const uint64_t tenant_id,
    const common::ObAddr &svr_addr,
    const uint64_t status)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(svr_addr);
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      const char *update_sql =
        "UPDATE __all_reserved_snapshot "
        "SET status = ?;";

      auto binder = [status](ObSQLiteBinder &b) -> int {
        b.bind_int64(status);
        return OB_SUCCESS;
      };

      int64_t affected_rows = 0;
      if (OB_FAIL(guard->execute(update_sql, binder, &affected_rows))) {
        LOG_WARN("failed to execute update", K(ret));
      }
    }
  }
  return ret;
}

int ObReservedSnapshotTableStorage::get(
    const uint64_t tenant_id,
    const uint64_t snapshot_type,
    const common::ObAddr &svr_addr,
    ObReservedSnapshotEntry &entry)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  entry.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      const char *select_sql =
        "SELECT snapshot_type, "
        "       create_time, snapshot_version, status "
        "FROM __all_reserved_snapshot "
        "WHERE snapshot_type = ?;";

      auto binder = [snapshot_type](ObSQLiteBinder &b) -> int {
        b.bind_int64(snapshot_type);
        return OB_SUCCESS;
      };

      auto row_processor = [&entry, &svr_addr](ObSQLiteRowReader &reader) -> int {
        int ret = OB_SUCCESS;
        entry.snapshot_type_ = reader.get_int64();
        entry.create_time_ = reader.get_int64();
        entry.snapshot_version_ = reader.get_int64();
        entry.status_ = reader.get_int64();
        // svr_addr_ is no longer stored in table, use the input parameter
        entry.svr_addr_ = svr_addr;
        return ret;
      };

      if (OB_FAIL(guard->query(select_sql, binder, row_processor))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("failed to query", K(ret));
        } else {
          ret = OB_ENTRY_NOT_EXIST;
        }
      }
    }
  }
  return ret;
}

int ObReservedSnapshotTableStorage::get_all(
    const uint64_t tenant_id,
    ObIArray<ObReservedSnapshotEntry> &entries)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  entries.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      const char *select_sql =
        "SELECT snapshot_type, "
        "       create_time, snapshot_version, status "
        "FROM __all_reserved_snapshot "
        "ORDER BY snapshot_type;";

      ObSQLiteStmt *stmt = nullptr;
      if (OB_FAIL(guard->prepare_query(select_sql, nullptr, stmt))) {
        LOG_WARN("failed to prepare query", K(ret));
      } else {
        ObSQLiteRowReader reader;
        while (OB_SUCC(ret)) {
          ret = guard->step_query(stmt, reader);
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          } else if (OB_FAIL(ret)) {
            LOG_WARN("failed to step query", K(ret));
          } else {
            ObReservedSnapshotEntry entry;
            entry.snapshot_type_ = reader.get_int64();
            entry.create_time_ = reader.get_int64();
            entry.snapshot_version_ = reader.get_int64();
            entry.status_ = reader.get_int64();
            // svr_addr_ is no longer stored in table, reset to invalid
            entry.svr_addr_.reset();

            if (OB_FAIL(entries.push_back(entry))) {
              LOG_WARN("failed to push back entry", K(ret));
            }
          }
        }

        if (stmt) {
          guard->finalize_query(stmt);
        }
      }
    }
  }
  return ret;
}

int ObReservedSnapshotTableStorage::delete_expired(
    const uint64_t tenant_id,
    const common::ObAddr &svr_addr)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  UNUSED(svr_addr);
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      const char *delete_sql = "DELETE FROM __all_reserved_snapshot;";

      int64_t affected_rows = 0;
      if (OB_FAIL(guard->execute(delete_sql, nullptr, &affected_rows))) {
        LOG_WARN("failed to execute delete", K(ret));
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
