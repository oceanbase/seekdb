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

// SQLite storage for local tablet checksums.

#include "share/storage/ob_tablet_local_checksum_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_print_utils.h"
#include "share/ob_tablet_local_checksum_operator.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObTabletLocalChecksumTableStorage::ObTabletLocalChecksumTableStorage()
  : pool_(nullptr)
{
}

ObTabletLocalChecksumTableStorage::~ObTabletLocalChecksumTableStorage()
{
}

int ObTabletLocalChecksumTableStorage::init(ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_ = pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else if (OB_FAIL(create_table_if_not_exists())) {
  }
  if (OB_FAIL(ret)) {
    pool_ = NULL;
  }
  return ret;
}

int ObTabletLocalChecksumTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_TABLET_LOCAL_CHECKSUM, nullptr))) {
    }
  }
  return ret;
}

int ObTabletLocalChecksumTableStorage::batch_get(
    const ObIArray<common::ObTabletID> &tablet_ids,
    const SCN &compaction_scn,
    ObLocalTabletChecksumArray &items,
    const bool include_larger_than)
{
  int ret = OB_SUCCESS;
  items.reset();
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (tablet_ids.empty()) {
    // do nothing
  } else {
    // Build SQL with IN clause
    common::ObSqlString sql;
    if (OB_FAIL(sql.append_fmt(
        "SELECT tablet_id, compaction_scn, "
        "       row_count, data_checksum, column_checksums, b_column_checksums, "
        "       data_checksum_type "
        "FROM __all_tablet_local_checksum "
        "WHERE tablet_id IN ("))) {
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
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(sql.append(")"))) {
        } else if (compaction_scn.is_valid()) {
          const char *op = include_larger_than ? ">=" : "=";
          if (OB_FAIL(sql.append_fmt(" AND compaction_scn %s %lu",
              op, compaction_scn.get_val_for_inner_table_field()))) {
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(sql.append(" ORDER BY tablet_id;"))) {
          LOG_WARN("failed to append ordering", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
        ObTabletLocalChecksumItem item;
        int64_t tablet_id_val = reader.get_int64();
        uint64_t compaction_scn_val = reader.get_int64();
        int64_t row_count = reader.get_int64();
        int64_t data_checksum = reader.get_int64();
        int column_checksums_len = 0;
        int b_column_checksums_len = 0;
        const char *column_checksums_str = reader.get_text(&column_checksums_len);
        const void *b_column_checksums_blob = reader.get_blob(&b_column_checksums_len);
        int64_t data_checksum_type = reader.get_int64();
        UNUSED(column_checksums_str);
        if (!is_valid_data_checksum_type(static_cast<ObDataChecksumType>(data_checksum_type))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid local checksum type", K(ret), K(data_checksum_type), K(tablet_id_val));
        } else if (OB_FAIL(item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val))) {
        } else if (OB_ISNULL(b_column_checksums_blob) || b_column_checksums_len <= 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("local column checksum metadata is empty", K(ret), K(tablet_id_val));
        } else {
          item.tablet_id_ = ObTabletID(tablet_id_val);
          item.row_count_ = row_count;
          item.data_checksum_ = data_checksum;
          item.data_checksum_type_ = static_cast<ObDataChecksumType>(data_checksum_type);
          common::ObString b_column_checksums_obstr(b_column_checksums_len, static_cast<const char *>(b_column_checksums_blob));
          if (OB_FAIL(item.column_meta_.set_with_str(item.data_checksum_type_, b_column_checksums_obstr))) {
          }
        }

        if (OB_SUCC(ret) && OB_UNLIKELY(!item.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid local checksum item", K(ret), K(item));
        } else if (OB_SUCC(ret) && OB_FAIL(items.push_back(item))) {
          LOG_WARN("failed to push back item", K(ret));
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

int ObTabletLocalChecksumTableStorage::get_row_count(
    const common::ObTabletID &tablet_id,
    int64_t &row_count)
{
  int ret = OB_SUCCESS;
  row_count = 0;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *select_sql =
      "SELECT row_count FROM __all_tablet_local_checksum "
      "WHERE tablet_id = ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(tablet_id.id());
      return OB_SUCCESS;
    };

    auto row_processor = [&](ObSQLiteRowReader &reader) -> int {
      int64_t value = reader.get_int64();
      if (value > 0) {
        row_count = value;
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

} // namespace share
} // namespace oceanbase
