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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/optimizer/stat/ob_dbms_stats_executor.h"
#include "sql/pl/sys_package/ob_dbms_stats.h"
#include "sql/engine/cmd/ob_analyze_executor.h"
#include "sql/optimizer/stat/ob_dbms_stats_lock_unlock.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
#include "share/ob_share_util.h"

//#define COMPUTE_FREQUENCY_HISTOGRAM
//   "SELECT /*+NO_USE_PX*/ col, sum(val) over (order by col rows between unbounded preceding and current row) "
//   "FROM (SELECT %.*s as col, count(*) as val FROM %s WHERE %.*s IS NOT NULL GROUP BY %.*s) temp;"
//
//#define COMPUTE_TOP_FREQUENCY_HISTOGRAM
//   "SELECT /*+NO_USE_PX*/ col, sum(val) over (order by col rows between unbounded preceding and current row) "
//   "FROM (SELECT * FROM (SELECT %.*s as col, count(*) as val FROM %s WHERE %.*s IS NOT NULL GROUP BY %.*s ORDER BY val LIMIT %ld) temp) temp2;"
//
//#define COMPUTE_HEIGHT_BASED_HISTOGRAM
//   "SELECT /*+NO_USE_PX*/ endpoint_value, sum(endpoint_num) over (order by endpoint_value rows between unbounded preceding and current row) "
//   "FROM (SELECT endpoint_value, count(*) as endpoint_num "
//   "FROM (SELECT MAX(col) as endpoint_value "
//   "FROM (SELECT %.*s as col, ntile(%ld) over (order by %.*s) as bucket FROM %s WHERE %.*s IS NOT NULL) temp GROUP BY bucket) temp2 group by endpoint_value) temp3;"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObAnalyzeExecutor::execute(ObExecContext &ctx, ObAnalyzeStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTableStatParam,1> params;
  ObSQLSessionInfo *session = ctx.get_my_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(session));
  } else {
    
    bool is_primary = true;
    if (OB_FAIL(ObShareUtil::check_if_server_role_is_primary(is_primary))) {
    } else if (OB_UNLIKELY(!is_primary)) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "analyze table on a standby database");
    }
  }
  if (FAILEDx(ObDbmsStatsUtils::implicit_commit_before_gather_stats(ctx))) {
    LOG_WARN("failed to implicit commit before gather stats", K(ret));
  } else if (OB_FAIL(ObDbmsStatsUtils::cancel_async_gather_stats(ctx))) {
  } else if (OB_FAIL(stmt.fill_table_stat_params(ctx, params))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
      if (OB_FAIL(pl::ObDbmsStats::process_not_size_manual_column(ctx, params.at(i)))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (stmt.is_delete_histogram()) {
        bool cascade_columns = true;
        bool cascade_indexes = true;
        if (OB_UNLIKELY(params.count() != 1)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected error", K(ret), K(params));
        } else {
          ObArenaAllocator tmp_alloc("DeleteStats", OB_MALLOC_NORMAL_BLOCK_SIZE);
          params.at(0).allocator_ = &tmp_alloc;//use the temp allocator to free memory after delete stats.
          if (OB_FAIL(ObDbmsStatsLockUnlock::check_stat_locked(ctx, params.at(0)))) {
          } else if (OB_FAIL(ObDbmsStatsExecutor::delete_table_stats(ctx, params.at(0), cascade_columns))) {
          } else if (OB_FAIL(pl::ObDbmsStats::update_stat_cache(params.at(0)))) {
          } else if (cascade_indexes && params.at(0).part_name_.empty()) {
            if (OB_FAIL(pl::ObDbmsStats::delete_table_index_stats(ctx, params.at(0)))) {
            } else {/*do nothing*/}
          }
        }
      } else {
        int64_t task_cnt = params.count();
        int64_t start_time = ObTimeUtility::current_time();
        ObOptStatTaskInfo task_info;
        if (OB_FAIL(pl::ObDbmsStats::init_gather_task_info(ctx, ObOptStatGatherType::MANUAL_GATHER,
                                                          start_time, task_cnt, task_info))) {
        } else {
          int64_t i = 0;
          for (; OB_SUCC(ret) && i < params.count(); ++i) {
            ObTableStatParam &param = params.at(i);
            ObArenaAllocator tmp_alloc("OptStatGather", OB_MALLOC_NORMAL_BLOCK_SIZE);
            param.allocator_ = &tmp_alloc;//use the temp allocator to free memory after gather stats.
            start_time = ObTimeUtility::current_time();
            ObOptStatGatherStat gather_stat(task_info);
            ObOptStatGatherStatList::instance().push(gather_stat);
            ObOptStatGatherAudit audit(tmp_alloc);
            ObOptStatRunningMonitor running_monitor(ctx.get_allocator(), start_time, param.allocator_->used(), gather_stat, audit);
            if (OB_FAIL(running_monitor.add_monitor_info(ObOptStatRunningPhase::GATHER_PREPARE))) {
            } else if (OB_FAIL(running_monitor.add_table_info(param))) {
            } else if (OB_FAIL(ObDbmsStatsLockUnlock::check_stat_locked(ctx, param))) {
            } else if (OB_FAIL(ObOptStatMonitorManager::flush_database_monitoring_info(ctx, false, true))) {
            } else if (OB_FAIL(ObDbmsStatsExecutor::gather_table_stats(ctx, param, running_monitor))) {
            } else if (OB_FAIL(pl::ObDbmsStats::update_stat_cache(param))) {
            } else {
            }
            if (ret == OB_SUCCESS || ret == OB_TIMEOUT) {
              int tmp_ret = ret;
              if (OB_FAIL(running_monitor.flush_gather_audit())) {
              } else {
                ret = tmp_ret;
              }
            }
            running_monitor.set_monitor_result(ret, ObTimeUtility::current_time(), param.allocator_->used());
            ObOptStatGatherStatList::instance().remove(gather_stat);
            task_info.completed_table_count_ ++;
          }
          task_info.task_end_time_ = ObTimeUtility::current_time();
          task_info.ret_code_ = ret;
          task_info.failed_count_ = ret == OB_SUCCESS ? 0 : params.count() - i + 1;
        }
      }
    }
  }
  return ret;
}

} // end of SQL
} // end of OceanBase
