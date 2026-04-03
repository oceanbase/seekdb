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

#include "share/storage/ob_tenant_event_history_table_storage.h"
#include "share/storage/ob_sqlite_connection.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/net/ob_addr.h"
#include "lib/string/ob_sql_string.h"

#include "share/storage/ob_sqlite_table_schema.h"

namespace oceanbase
{
namespace share
{

ObTenantEventHistoryTableStorage::ObTenantEventHistoryTableStorage()
  : pool_(nullptr)
{
}

ObTenantEventHistoryTableStorage::~ObTenantEventHistoryTableStorage()
{
}

int ObTenantEventHistoryTableStorage::init(ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pool_ = pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else if (OB_FAIL(create_table_if_not_exists())) {
    LOG_WARN("failed to create table", K(ret));
  }
  if (OB_FAIL(ret)) {
    pool_ = nullptr;
  }
  return ret;
}

int ObTenantEventHistoryTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_TENANT_EVENT_HISTORY, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObTenantEventHistoryTableStorage::insert(const ObTenantEventHistoryEntry &entry)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *insert_sql =
      "INSERT INTO __all_tenant_event_history "
      "(gmt_create, module, event, "
      "name1, value1, name2, value2, name3, value3, "
      "name4, value4, name5, value5, name6, value6, extra_info, "
      "trace_id, cost_time, ret_code, error_msg) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    if (OB_SUCC(ret)) {
      auto binder = [&](ObSQLiteBinder &b) -> int {
        b.bind_int64(entry.gmt_create_);
        b.bind_text(entry.module_.empty() ? "" : entry.module_.ptr(), entry.module_.length());
        b.bind_text(entry.event_.empty() ? "" : entry.event_.ptr(), entry.event_.length());
        b.bind_text(entry.name1_.empty() ? "" : entry.name1_.ptr(), entry.name1_.length());
        b.bind_text(entry.value1_.empty() ? "" : entry.value1_.ptr(), entry.value1_.length());
        b.bind_text(entry.name2_.empty() ? "" : entry.name2_.ptr(), entry.name2_.length());
        b.bind_text(entry.value2_.empty() ? "" : entry.value2_.ptr(), entry.value2_.length());
        b.bind_text(entry.name3_.empty() ? "" : entry.name3_.ptr(), entry.name3_.length());
        b.bind_text(entry.value3_.empty() ? "" : entry.value3_.ptr(), entry.value3_.length());
        b.bind_text(entry.name4_.empty() ? "" : entry.name4_.ptr(), entry.name4_.length());
        b.bind_text(entry.value4_.empty() ? "" : entry.value4_.ptr(), entry.value4_.length());
        b.bind_text(entry.name5_.empty() ? "" : entry.name5_.ptr(), entry.name5_.length());
        b.bind_text(entry.value5_.empty() ? "" : entry.value5_.ptr(), entry.value5_.length());
        b.bind_text(entry.name6_.empty() ? "" : entry.name6_.ptr(), entry.name6_.length());
        b.bind_text(entry.value6_.empty() ? "" : entry.value6_.ptr(), entry.value6_.length());
        b.bind_text(entry.extra_info_.empty() ? "" : entry.extra_info_.ptr(), entry.extra_info_.length());
        b.bind_text(entry.trace_id_.empty() ? "" : entry.trace_id_.ptr(), entry.trace_id_.length());
        b.bind_int64(entry.cost_time_);
        b.bind_int64(entry.ret_code_);
        b.bind_text(entry.error_msg_.empty() ? "" : entry.error_msg_.ptr(), entry.error_msg_.length());
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
  }
  return ret;
}

int ObTenantEventHistoryTableStorage::insert_all(const ObIArray<ObTenantEventHistoryEntry> &entries)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (entries.empty()) {
    // do nothing
  } else {
    const char *insert_sql =
      "INSERT INTO __all_tenant_event_history "
      "(gmt_create, module, event, "
      "name1, value1, name2, value2, name3, value3, "
      "name4, value4, name5, value5, name6, value6, extra_info, "
      "trace_id, cost_time, ret_code, error_msg) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else {
      // Begin transaction for batch insert
      if (OB_FAIL(guard->execute("BEGIN TRANSACTION;", nullptr))) {
        LOG_WARN("failed to begin transaction", K(ret));
      } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < entries.count(); ++i) {
          const ObTenantEventHistoryEntry &entry = entries.at(i);

          if (OB_SUCC(ret)) {
            auto binder = [&](ObSQLiteBinder &b) -> int {
              b.bind_int64(entry.gmt_create_);
              b.bind_text(entry.module_.empty() ? "" : entry.module_.ptr(), entry.module_.length());
              b.bind_text(entry.event_.empty() ? "" : entry.event_.ptr(), entry.event_.length());
              b.bind_text(entry.name1_.empty() ? "" : entry.name1_.ptr(), entry.name1_.length());
              b.bind_text(entry.value1_.empty() ? "" : entry.value1_.ptr(), entry.value1_.length());
              b.bind_text(entry.name2_.empty() ? "" : entry.name2_.ptr(), entry.name2_.length());
              b.bind_text(entry.value2_.empty() ? "" : entry.value2_.ptr(), entry.value2_.length());
              b.bind_text(entry.name3_.empty() ? "" : entry.name3_.ptr(), entry.name3_.length());
              b.bind_text(entry.value3_.empty() ? "" : entry.value3_.ptr(), entry.value3_.length());
              b.bind_text(entry.name4_.empty() ? "" : entry.name4_.ptr(), entry.name4_.length());
              b.bind_text(entry.value4_.empty() ? "" : entry.value4_.ptr(), entry.value4_.length());
              b.bind_text(entry.name5_.empty() ? "" : entry.name5_.ptr(), entry.name5_.length());
              b.bind_text(entry.value5_.empty() ? "" : entry.value5_.ptr(), entry.value5_.length());
              b.bind_text(entry.name6_.empty() ? "" : entry.name6_.ptr(), entry.name6_.length());
              b.bind_text(entry.value6_.empty() ? "" : entry.value6_.ptr(), entry.value6_.length());
              b.bind_text(entry.extra_info_.empty() ? "" : entry.extra_info_.ptr(), entry.extra_info_.length());
              b.bind_text(entry.trace_id_.empty() ? "" : entry.trace_id_.ptr(), entry.trace_id_.length());
              b.bind_int64(entry.cost_time_);
              b.bind_int64(entry.ret_code_);
              b.bind_text(entry.error_msg_.empty() ? "" : entry.error_msg_.ptr(), entry.error_msg_.length());
              return OB_SUCCESS;
            };
            if (OB_FAIL(guard->execute(insert_sql, binder))) {
              LOG_WARN("failed to execute insert", K(ret), K(i));
            }
          }
        }

        if (OB_SUCC(ret)) {
          if (OB_FAIL(guard->execute("COMMIT;", nullptr))) {
            LOG_WARN("failed to commit transaction", K(ret));
          }
        } else {
          int tmp_ret = OB_SUCCESS;
          if (OB_SUCCESS != (tmp_ret = guard->execute("ROLLBACK;", nullptr))) {
            LOG_WARN("failed to rollback transaction", K(tmp_ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObTenantEventHistoryTableStorage::delete_expired(int64_t gmt_create_before, int64_t limit)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *delete_sql =
      "DELETE FROM __all_tenant_event_history "
      "WHERE gmt_create < ? "
      "LIMIT ?;";

    auto binder = [&](ObSQLiteBinder &b) -> int {
      b.bind_int64(gmt_create_before);
      b.bind_int64(limit);
      return OB_SUCCESS;
    };
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->execute(delete_sql, binder))) {
      LOG_WARN("failed to execute delete", K(ret));
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
