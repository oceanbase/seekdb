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

#include "share/storage/ob_column_checksum_error_info_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "share/ob_column_checksum_error_operator.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObColumnChecksumErrorInfoTableStorage::ObColumnChecksumErrorInfoTableStorage()
  : pool_(nullptr)
{
}

ObColumnChecksumErrorInfoTableStorage::~ObColumnChecksumErrorInfoTableStorage()
{
}

int ObColumnChecksumErrorInfoTableStorage::init(ObSQLiteConnectionPool *pool)
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

int ObColumnChecksumErrorInfoTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_COLUMN_CHECKSUM_ERROR_INFO, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObColumnChecksumErrorInfoTableStorage::insert(const ObColumnChecksumErrorInfo &error_info)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *insert_sql =
      "INSERT INTO __all_column_checksum_error_info "
      "(frozen_scn, index_type, data_table_id, index_table_id, "
      " data_tablet_id, index_tablet_id, column_id, data_column_checksum, index_column_checksum) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(error_info.frozen_scn_.get_val_for_inner_table_field());
      b.bind_int64(error_info.is_global_index_ ? 1 : 0);
      b.bind_int64(error_info.data_table_id_);
      b.bind_int64(error_info.index_table_id_);
      b.bind_int64(error_info.data_tablet_id_.id());
      b.bind_int64(error_info.index_tablet_id_.id());
      b.bind_int64(error_info.column_id_);
      b.bind_int64(error_info.data_column_checksum_);
      b.bind_int64(error_info.index_column_checksum_);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(insert_sql, binder))) {
      LOG_WARN("failed to execute insert", K(ret));
    }
  }
  return ret;
}

int ObColumnChecksumErrorInfoTableStorage::insert_all(const ObIArray<ObColumnChecksumErrorInfo> &error_infos)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (error_infos.empty()) {
    // do nothing
  } else {
    const char *insert_sql =
      "INSERT INTO __all_column_checksum_error_info "
      "(frozen_scn, index_type, data_table_id, index_table_id, "
      " data_tablet_id, index_tablet_id, column_id, data_column_checksum, index_column_checksum) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      // Begin transaction for batch insert
      if (OB_FAIL(guard->begin_transaction())) {
        LOG_WARN("failed to begin transaction", K(ret));
      } else {
        ObSQLiteStmt *stmt = nullptr;
        if (OB_FAIL(guard->prepare_execute(insert_sql, stmt))) {
          LOG_WARN("failed to prepare execute", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < error_infos.count(); ++i) {
            const ObColumnChecksumErrorInfo &error_info = error_infos.at(i);
            auto binder = [&](ObSQLiteBinder &b) -> int {
              b.bind_int64(error_info.frozen_scn_.get_val_for_inner_table_field());
              b.bind_int64(error_info.is_global_index_ ? 1 : 0);
              b.bind_int64(error_info.data_table_id_);
              b.bind_int64(error_info.index_table_id_);
              b.bind_int64(error_info.data_tablet_id_.id());
              b.bind_int64(error_info.index_tablet_id_.id());
              b.bind_int64(error_info.column_id_);
              b.bind_int64(error_info.data_column_checksum_);
              b.bind_int64(error_info.index_column_checksum_);
              return OB_SUCCESS;
            };

            if (OB_FAIL(guard->step_execute(stmt, binder))) {
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

int ObColumnChecksumErrorInfoTableStorage::get(
    const uint64_t tenant_id,
    const SCN &frozen_scn,
    const bool is_global_index,
    const int64_t data_table_id,
    const int64_t index_table_id,
    const common::ObTabletID &data_tablet_id,
    const common::ObTabletID &index_tablet_id,
    ObIArray<ObColumnChecksumErrorInfo> &error_infos)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  error_infos.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT frozen_scn, index_type, data_table_id, index_table_id, "
      "       data_tablet_id, index_tablet_id, column_id, data_column_checksum, index_column_checksum "
      "FROM __all_column_checksum_error_info "
      "WHERE frozen_scn = ? AND index_type = ? "
      "  AND data_table_id = ? AND index_table_id = ? "
      "  AND data_tablet_id = ? AND index_tablet_id = ? "
      "ORDER BY column_id;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(frozen_scn.get_val_for_inner_table_field());
      b.bind_int64(is_global_index ? 1 : 0);
      b.bind_int64(data_table_id);
      b.bind_int64(index_table_id);
      b.bind_int64(data_tablet_id.id());
      b.bind_int64(index_tablet_id.id());
      return OB_SUCCESS;
    };

    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      ObColumnChecksumErrorInfo error_info;
      uint64_t frozen_scn_val = reader.get_int64();
      int64_t index_type = reader.get_int64();
      int64_t data_table_id_val = reader.get_int64();
      int64_t index_table_id_val = reader.get_int64();
      int64_t data_tablet_id_val = reader.get_int64();
      int64_t index_tablet_id_val = reader.get_int64();
      int64_t column_id_val = reader.get_int64();
      int64_t data_column_checksum_val = reader.get_int64();
      int64_t index_column_checksum_val = reader.get_int64();

      error_info.frozen_scn_.convert_for_inner_table_field(frozen_scn_val);
      error_info.is_global_index_ = (index_type != 0);
      error_info.data_table_id_ = data_table_id_val;
      error_info.index_table_id_ = index_table_id_val;
      error_info.data_tablet_id_ = ObTabletID(data_tablet_id_val);
      error_info.index_tablet_id_ = ObTabletID(index_tablet_id_val);
      error_info.column_id_ = column_id_val;
      error_info.data_column_checksum_ = data_column_checksum_val;
      error_info.index_column_checksum_ = index_column_checksum_val;

      if (OB_FAIL(error_infos.push_back(error_info))) {
        LOG_WARN("failed to push back error info", K(ret));
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

int ObColumnChecksumErrorInfoTableStorage::delete_expired(
    const uint64_t tenant_id,
    const SCN &frozen_scn_before,
    int64_t limit)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // SQLite doesn't support DELETE ... LIMIT, use subquery with rowid instead
    ObSqlString sql;
    if (OB_FAIL(sql.append_fmt("DELETE FROM __all_column_checksum_error_info "
                                "WHERE rowid IN ("
                                "  SELECT rowid FROM __all_column_checksum_error_info "
                                "  WHERE frozen_scn < %lu "
                                "  ORDER BY frozen_scn "
                                "  LIMIT %ld"
                                ")",
                                frozen_scn_before.get_val_for_inner_table_field(), limit))) {
      LOG_WARN("failed to format sql", K(ret));
    } else {
      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->execute(sql.ptr(), nullptr))) {
        LOG_WARN("failed to execute delete", K(ret));
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
