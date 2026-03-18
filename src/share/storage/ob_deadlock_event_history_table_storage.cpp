#define USING_LOG_PREFIX SHARE

#include "share/storage/ob_deadlock_event_history_table_storage.h"
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

ObDeadlockEventHistoryTableStorage::ObDeadlockEventHistoryTableStorage()
  : pool_(nullptr)
{
}

ObDeadlockEventHistoryTableStorage::~ObDeadlockEventHistoryTableStorage()
{
}

int ObDeadlockEventHistoryTableStorage::init(ObSQLiteConnectionPool *pool)
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

int ObDeadlockEventHistoryTableStorage::create_table_if_not_exists()
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
    } else if (OB_FAIL(guard->execute(SQLITE_CREATE_TABLE_DEADLOCK_EVENT_HISTORY, nullptr))) {
      LOG_WARN("failed to create table", K(ret));
    }
  }
  return ret;
}

int ObDeadlockEventHistoryTableStorage::insert(const ObDeadlockEventHistoryEntry &entry)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const char *insert_sql =
      "INSERT INTO __all_deadlock_event_history "
      "(event_id, detector_id, report_time, "
      "cycle_idx, cycle_size, role, priority_level, priority, create_time, "
      "start_delay, module, visitor, object, extra_name1, extra_value1, "
      "extra_name2, extra_value2, extra_name3, extra_value3) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

    auto binder = [&](ObSQLiteBinder &b) -> int {
        b.bind_int64(entry.event_id_);
        b.bind_int64(entry.detector_id_);
        b.bind_int64(entry.report_time_);
        b.bind_int64(entry.cycle_idx_);
        b.bind_int64(entry.cycle_size_);
        b.bind_text(entry.role_.empty() ? "" : entry.role_.ptr(), entry.role_.length());
        b.bind_text(entry.priority_level_.empty() ? "" : entry.priority_level_.ptr(), entry.priority_level_.length());
        b.bind_int64(entry.priority_);
        b.bind_int64(entry.create_time_);
        b.bind_int64(entry.start_delay_);
        b.bind_text(entry.module_.empty() ? "" : entry.module_.ptr(), entry.module_.length());
        b.bind_text(entry.visitor_.empty() ? "" : entry.visitor_.ptr(), entry.visitor_.length());
        b.bind_text(entry.object_.empty() ? "" : entry.object_.ptr(), entry.object_.length());
        b.bind_text(entry.extra_name1_.empty() ? "" : entry.extra_name1_.ptr(), entry.extra_name1_.length());
        b.bind_text(entry.extra_value1_.empty() ? "" : entry.extra_value1_.ptr(), entry.extra_value1_.length());
        b.bind_text(entry.extra_name2_.empty() ? "" : entry.extra_name2_.ptr(), entry.extra_name2_.length());
        b.bind_text(entry.extra_value2_.empty() ? "" : entry.extra_value2_.ptr(), entry.extra_value2_.length());
        b.bind_text(entry.extra_name3_.empty() ? "" : entry.extra_name3_.ptr(), entry.extra_name3_.length());
        b.bind_text(entry.extra_value3_.empty() ? "" : entry.extra_value3_.ptr(), entry.extra_value3_.length());
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

int ObDeadlockEventHistoryTableStorage::insert_all(const ObIArray<ObDeadlockEventHistoryEntry> &entries)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (entries.empty()) {
    // do nothing
  } else {
    const char *insert_sql =
      "INSERT INTO __all_deadlock_event_history "
      "(event_id, detector_id, report_time, "
      "cycle_idx, cycle_size, role, priority_level, priority, create_time, "
      "start_delay, module, visitor, object, extra_name1, extra_value1, "
      "extra_name2, extra_value2, extra_name3, extra_value3) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

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
          for (int64_t i = 0; OB_SUCC(ret) && i < entries.count(); ++i) {
            const ObDeadlockEventHistoryEntry &entry = entries.at(i);
            auto binder = [&](ObSQLiteBinder &b) -> int {
                b.bind_int64(entry.event_id_);
                b.bind_int64(entry.detector_id_);
                b.bind_int64(entry.report_time_);
                b.bind_int64(entry.cycle_idx_);
                b.bind_int64(entry.cycle_size_);
                b.bind_text(entry.role_.ptr(), entry.role_.length());
                b.bind_text(entry.priority_level_.ptr(), entry.priority_level_.length());
                b.bind_int64(entry.priority_);
                b.bind_int64(entry.create_time_);
                b.bind_int64(entry.start_delay_);
                b.bind_text(entry.module_.ptr(), entry.module_.length());
                b.bind_text(entry.visitor_.ptr(), entry.visitor_.length());
                b.bind_text(entry.object_.ptr(), entry.object_.length());
                b.bind_text(entry.extra_name1_.ptr(), entry.extra_name1_.length());
                b.bind_text(entry.extra_value1_.ptr(), entry.extra_value1_.length());
                b.bind_text(entry.extra_name2_.ptr(), entry.extra_name2_.length());
                b.bind_text(entry.extra_value2_.ptr(), entry.extra_value2_.length());
                b.bind_text(entry.extra_name3_.ptr(), entry.extra_name3_.length());
                b.bind_text(entry.extra_value3_.ptr(), entry.extra_value3_.length());
                return OB_SUCCESS;
              };
              if (OB_FAIL(guard->step_execute(stmt, binder))) {
                LOG_WARN("failed to step execute", K(ret));
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

int ObDeadlockEventHistoryTableStorage::delete_expired(int64_t report_time_before, int64_t limit)
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    // SQLite doesn't support DELETE ... LIMIT, use subquery with rowid instead
    ObSqlString sql;
    if (OB_FAIL(sql.assign_fmt("DELETE FROM __all_deadlock_event_history "
                                "WHERE rowid IN ("
                                "  SELECT rowid FROM __all_deadlock_event_history "
                                "  WHERE report_time < %ld "
                                "  ORDER BY report_time "
                                "  LIMIT %ld"
                                ")",
                                report_time_before, limit))) {
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
