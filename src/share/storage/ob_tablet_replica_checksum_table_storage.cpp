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

#include "share/storage/ob_tablet_replica_checksum_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/net/ob_addr.h"
#include "share/ob_tablet_replica_checksum_operator.h"
#include "observer/ob_server_struct.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObTabletReplicaChecksumTableStorage::ObTabletReplicaChecksumTableStorage()
  : pool_(nullptr)
{
}

ObTabletReplicaChecksumTableStorage::~ObTabletReplicaChecksumTableStorage()
{
}

int ObTabletReplicaChecksumTableStorage::init(ObSQLiteConnectionPool *pool)
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

int ObTabletReplicaChecksumTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_TABLET_REPLICA_CHECKSUM, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumTableStorage::batch_insert_or_update(
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplicaChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (items.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("items is empty", K(ret));
  } else {
    UNUSED(tenant_id);
    const char *upsert_sql =
      "INSERT INTO __all_tablet_replica_checksum "
      "(tablet_id, compaction_scn, "
      " row_count, data_checksum, column_checksums, b_column_checksums, "
      " data_checksum_type, co_base_snapshot_version) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
      "ON CONFLICT(tablet_id) DO UPDATE SET "
      "compaction_scn = excluded.compaction_scn, "
      "row_count = excluded.row_count, "
      "data_checksum = excluded.data_checksum, "
      "column_checksums = excluded.column_checksums, "
      "b_column_checksums = excluded.b_column_checksums, "
      "data_checksum_type = excluded.data_checksum_type, "
      "co_base_snapshot_version = excluded.co_base_snapshot_version;";

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
        if (OB_FAIL(guard->prepare_execute(upsert_sql, stmt))) {
          LOG_WARN("failed to prepare execute", K(ret));
        } else {
          common::ObArenaAllocator allocator;
          for (int64_t i = 0; OB_SUCC(ret) && i < items.count(); ++i) {
            const ObTabletReplicaChecksumItem &item = items.at(i);
            // Convert column_meta to string
            common::ObString column_checksums_str;
            common::ObString b_column_checksums_str;
            if (item.column_meta_.is_valid()) {
              if (OB_FAIL(ObTabletReplicaChecksumOperator::get_visible_column_meta(
                  item.column_meta_, allocator, column_checksums_str))) {
                LOG_WARN("failed to get visible column meta", K(ret));
              } else {
                // Serialize b_column_checksums using get_str_obj
                common::ObObj obj;
                if (OB_FAIL(item.column_meta_.get_str_obj(item.data_checksum_type_, allocator, obj, b_column_checksums_str))) {
                  LOG_WARN("failed to get hex column meta str", K(ret));
                }
              }
            }

            if (OB_SUCC(ret)) {
              auto binder = [&](ObSQLiteBinder &b) -> int {
                b.bind_int64(item.tablet_id_.id());
                b.bind_int64(item.compaction_scn_.get_val_for_inner_table_field());
                b.bind_int64(item.row_count_);
                b.bind_int64(item.data_checksum_);
                if (column_checksums_str.empty()) {
                  b.bind_text("", 0);
                } else {
                  b.bind_text(column_checksums_str.ptr(), column_checksums_str.length());
                }
                if (b_column_checksums_str.empty()) {
                  b.bind_blob(nullptr, 0);
                } else {
                  b.bind_blob(b_column_checksums_str.ptr(), b_column_checksums_str.length());
                }
                b.bind_int64(static_cast<int64_t>(item.data_checksum_type_));
                b.bind_int64(item.co_base_snapshot_version_.get_val_for_inner_table_field());
                return OB_SUCCESS;
              };

              if (OB_FAIL(guard->step_execute(stmt, binder))) {
                LOG_WARN("failed to step execute", K(ret), K(i));
              }
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

int ObTabletReplicaChecksumTableStorage::batch_remove(
    const uint64_t tenant_id,
    const ObIArray<ObTabletReplica> &replicas)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (replicas.empty()) {
    // do nothing
  } else {
    const char *delete_sql =
      "DELETE FROM __all_tablet_replica_checksum "
      "WHERE tablet_id = ?;";

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      // Begin transaction for batch remove
      if (OB_FAIL(guard->begin_transaction())) {
        LOG_WARN("failed to begin transaction", K(ret));
      } else {
        ObSQLiteStmt *stmt = nullptr;
        if (OB_FAIL(guard->prepare_execute(delete_sql, stmt))) {
          LOG_WARN("failed to prepare execute", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < replicas.count(); ++i) {
            const ObTabletReplica &replica = replicas.at(i);
            if (replica.primary_keys_are_valid()) {
              auto binder = [&](ObSQLiteBinder &b) -> int {
                b.bind_int64(replica.get_tablet_id().id());
                return OB_SUCCESS;
              };
              if (OB_FAIL(guard->step_execute(stmt, binder))) {
                LOG_WARN("failed to step execute", K(ret), K(i));
              }
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

int ObTabletReplicaChecksumTableStorage::remove_residual(
    const uint64_t tenant_id,
    const common::ObAddr &server,
    const int64_t limit,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  affected_rows = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    UNUSED(server);  // SQLite table is local, no need to filter by server
    const char *delete_sql =
      "DELETE FROM __all_tablet_replica_checksum "
      "LIMIT ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(limit);
      return OB_SUCCESS;
    };

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(delete_sql, binder, &affected_rows))) {
      LOG_WARN("failed to execute delete", K(ret));
    }
  }
  return ret;
}

int ObTabletReplicaChecksumTableStorage::batch_get(
    const uint64_t tenant_id,
    const ObIArray<ObTabletLSPair> &pairs,
    const SCN &compaction_scn,
    ObReplicaCkmArray &items,
    const bool include_larger_than)
{
  int ret = OB_SUCCESS;
  items.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (pairs.empty()) {
    // do nothing
  } else {
    UNUSED(tenant_id);
    // Build SQL with IN clause
    common::ObSqlString sql;
    if (OB_FAIL(sql.append_fmt(
        "SELECT tablet_id, compaction_scn, "
        "       row_count, data_checksum, column_checksums, b_column_checksums, "
        "       data_checksum_type, co_base_snapshot_version "
        "FROM __all_tablet_replica_checksum "
        "WHERE tablet_id IN ("))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < pairs.count(); ++i) {
        const ObTabletLSPair &pair = pairs.at(i);
        if (OB_FAIL(sql.append_fmt(
            "%s %ld",
            i == 0 ? "" : ",",
            pair.get_tablet_id().id()))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        const char *op = include_larger_than ? ">=" : "=";
        if (OB_FAIL(sql.append_fmt(
            ") AND compaction_scn %s %lu "
            "ORDER BY tablet_id;",
            op, compaction_scn.get_val_for_inner_table_field()))) {
          LOG_WARN("failed to append sql", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      common::ObArenaAllocator allocator;
      auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
        ObTabletReplicaChecksumItem item;
        int64_t tablet_id_val = reader.get_int64();
        uint64_t compaction_scn_val = reader.get_int64();
        int64_t row_count = reader.get_int64();
        int64_t data_checksum = reader.get_int64();
        int column_checksums_len = 0;
        int b_column_checksums_len = 0;
        const char *column_checksums_str = reader.get_text(&column_checksums_len);
        const void *b_column_checksums_blob = reader.get_blob(&b_column_checksums_len);
        int64_t data_checksum_type = reader.get_int64();
        uint64_t co_base_snapshot_version_val = reader.get_int64();

        UNUSED(column_checksums_str);
        item.tablet_id_ = ObTabletID(tablet_id_val);
        item.ls_id_ = ObLSID::SYS_LS_ID;
        item.server_ = GCTX.self_addr();
        item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val);
        item.row_count_ = row_count;
        item.data_checksum_ = data_checksum;
        item.data_checksum_type_ = static_cast<ObDataChecksumType>(data_checksum_type);
        item.co_base_snapshot_version_.convert_for_inner_table_field(co_base_snapshot_version_val);

        // Parse b_column_checksums blob (binary column checksums)
        // Note: column_checksums is only for display, b_column_checksums is the actual data source
        if (OB_NOT_NULL(b_column_checksums_blob) && b_column_checksums_len > 0) {
          common::ObString b_column_checksums_obstr(b_column_checksums_len, static_cast<const char *>(b_column_checksums_blob));
          int tmp_ret = item.column_meta_.set_with_str(item.data_checksum_type_, b_column_checksums_obstr);
          if (OB_SUCCESS != tmp_ret) {
            LOG_WARN("failed to set column meta with b_column_checksums blob, skip invalid data",
                     K(tmp_ret), K(b_column_checksums_obstr), K(item.tablet_id_), K(item.ls_id_));
            item.column_meta_.reset();
          }
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(items.push_back(item))) {
            LOG_WARN("failed to push back item", K(ret));
          }
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

int ObTabletReplicaChecksumTableStorage::range_get(
    const uint64_t tenant_id,
    const common::ObTabletID &start_tablet_id,
    const int64_t range_size,
    ObIArray<ObTabletReplicaChecksumItem> &items,
    int64_t &tablet_cnt)
{
  int ret = OB_SUCCESS;
  items.reset();
  tablet_cnt = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (range_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(range_size));
  } else {
    const char *select_sql =
      "SELECT tablet_id, compaction_scn, "
      "       row_count, data_checksum, column_checksums, b_column_checksums, "
      "       data_checksum_type, co_base_snapshot_version "
      "FROM __all_tablet_replica_checksum "
      "WHERE tablet_id > ? "
      "ORDER BY tablet_id "
      "LIMIT ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(start_tablet_id.id());
      b.bind_int64(range_size);
      return OB_SUCCESS;
    };

    common::ObArenaAllocator allocator;
    common::ObTabletID last_tablet_id;
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      ObTabletReplicaChecksumItem item;
      int64_t tablet_id_val = reader.get_int64();
      uint64_t compaction_scn_val = reader.get_int64();
      int64_t row_count = reader.get_int64();
      int64_t data_checksum = reader.get_int64();
      int column_checksums_len = 0;
      int b_column_checksums_len = 0;
      const char *column_checksums_str = reader.get_text(&column_checksums_len);
      const void *b_column_checksums_blob = reader.get_blob(&b_column_checksums_len);
      int64_t data_checksum_type = reader.get_int64();
      uint64_t co_base_snapshot_version_val = reader.get_int64();

      UNUSED(column_checksums_str);
      item.tablet_id_ = ObTabletID(tablet_id_val);
      item.ls_id_ = ObLSID::SYS_LS_ID;
      item.server_ = GCTX.self_addr();
      item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val);
      item.row_count_ = row_count;
      item.data_checksum_ = data_checksum;
      item.data_checksum_type_ = static_cast<ObDataChecksumType>(data_checksum_type);
      item.co_base_snapshot_version_.convert_for_inner_table_field(co_base_snapshot_version_val);

      // Parse b_column_checksums blob (binary column checksums)
      // Note: column_checksums is only for display, b_column_checksums is the actual data source
      if (OB_NOT_NULL(b_column_checksums_blob) && b_column_checksums_len > 0) {
        common::ObString b_column_checksums_obstr(b_column_checksums_len, static_cast<const char *>(b_column_checksums_blob));
        int tmp_ret = item.column_meta_.set_with_str(item.data_checksum_type_, b_column_checksums_obstr);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("failed to set column meta with b_column_checksums blob, skip invalid data",
                   K(tmp_ret), K(b_column_checksums_obstr), K(item.tablet_id_), K(item.ls_id_));
          item.column_meta_.reset();
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(items.push_back(item))) {
          LOG_WARN("failed to push back item", K(ret));
        } else {
          // Count distinct tablets
          if (last_tablet_id != item.tablet_id_) {
            tablet_cnt++;
            last_tablet_id = item.tablet_id_;
          }
        }
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

int ObTabletReplicaChecksumTableStorage::get_min_compaction_scn(
    const uint64_t tenant_id,
    uint64_t &min_compaction_scn)
{
  int ret = OB_SUCCESS;
  min_compaction_scn = UINT64_MAX;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT MIN(compaction_scn) as value FROM __all_tablet_replica_checksum "
      ";";

    auto binder = [&](ObSQLiteBinder &b) -> int {      return OB_SUCCESS;
    };

    bool found = false;
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      uint64_t value = reader.get_int64();
      if (value != 0) {
        min_compaction_scn = value;
        found = true;
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
        ret = OB_SUCCESS; // No rows is acceptable
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumTableStorage::get_max_row_count(
    const uint64_t tenant_id,
    const common::ObTabletID &tablet_id,
    const ObLSID &ls_id,
    int64_t &max_row_count)
{
  int ret = OB_SUCCESS;
  max_row_count = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT MAX(row_count) as max_row_count FROM __all_tablet_replica_checksum "
      "WHERE tablet_id = ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {      b.bind_int64(tablet_id.id());
      return OB_SUCCESS;
    };

    bool found = false;
    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      int64_t value = reader.get_int64();
      if (value > 0) {
        max_row_count = value;
        found = true;
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
        ret = OB_SUCCESS; // No rows is acceptable
      }
    }
  }
  return ret;
}

int ObTabletReplicaChecksumTableStorage::batch_check_checksum(
    const uint64_t tenant_id,
    const ObIArray<common::ObTabletID> &tablet_ids,
    const int64_t start_idx,
    const int64_t end_idx,
    bool &has_error)
{
  int ret = OB_SUCCESS;
  UNUSED(tenant_id);
  has_error = false;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (start_idx < 0 || end_idx > tablet_ids.count() || start_idx >= end_idx) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(start_idx), K(end_idx), "tablet_ids count", tablet_ids.count());
  } else {
    // Build SQL query to check for checksum errors
    ObSqlString sql;
    if (OB_FAIL(sql.append("SELECT tablet_id FROM ("
        "SELECT tablet_id, row_count, data_checksum, b_column_checksums, compaction_scn "
        "FROM __all_tablet_replica_checksum WHERE tablet_id IN ("))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {
      // Build IN clause
      for (int64_t i = start_idx; OB_SUCC(ret) && i < end_idx; ++i) {
        if (OB_FAIL(sql.append_fmt("%s%ld", i == start_idx ? "" : ",", tablet_ids.at(i).id()))) {
          LOG_WARN("failed to append tablet_id", K(ret));
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(sql.append(")) as J GROUP BY J.tablet_id, J.compaction_scn "
          "HAVING MIN(J.data_checksum) != MAX(J.data_checksum) "
          "OR MIN(J.row_count) != MAX(J.row_count) "
          "OR MIN(J.b_column_checksums) != MAX(J.b_column_checksums) LIMIT 1"))) {
        LOG_WARN("failed to append sql", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      auto binder = [&](ObSQLiteBinder &b) -> int {
        // No parameters needed, all values are in SQL string
        return OB_SUCCESS;
      };

      auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
        has_error = true; // If we get any row, there's an error
        return OB_SUCCESS;
      };

      ObSQLiteConnectionGuard guard(pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->query(sql.ptr(), binder, row_processor))) {
        if (OB_ENTRY_NOT_EXIST != ret) {
          LOG_WARN("failed to query", K(ret));
        } else {
          ret = OB_SUCCESS; // No rows means no error
        }
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
