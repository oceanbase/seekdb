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

#define USING_LOG_PREFIX COMMON
#include "ob_opt_stat_monitor_manager.h"
#include "share/ob_ex_rpc.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_shared_timer.h"
#include "share/ob_server_struct.h"
#include "sql/ob_sql_init.h"
#include "share/ob_sql_client_decorator.h"
#include "observer/dbms_scheduler/ob_dbms_sched_job_executor.h"
#include "sql/optimizer/stat/ob_dbms_stats_maintenance_window.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "query/optimizer/stat/ob_optimizer_stat_service.h"

namespace oceanbase
{
using namespace observer;
using namespace sqlclient;

namespace common
{
#define INSERT_COLUMN_USAGE "INSERT INTO __all_column_usage(table_id," \
                                                            "column_id," \
                                                            "equality_preds," \
                                                            "equijoin_preds," \
                                                            "nonequijion_preds," \
                                                            "range_preds," \
                                                            "like_preds," \
                                                            "null_preds," \
                                                            "distinct_member," \
                                                            "groupby_member," \
                                                            "flags) VALUES" \

#define ON_DUPLICATE_UPDATE "ON DUPLICATE KEY UPDATE " \
            "equality_preds = equality_preds + if (values(flags) & 1, 1, 0)," \
            "equijoin_preds = equijoin_preds + if (values(flags) & 2, 1, 0)," \
            "nonequijion_preds = nonequijion_preds + if (values(flags) & 4, 1, 0)," \
            "range_preds = range_preds + if (values(flags) & 8, 1, 0)," \
            "like_preds = like_preds + if (values(flags) & 16, 1, 0)," \
            "null_preds = null_preds + if (values(flags) & 32, 1, 0)," \
            "distinct_member = distinct_member + if (values(flags) & 64, 1, 0)," \
            "groupby_member = groupby_member + if (values(flags) & 128, 1, 0)," \
            "flags = values(flags);"

#define SELECT_FROM_COLUMN_USAGE \
  "SELECT column_id, equality_preds, equijoin_preds, nonequijion_preds, range_preds, " \
         "like_preds, null_preds, distinct_member, groupby_member " \
  "FROM oceanbase.__all_column_usage " \
  "WHERE table_id = %lu and column_id in (%s);"

#define INSERT_MONITOR_MODIFIED \
  "INSERT INTO %s (table_id, tablet_id, inserts, updates, deletes) VALUES "

#define ON_DUPLICATE_UPDATE_MONITOR_MODIFIED \
  "ON DUPLICATE KEY UPDATE " \
  "inserts = inserts + values(inserts)," \
  "updates = updates + values(updates)," \
  "deletes = deletes + values(deletes);"

#define INSERT_STALE_TABLE_STAT_SQL "INSERT /*+QUERY_TIMEOUT(60000000)*/INTO %s(table_id," \
                                                                                "partition_id," \
                                                                                "index_type," \
                                                                                "object_type," \
                                                                                "last_analyzed," \
                                                                                "sstable_row_cnt," \
                                                                                "sstable_avg_row_len," \
                                                                                "macro_blk_cnt," \
                                                                                "micro_blk_cnt," \
                                                                                "memtable_row_cnt," \
                                                                                "memtable_avg_row_len," \
                                                                                "row_cnt," \
                                                                                "avg_row_len," \
                                                                                "global_stats," \
                                                                                "user_stats," \
                                                                                "stattype_locked," \
                                                                                "stale_stats) VALUES %s" \
                                                                                "ON DUPLICATE KEY UPDATE " \
                                                                                "stale_stats = if(last_analyzed > 0, stale_stats, values(stale_stats))"

#define STALE_TABLE_STAT_MOCK_VALUE_PATTERN "(%lu, %ld, 0, 0, 0, -1, -1, 0, 0, -1, -1, 0, 0, 0, 0, 0, 1)"


void ObOptStatMonitorFlushAllTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(optstat_monitor_mgr_) && optstat_monitor_mgr_->inited_) {
    LOG_INFO("run opt stat monitor flush all task");
    
    bool write_enabled = false;
    THIS_WORKER.set_timeout_ts(FLUSH_INTERVAL / 2 + ObTimeUtility::current_time());
    if (OB_FAIL(ObShareUtil::is_server_write_enabled(write_enabled))) {
      LOG_WARN("fail to check whether server writes are enabled", KR(ret));
    } else if (!write_enabled) {
      // do nothing
    } else if (OB_FAIL(optstat_monitor_mgr_->maintain_opt_stat_monitoring_info())) {
      LOG_WARN("failed to maintain opt stat monitoring info", K(ret));
    } else {/*do nothing*/}
  }
}

void ObOptStatMonitorCheckTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(optstat_monitor_mgr_) && optstat_monitor_mgr_->inited_) {
    LOG_INFO("run opt stat monitor check task");
    THIS_WORKER.set_timeout_ts(OPT_STATS_MAINTENANCE_INTERVAL_US + ObTimeUtility::current_time());
    if (OB_FAIL(optstat_monitor_mgr_->run_periodic_maintenance_once())) {
      LOG_WARN("failed to run periodic opt stat maintenance", K(ret));
    }
  }
}

int ObOptStatMonitorManager::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("column usage manager has already been initialized.", K(ret));
  } else if (OB_FAIL(column_usage_map_.create(10000, "ColUsagHashMap", "ColUsagNode"))) {
    LOG_WARN("failed to column usage map", K(ret));
  } else if (OB_FAIL(dml_stat_map_.create(10000, "DmlStatHashMap", "DmlStatNode"))) {
    LOG_WARN("failed to create dml stat map", K(ret));
  } else {
    inited_ = true;
    mysql_proxy_ = GCTX.sql_proxy_;
    monitor_modified_epoch_.store(0, std::memory_order_release);
    completed_gather_epoch_.store(INVALID_GATHER_EPOCH, std::memory_order_release);
    async_gather_scheduled_.store(false, std::memory_order_release);
    async_gather_running_.store(false, std::memory_order_release);
    destroyed_ = false;
  }
  if (OB_FAIL(ret) && !inited_) {
    destroy();
  }
  return ret;
}

void ObOptStatMonitorManager::destroy()
{
  if (!destroyed_) {
    destroyed_ = true;
    inited_ = false;
    SpinWLockGuard guard(lock_);
    column_usage_map_.destroy();
    dml_stat_map_.destroy();
  }
}

int ObOptStatMonitorManager::flush_database_monitoring_info(sql::ObExecContext &ctx,
                                                            const bool is_flush_col_usage,
                                                            const bool is_flush_dml_stat,
                                                            const bool ignore_failed,
                                                            int64_t *flushed_dml_epoch)
{
  int ret = OB_SUCCESS;
  int64_t timeout = -1;
  if (OB_ISNULL(ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(ctx.get_my_session()));
  } else {
    obcall::ObFlushOptStatArg arg(is_flush_col_usage,
                                 is_flush_dml_stat);
    timeout = std::min(MAX_OPT_STATS_PROCESS_RPC_TIMEOUT, THIS_WORKER.get_timeout_remain());
    if (0 >= GCTX.start_service_time_) {
      //server may not serving
    } else if (0 >= timeout) {
      ret = OB_TIMEOUT;
      LOG_WARN("query timeout is reached", K(ret), K(timeout));
    } else if (OB_FAIL(ex_rpc::sync_call([&]() -> int {
      SERVER_MODULE_SCOPE {
        ObOptStatMonitorManager *monitor_mgr =
            share::server_service<ObOptStatMonitorManager>();
        if (OB_ISNULL(monitor_mgr)) {
          return OB_ERR_UNEXPECTED;
        } else if (NULL != flushed_dml_epoch
                   && arg.is_flush_col_usage_
                   && arg.is_flush_dml_stat_) {
          return monitor_mgr->maintain_opt_stat_monitoring_info(flushed_dml_epoch);
        } else {
          return monitor_mgr->update_opt_stat_monitoring_info(arg);
        }
      }
      return OB_SUCCESS;
    }))) {
      LOG_WARN("failed to flush opt stat monitoring info caused by unknow error", K(ret), K(arg));
      //ignore flush cache failed, TODO @jiangxiu.wt can aduit it and flush cache manually later.
      if (ignore_failed) {
        ret = OB_SUCCESS;
        LOG_USER_WARN(OB_ERR_DBMS_STATS_PL, "failed to flush opt stat monitoring info");
      }
    }
    LOG_TRACE("flush database monitoring info cache", K(arg));
  }
  return ret;
}

int ObOptStatMonitorManager::update_local_cache(common::ObIArray<ColumnUsageArg> &args)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  for (int64_t i = 0; OB_SUCC(ret) && i < args.count(); ++i) {
    ColumnUsageArg &arg = args.at(i);
    StatKey col_key(arg.table_id_, arg.column_id_);
    int64_t flags = 0;
    if (!ObDbmsStatsUtils::is_automatic_column_usage_table(arg.table_id_)) {
      LOG_TRACE("skip automatic column usage monitoring", K(arg));
    } else if (OB_FAIL(column_usage_map_.get_refactored(col_key, flags))) {
      if (OB_LIKELY(ret == OB_HASH_NOT_EXIST)) {
        if (OB_FAIL(column_usage_map_.set_refactored(col_key, arg.flags_))) {
          // other thread set the refactor, try update again
          if (OB_FAIL(column_usage_map_.get_refactored(col_key, flags))) {
            LOG_WARN("failed to get refactored", K(ret));
          } else if ((~flags) & arg.flags_) {
            UpdateValueAtomicOp atomic_op(arg.flags_);
            if (OB_FAIL(column_usage_map_.atomic_refactored(col_key, atomic_op))) {
              LOG_WARN("failed to atomic refactored", K(ret));
            }
          }
        }
      } else {
        LOG_WARN("failed to get refactored", K(ret));
      }
    } else if ((~flags) & arg.flags_) {
      UpdateValueAtomicOp atomic_op(arg.flags_);
      if (OB_FAIL(column_usage_map_.atomic_refactored(col_key, atomic_op))) {
        LOG_WARN("failed to atomic refactored", K(ret));
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::update_local_cache(ObOptDmlStat &dml_stat)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  StatKey key(dml_stat.table_id_, dml_stat.tablet_id_);
  ObOptDmlStat tmp_dml_stat;
  const bool has_dml = 0 != dml_stat.insert_row_count_
                       || 0 != dml_stat.update_row_count_
                       || 0 != dml_stat.delete_row_count_;
  if (!has_dml || !ObDbmsStatsUtils::is_automatic_stat_monitoring_table(dml_stat.table_id_)) {
    LOG_TRACE("skip automatic DML monitoring", K(dml_stat));
  } else if (OB_FAIL(dml_stat_map_.get_refactored(key, tmp_dml_stat))) {
    if (OB_LIKELY(ret == OB_HASH_NOT_EXIST)) {
      if (OB_FAIL(dml_stat_map_.set_refactored(key, dml_stat))) {
        // other thread set the refactor, try update again
        if (OB_FAIL(dml_stat_map_.get_refactored(key, tmp_dml_stat))) {
          LOG_WARN("failed to get refactored", K(ret));
        } else {
          UpdateValueAtomicOp atomic_op(dml_stat);
          if (OB_FAIL(dml_stat_map_.atomic_refactored(key, atomic_op))) {
            LOG_WARN("failed to atomic refactored", K(ret));
          }
        }
      }
    } else {
      LOG_WARN("failed to get refactored", K(ret));
    }
  } else {
    UpdateValueAtomicOp atomic_op(dml_stat);
    if (OB_FAIL(dml_stat_map_.atomic_refactored(key, atomic_op))) {
      LOG_WARN("failed to atomic refactored", K(ret));
    } else {/*do nothing*/}
  }
  if (OB_SUCC(ret) && has_dml
      && ObDbmsStatsUtils::is_automatic_stat_monitoring_table(dml_stat.table_id_)) {
    monitor_modified_epoch_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ret;
}

int ObOptStatMonitorManager::update_opt_stat_monitoring_info(const obcall::ObFlushOptStatArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(arg));
  } else if (arg.is_flush_col_usage_ && arg.is_flush_dml_stat_) {
    if (OB_FAIL(maintain_opt_stat_monitoring_info())) {
      LOG_WARN("failed to maintain opt stat monitoring info", K(ret));
    }
  } else if (arg.is_flush_col_usage_ && OB_FAIL(update_column_usage_info(false))) {
    LOG_WARN("failed to update column usage info", K(ret));
  } else if (arg.is_flush_dml_stat_ && OB_FAIL(update_dml_stat_info())) {
    LOG_WARN("failed to update DML statistics", K(ret));
  } else { /*do nothing*/ }
  return ret;
}

int ObOptStatMonitorManager::maintain_opt_stat_monitoring_info(int64_t *flushed_dml_epoch)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(maintenance_lock_);
  bool has_column_usage = false;
  bool has_dml_stat = false;
  {
    // Use the write lock so a producer cannot publish a cache entry between the
    // empty check and the epoch snapshot. Data arriving after this snapshot has
    // a newer epoch and is deliberately handled by the next maintenance round.
    SpinWLockGuard map_guard(lock_);
    has_column_usage = !column_usage_map_.empty();
    has_dml_stat = !dml_stat_map_.empty();
    if (NULL != flushed_dml_epoch) {
      *flushed_dml_epoch = monitor_modified_epoch_.load(std::memory_order_acquire);
    }
  }
  if (!has_column_usage && !has_dml_stat) {
    // Steady-state fast path: do not check table writability, acquire schema,
    // or execute SQL when neither monitoring cache contains data.
  } else if (has_column_usage && OB_FAIL(update_column_usage_info_())) {
    LOG_WARN("failed to update column usage info", K(ret));
  } else if (has_dml_stat && OB_FAIL(update_dml_stat_info_(flushed_dml_epoch))) {
    LOG_WARN("failed to update DML statistics", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::reconcile_persisted_dml_stat_info()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(maintenance_lock_);
  if (OB_FAIL(clean_useless_dml_stat_info())) {
    LOG_WARN("failed to reconcile persisted DML statistics", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::run_periodic_maintenance_once()
{
  int ret = OB_SUCCESS;
  bool write_enabled = false;
  bool is_async_job_available = false;
  int64_t flushed_dml_epoch = INVALID_GATHER_EPOCH;
  if (OB_FAIL(ObShareUtil::is_server_write_enabled(write_enabled))) {
    LOG_WARN("failed to check server role", K(ret));
  } else if (!write_enabled) {
    // do nothing
  } else if (OB_FAIL(maintain_opt_stat_monitoring_info(&flushed_dml_epoch))) {
    LOG_WARN("failed to maintain opt stat monitoring info", K(ret));
  } else if (!has_pending_async_gather_stats()) {
    LOG_TRACE("skip async gather stats without pending monitoring changes",
              K(flushed_dml_epoch));
  } else if (OB_FAIL(ObDbmsStatsMaintenanceWindow::check_async_gather_stats_job_available(
      mysql_proxy_, is_async_job_available))) {
    LOG_WARN("failed to check async gather stats job switch", K(ret));
  } else if (!is_async_job_available) {
    LOG_TRACE("async gather stats job is disabled or missing");
  } else if (OB_FAIL(schedule_async_gather_stats_())) {
    LOG_WARN("failed to schedule async gather stats", K(ret));
  }
  return ret;
}

bool ObOptStatMonitorManager::try_begin_async_gather_stats()
{
  bool expected = false;
  return async_gather_running_.compare_exchange_strong(
      expected, true, std::memory_order_acq_rel, std::memory_order_acquire);
}

void ObOptStatMonitorManager::finish_async_gather_stats(const int64_t target_epoch,
                                                        const bool completed)
{
  if (completed && target_epoch >= 0) {
    int64_t current = completed_gather_epoch_.load(std::memory_order_acquire);
    while (target_epoch > current
           && !completed_gather_epoch_.compare_exchange_weak(
               current, target_epoch, std::memory_order_acq_rel, std::memory_order_acquire)) {
      // retry with refreshed current value
    }
  }
  async_gather_running_.store(false, std::memory_order_release);
}

bool ObOptStatMonitorManager::has_pending_async_gather_stats() const
{
  const int64_t completed_epoch = completed_gather_epoch_.load(std::memory_order_acquire);
  const int64_t modified_epoch = monitor_modified_epoch_.load(std::memory_order_acquire);
  return INVALID_GATHER_EPOCH == completed_epoch || completed_epoch < modified_epoch;
}

int ObOptStatMonitorManager::schedule_async_gather_stats_()
{
  int ret = OB_SUCCESS;
  bool expected = false;
  if (async_gather_running_.load(std::memory_order_acquire)) {
    LOG_TRACE("async gather stats is already running");
  } else if (!async_gather_scheduled_.compare_exchange_strong(
      expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
    LOG_TRACE("async gather stats has already been scheduled");
  } else {
    ret = ex_rpc::async_call_internal([]() {
      SERVER_MODULE_SCOPE {
        ObOptStatMonitorManager *monitor_mgr =
            share::server_service<ObOptStatMonitorManager>();
        if (OB_NOT_NULL(monitor_mgr)) {
          (void)monitor_mgr->run_timer_driven_async_gather_stats_();
        }
      }
    });
    if (OB_FAIL(ret)) {
      async_gather_scheduled_.store(false, std::memory_order_release);
      LOG_WARN("failed to dispatch async gather stats", K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::run_timer_driven_async_gather_stats_()
{
  int ret = OB_SUCCESS;
  dbms_scheduler::ObDBMSSchedJobExecutor executor;
  if (OB_ISNULL(GCTX.sql_proxy_) || OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy or schema service is null", K(ret));
  } else if (OB_FAIL(executor.init(GCTX.sql_proxy_, GCTX.schema_service_))) {
    LOG_WARN("failed to initialize timer-driven job executor", K(ret));
  } else if (OB_FAIL(executor.run_timer_driven_dbms_sched_job(
      ObString("ASYNC_GATHER_STATS_JOB_PROC")))) {
    LOG_WARN("failed to execute timer-driven async gather stats", K(ret));
  }
  async_gather_scheduled_.store(false, std::memory_order_release);
  return ret;
}

int ObOptStatMonitorManager::update_column_usage_info(const bool with_check)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(maintenance_lock_);
  if (with_check) {
    bool need_flush = false;
    {
      SpinRLockGuard map_guard(lock_);
      need_flush = column_usage_map_.size() >= 10000;
    }
    if (need_flush && OB_FAIL(update_column_usage_info_())) {
      LOG_WARN("failed to update column usage info", K(ret));
    }
  } else if (OB_FAIL(update_column_usage_info_())) {
    LOG_WARN("failed to update column usage info", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::update_dml_stat_info()
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(maintenance_lock_);
  if (OB_FAIL(update_dml_stat_info_(NULL))) {
    LOG_WARN("failed to update DML statistics", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::restore_column_usage_info_(
    const ObIArray<StatKey> &col_stat_keys,
    const ObIArray<int64_t> &col_flags,
    const int64_t begin_idx)
{
  int ret = OB_SUCCESS;
  ObArray<ColumnUsageArg> restore_args;
  if (OB_UNLIKELY(begin_idx < 0
                  || begin_idx > col_stat_keys.count()
                  || col_stat_keys.count() != col_flags.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid column usage restore range", K(ret), K(begin_idx),
             K(col_stat_keys.count()), K(col_flags.count()));
  } else {
    for (int64_t i = begin_idx; OB_SUCC(ret) && i < col_stat_keys.count(); ++i) {
      ColumnUsageArg arg;
      arg.table_id_ = col_stat_keys.at(i).first;
      arg.column_id_ = col_stat_keys.at(i).second;
      arg.flags_ = col_flags.at(i);
      if (OB_FAIL(restore_args.push_back(arg))) {
        LOG_WARN("failed to build column usage restore args", K(ret), K(arg));
      }
    }
    if (OB_SUCC(ret) && !restore_args.empty()
        && OB_FAIL(update_local_cache(restore_args))) {
      LOG_WARN("failed to restore column usage cache", K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::restore_dml_stat_info_(ObIArray<ObOptDmlStat> &dml_stats,
                                                    const int64_t begin_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(begin_idx < 0 || begin_idx > dml_stats.count())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid DML statistics restore range", K(ret), K(begin_idx), K(dml_stats.count()));
  } else {
    for (int64_t i = begin_idx; OB_SUCC(ret) && i < dml_stats.count(); ++i) {
      if (OB_FAIL(update_local_cache(dml_stats.at(i)))) {
        LOG_WARN("failed to restore DML statistics cache", K(ret), K(dml_stats.at(i)));
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::update_column_usage_info_()
{
  int ret = OB_SUCCESS;
  bool is_writeable = false;
  ObArray<StatKey> col_stat_keys;
  ObArray<int64_t> col_flags;
  ObArray<StatKey> valid_col_stat_keys;
  ObArray<int64_t> valid_col_flags;
  bool filter_complete = false;
  bool restore_attempted = false;
  ObSchemaGetterGuard schema_guard;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("opt stat monitor is not inited", K(ret));
  } else if (OB_FAIL(check_table_writeable(is_writeable))) {
    LOG_WARN("failed to check tabke writeable", K(ret));
  } else if (!is_writeable) {
    // do nothing
  } else if (OB_FAIL(get_col_usage_info(false, col_stat_keys, col_flags))) {
    LOG_WARN("failed to get col usage info", K(ret));
  } else if (col_stat_keys.empty()) {
    // Empty cache is the steady-state fast path: do not acquire schema or issue SQL.
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else if (col_stat_keys.count() != col_flags.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("column usage keys and flags count do not match", K(ret),
             K(col_stat_keys.count()), K(col_flags.count()));
  } else {
    // Filter before persisting. Otherwise predicates on unsupported inner tables,
    // especially __all_column_usage itself, can keep generating column usage writes.
    for (int64_t i = 0; OB_SUCC(ret) && i < col_stat_keys.count(); ++i) {
      const uint64_t table_id = col_stat_keys.at(i).first;
      const uint64_t column_id = col_stat_keys.at(i).second;
      bool is_valid = false;
      if (!ObDbmsStatsUtils::is_automatic_column_usage_table(table_id)) {
        LOG_TRACE("skip automatic table column usage", K(table_id), K(column_id));
      } else if (OB_FAIL(ObDbmsStatsUtils::check_is_stat_table(schema_guard,
                                                        table_id,
                                                        false,
                                                        is_valid))) {
        LOG_WARN("failed to check is stat table", K(ret), K(table_id), K(column_id));
      } else if (!is_valid) {
        LOG_TRACE("skip unsupported table column usage", K(table_id), K(column_id));
      } else if (OB_FAIL(valid_col_stat_keys.push_back(col_stat_keys.at(i)))) {
        LOG_WARN("failed to push valid column usage key", K(ret), K(table_id), K(column_id));
      } else if (OB_FAIL(valid_col_flags.push_back(col_flags.at(i)))) {
        LOG_WARN("failed to push valid column usage flags", K(ret), K(table_id), K(column_id));
      }
    }
    filter_complete = OB_SUCC(ret);

    int64_t batch_begin = 0;
    ObSqlString value_sql;
    int64_t count = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < valid_col_stat_keys.count(); ++i) {
      if (OB_FAIL(get_column_usage_sql(valid_col_stat_keys.at(i),
                                       valid_col_flags.at(i),
                                       0 != count,
                                       value_sql))) {
        LOG_WARN("failed to get column usage sql", K(ret));
      } else if (UPDATE_OPT_STAT_BATCH_CNT == ++count) {
        if (OB_FAIL(exec_insert_column_usage_sql(value_sql))) {
          LOG_WARN("failed to exec insert sql", K(ret));
        } else {
          batch_begin = i + 1;
          count = 0;
          value_sql.reset();
        }
      }
    }
    if (OB_SUCC(ret) && count != 0) {
      if (OB_FAIL(exec_insert_column_usage_sql(value_sql))) {
        LOG_WARN("failed to exec insert sql", K(ret));
      } else {
        batch_begin = valid_col_stat_keys.count();
      }
    }
    if (OB_SUCCESS != ret) {
      restore_attempted = true;
      int tmp_ret = filter_complete
          ? restore_column_usage_info_(valid_col_stat_keys, valid_col_flags, batch_begin)
          : restore_column_usage_info_(col_stat_keys, col_flags, 0);
      if (OB_SUCCESS != tmp_ret) {
        LOG_ERROR("failed to restore unpersisted column usage", K(tmp_ret), K(ret));
      }
    }
  }
  if (OB_SUCCESS != ret && !restore_attempted && !col_stat_keys.empty()) {
    int tmp_ret = restore_column_usage_info_(col_stat_keys, col_flags, 0);
    if (OB_SUCCESS != tmp_ret) {
      LOG_ERROR("failed to restore extracted column usage", K(tmp_ret), K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::update_dml_stat_info_(int64_t *flushed_dml_epoch)
{
  int ret = OB_SUCCESS;
  bool is_writeable = false;
  ObArray<ObOptDmlStat> all_dml_stats;
  ObArray<ObOptDmlStat> valid_dml_stats;
  bool filter_complete = false;
  bool restore_attempted = false;
  ObSchemaGetterGuard schema_guard;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("opt stat monitor is not inited", K(ret));
  } else if (OB_FAIL(check_table_writeable(is_writeable))) {
    LOG_WARN("failed to check tabke writeable", K(ret));
  } else if (!is_writeable) {
    // do nothing
  } else if (OB_FAIL(get_dml_stats(all_dml_stats, flushed_dml_epoch))) {
    LOG_WARN("failed to swap get dml stat", K(ret));
  } else if (all_dml_stats.empty()) {
    // Empty cache is the steady-state fast path: do not acquire schema or issue SQL.
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else {
    // Filter before persisting. Otherwise DML from unsupported inner tables,
    // especially __all_monitor_modified itself, can keep generating monitor writes.
    for (int64_t i = 0; OB_SUCC(ret) && i < all_dml_stats.count(); ++i) {
      bool is_valid = false;
      if (!ObDbmsStatsUtils::is_automatic_stat_monitoring_table(
              all_dml_stats.at(i).table_id_)) {
        LOG_TRACE("skip automatic table dml stat", K(all_dml_stats.at(i)));
      } else if (OB_FAIL(ObDbmsStatsUtils::check_is_stat_table(schema_guard,
                                                        all_dml_stats.at(i).table_id_,
                                                        false,
                                                        is_valid))) {
        LOG_WARN("failed to check is stat table", K(ret), K(all_dml_stats.at(i)));
      } else if (!is_valid) {
        LOG_TRACE("skip unsupported table dml stat", K(all_dml_stats.at(i)));
      } else if (OB_FAIL(valid_dml_stats.push_back(all_dml_stats.at(i)))) {
        LOG_WARN("failed to push back valid dml stat", K(ret), K(all_dml_stats.at(i)));
      }
    }
    filter_complete = OB_SUCC(ret);

    int64_t batch_begin = 0;
    if (OB_SUCC(ret) && !valid_dml_stats.empty()) {
      ObSqlString value_sql;
      int64_t count = 0;

      for (int64_t i = 0; OB_SUCC(ret) && i < valid_dml_stats.count(); ++i) {
        if (OB_FAIL(get_dml_stat_sql(valid_dml_stats.at(i), 0 != count, value_sql))) {
          LOG_WARN("failed to get dml stat sql", K(ret));
        } else if (UPDATE_OPT_STAT_BATCH_CNT == ++count) {
          if (OB_FAIL(exec_insert_monitor_modified_sql(value_sql))) {
            LOG_WARN("failed to exec insert sql", K(ret));
          } else {
            batch_begin = i + 1;
            count = 0;
            value_sql.reset();
          }
        }
      }
      if (OB_SUCC(ret) && count != 0) {
        if (OB_FAIL(exec_insert_monitor_modified_sql(value_sql))) {
          LOG_WARN("failed to exec insert sql", K(ret));
        } else {
          batch_begin = valid_dml_stats.count();
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(clean_useless_dml_stat_info())) {
          LOG_WARN("failed to clean useless dml stat info", K(ret));
        } else if (OB_FAIL(check_opt_stats_expired(valid_dml_stats))) {
          LOG_WARN("failed to check opt stats expired", K(ret));
        } else {/*do nohting*/}
      }
    }
    if (OB_SUCCESS != ret) {
      restore_attempted = true;
      int tmp_ret = filter_complete
          ? restore_dml_stat_info_(valid_dml_stats, batch_begin)
          : restore_dml_stat_info_(all_dml_stats, 0);
      if (OB_SUCCESS != tmp_ret) {
        LOG_ERROR("failed to restore unpersisted DML statistics", K(tmp_ret), K(ret));
      }
    }
  }
  if (OB_SUCCESS != ret && !restore_attempted && !all_dml_stats.empty()) {
    int tmp_ret = restore_dml_stat_info_(all_dml_stats, 0);
    if (OB_SUCCESS != tmp_ret) {
      LOG_ERROR("failed to restore extracted DML statistics", K(tmp_ret), K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::get_column_usage_sql(const StatKey &col_key,
                                                  const int64_t flags,
                                                  const bool need_add_comma,
                                                  ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  share::ObDMLSqlSplicer dml_splicer;
  uint64_t table_id = col_key.first;
  
  uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(table_id);
  int64_t equality_preds = flags & EQUALITY_PREDS ? 1 : 0;
  int64_t equijoin_preds = flags & EQUIJOIN_PREDS ? 1 : 0;
  int64_t nonequijion_preds = flags & NONEQUIJOIN_PREDS ? 1 : 0;
  int64_t range_preds = flags & RANGE_PREDS ? 1 : 0;
  int64_t like_preds = flags & LIKE_PREDS ? 1 : 0;
  int64_t null_preds = flags & NULL_PREDS ? 1 : 0;
  int64_t distinct_member = flags & DISTINCT_MEMBER ? 1 : 0;
  int64_t groupby_member = flags & GROUPBY_MEMBER ? 1 : 0;
  if (OB_FAIL(dml_splicer.add_pk_column("table_id", pure_table_id)) ||
      OB_FAIL(dml_splicer.add_pk_column("column_id", col_key.second)) ||
      OB_FAIL(dml_splicer.add_column("equality_preds", equality_preds)) ||
      OB_FAIL(dml_splicer.add_column("equijoin_preds", equijoin_preds)) ||
      OB_FAIL(dml_splicer.add_column("nonequijion_preds", nonequijion_preds)) ||
      OB_FAIL(dml_splicer.add_column("range_preds", range_preds)) ||
      OB_FAIL(dml_splicer.add_column("like_preds", like_preds)) ||
      OB_FAIL(dml_splicer.add_column("null_preds", null_preds)) ||
      OB_FAIL(dml_splicer.add_column("distinct_member", distinct_member)) ||
      OB_FAIL(dml_splicer.add_column("groupby_member", groupby_member)) ||
      OB_FAIL(dml_splicer.add_column("flags", flags))) {
    LOG_WARN("failed to add dml splicer column", K(ret));
  } else if (OB_FAIL(sql_string.append_fmt("%s", need_add_comma ? ",(" : "("))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (OB_FAIL(dml_splicer.splice_values(sql_string))) {
    LOG_WARN("failed to get sql string", K(ret));
  } else if (OB_FAIL(sql_string.append(")"))) {
    LOG_WARN("failed to append string", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::exec_insert_column_usage_sql(ObSqlString &values_sql)
{
  int ret = OB_SUCCESS;
  ObSqlString insert_sql;
  int64_t affected_rows = 0;
  if (OB_FAIL(insert_sql.append(INSERT_COLUMN_USAGE))) {
    LOG_WARN("failed to append sql", K(ret));
  } else if (OB_FAIL(insert_sql.append(values_sql.ptr(), values_sql.length()))) {
    LOG_WARN("failed to append format", K(ret));
  } else if (OB_FAIL(insert_sql.append(ON_DUPLICATE_UPDATE))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (OB_FAIL(mysql_proxy_->write(insert_sql.ptr(), affected_rows))) {
    LOG_WARN("fail to exec sql", K(insert_sql), K(ret));
  } else if (OB_FAIL(mysql_proxy_->write("commit;", affected_rows))) {
    LOG_WARN("fail to exec sql", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::get_column_usage_from_table(ObExecContext &ctx,
                                                         ObIArray<ObColumnStatParam *> &column_params,
                                                         uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  if (OB_FAIL(construct_get_column_usage_sql(column_params, table_id, select_sql))) {
    LOG_WARN("failed to construct sql", K(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *ctx.get_sql_proxy();
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
        LOG_WARN("failed to execute sql", K(ret), K(select_sql));
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else if (OB_FAIL(client_result->next())) {
        LOG_WARN("failed to get next row", K(ret));
      }
      while (OB_SUCC(ret)) {
        ObColumnStatParam *target_param = NULL;
        int64_t flag = 0;
        for (int64_t i = 0; OB_SUCC(ret) && i < info_count + 1; ++i) {
          ObObj val;
          if (OB_FAIL(client_result->get_obj(i, val))) {
            LOG_WARN("failed to get object", K(ret));
          } else if (!val.is_int()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected value type", K(ret), K(i));
          } else if (i == 0) {
            // column_id
            int64_t column_id = val.get_int();
            bool find = false;
            for (int64_t j = 0; !find && j < column_params.count(); ++j) {
              if (column_params.at(j)->column_id_ == column_id) {
                target_param = column_params.at(j);
                find = true;
              }
            }
          } else if (val.get_int() > 0) {
            flag |= 1 << (i - 1);
          }
        }
        if (OB_SUCC(ret)) {
          if (OB_ISNULL(target_param)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("get unexpected null", K(ret));
          } else {
            target_param->column_usage_flag_ = flag;
            ret = client_result->next();
          }
        }
      }
      if (OB_LIKELY(ret == OB_ITER_END)) {
        ret = OB_SUCCESS;
      }
      if (NULL != client_result) {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::construct_get_column_usage_sql(ObIArray<ObColumnStatParam *> &column_params,
                                                            const uint64_t table_id,
                                                            ObSqlString &select_sql)
{
  int ret = OB_SUCCESS;
  ObSqlString col_ids;
  
  uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(table_id);
  for (int64_t i = 0; OB_SUCC(ret) && i < column_params.count(); ++i) {
    ObColumnStatParam *column_param = column_params.at(i);
    if (OB_ISNULL(column_param)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret));
    } else if (OB_FAIL(col_ids.append_fmt("%s%lu",
                                          i == 0 ? "" : ", ",
                                          column_param->column_id_))) {
      LOG_WARN("failed to append format", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(select_sql.append_fmt(SELECT_FROM_COLUMN_USAGE,
                                      pure_table_id,
                                      col_ids.ptr()))) {
      LOG_WARN("failed to append fmt", K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::check_table_writeable(bool &is_writeable)
{
  int ret = OB_SUCCESS;
  is_writeable = true;
  bool write_enabled = false;
  if (OB_FAIL(ObShareUtil::is_server_write_enabled(write_enabled))) {
    LOG_WARN("fail to check whether server writes are enabled", KR(ret));
  } else if (OB_UNLIKELY(!write_enabled)) {
    is_writeable = false;
  }
  return ret;
}

int ObOptStatMonitorManager::UpdateValueAtomicOp::operator() (common::hash::HashMapPair<StatKey, int64_t> &entry)
{
  entry.second |= flags_;
  return OB_SUCCESS;
}

int ObOptStatMonitorManager::UpdateValueAtomicOp::operator() (common::hash::HashMapPair<StatKey, ObOptDmlStat> &entry)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(entry.second.table_id_ != dml_stat_.table_id_ ||
                  entry.second.tablet_id_ != dml_stat_.tablet_id_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(entry.second), K(dml_stat_));
  } else {
    entry.second.insert_row_count_ += dml_stat_.insert_row_count_;
    entry.second.update_row_count_ += dml_stat_.update_row_count_;
    entry.second.delete_row_count_ += dml_stat_.delete_row_count_;
  }
  return ret;
}

int ObOptStatMonitorManager::exec_insert_monitor_modified_sql(ObSqlString &values_sql,
                                                              ObISQLConnection *conn)
{
  int ret = OB_SUCCESS;
  ObSqlString insert_sql;
  int64_t affected_rows = 0;
  if (OB_FAIL(insert_sql.append_fmt(INSERT_MONITOR_MODIFIED, share::OB_ALL_MONITOR_MODIFIED_TNAME))) {
    LOG_WARN("failed to append sql", K(ret));
  } else if (OB_FAIL(insert_sql.append(values_sql.ptr(), values_sql.length()))) {
    LOG_WARN("failed to append format", K(ret));
  } else if (OB_FAIL(insert_sql.append(ON_DUPLICATE_UPDATE_MONITOR_MODIFIED))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (nullptr != conn &&
             OB_FAIL(conn->execute_write(insert_sql.ptr(), affected_rows))) {
    LOG_WARN("fail to exec sql", K(insert_sql), K(ret));
  } else if (nullptr == conn &&
             OB_FAIL(mysql_proxy_->write(insert_sql.ptr(), affected_rows))) {
    LOG_WARN("fail to exec sql", K(insert_sql), K(ret));
  } else {
    LOG_TRACE("succeed to exec insert monitor modified sql", K(values_sql));
  }
  return ret;
}

int ObOptStatMonitorManager::get_dml_stat_sql(const ObOptDmlStat &dml_stat,
                                              const bool need_add_comma,
                                              ObSqlString &sql_string)
{
  int ret = OB_SUCCESS;
  share::ObDMLSqlSplicer dml_splicer;
  uint64_t table_id = dml_stat.table_id_;
  
  uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(table_id);
  if (OB_FAIL(dml_splicer.add_pk_column("table_id", pure_table_id)) ||
      OB_FAIL(dml_splicer.add_pk_column("tablet_id", dml_stat.tablet_id_)) ||
      OB_FAIL(dml_splicer.add_column("inserts", dml_stat.insert_row_count_)) ||
      OB_FAIL(dml_splicer.add_column("updates", dml_stat.update_row_count_)) ||
      OB_FAIL(dml_splicer.add_column("deletes", dml_stat.delete_row_count_))) {
    LOG_WARN("failed to add dml splicer column", K(ret));
  } else if (OB_FAIL(sql_string.append_fmt("%s", need_add_comma ? ",(" : "("))) {
    LOG_WARN("failed to append string", K(ret));
  } else if (OB_FAIL(dml_splicer.splice_values(sql_string))) {
    LOG_WARN("failed to get sql string", K(ret));
  } else if (OB_FAIL(sql_string.append(")"))) {
    LOG_WARN("failed to append string", K(ret));
  }
  return ret;
}

int ObOptStatMonitorManager::generate_opt_stat_monitoring_info_rows(ObIOptDmlStatConsumer &consumer)
{
  int ret = OB_SUCCESS;
  SpinRLockGuard guard(lock_);
  if (OB_FAIL(dml_stat_map_.foreach_refactored(consumer))) {
    LOG_WARN("fail to generate opt stat monitoring info rows", K(ret));
  } else {/*do nothing*/}
  return ret;
}

int ObOptStatMonitorManager::clean_useless_dml_stat_info()
{
  int ret = OB_SUCCESS;
  ObSqlString delete_table_sql;
  ObSqlString delete_part_sql;
  int64_t affected_rows1 = 0;
  int64_t affected_rows2 = 0;
  const char* all_table_name = NULL;
  if (OB_FAIL(ObSchemaUtils::get_all_table_name(all_table_name))) {
    LOG_WARN("failed to get all table name", K(ret));
  } else if (OB_FAIL(delete_table_sql.append_fmt("DELETE FROM %s m WHERE (NOT EXISTS (SELECT 1 " \
            "FROM %s t, %s db WHERE t.database_id = db.database_id "\
            "AND t.table_id = m.table_id AND db.database_name != '__recyclebin')) "\
            "AND table_id > %ld;",
            share::OB_ALL_MONITOR_MODIFIED_TNAME, all_table_name, share::OB_ALL_DATABASE_TNAME,
            OB_MAX_INNER_TABLE_ID))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else if (OB_FAIL(delete_part_sql.append_fmt("DELETE /*+leading(view3, m1) use_nl(view3, m1)*/FROM %s m1 WHERE (table_id, tablet_id) IN ( "\
            "SELECT /*+leading(m, view1, view2, t, db) use_hash(m,view1) use_hash((m,view1),view2) use_nl((m,view1,view2),t), use_nl((m,view1,view2,t),db)*/ "\
            "m.table_id, m.tablet_id FROM %s m, %s t, %s db WHERE t.table_id = m.table_id AND t.part_level > 0 "\
            "AND t.database_id = db.database_id AND db.database_name != '__recyclebin' "\
            "AND NOT EXISTS (SELECT 1 FROM %s p WHERE  p.table_id = m.table_id AND p.tablet_id = m.tablet_id) "\
            "AND NOT EXISTS (SELECT 1 FROM %s sp WHERE  sp.table_id = m.table_id AND sp.tablet_id = m.tablet_id)) "\
            "AND table_id > %ld;",
            share::OB_ALL_MONITOR_MODIFIED_TNAME, share::OB_ALL_MONITOR_MODIFIED_TNAME,
            all_table_name, share::OB_ALL_DATABASE_TNAME, share::OB_ALL_PART_TNAME,
            share::OB_ALL_SUB_PART_TNAME, OB_MAX_INNER_TABLE_ID))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else if (OB_FAIL(mysql_proxy_->write(delete_table_sql.ptr(), affected_rows1))) {
    LOG_WARN("failed to execute sql", K(ret), K(delete_table_sql));
  } else if (OB_FAIL(mysql_proxy_->write(delete_part_sql.ptr(), affected_rows2))) {
    LOG_WARN("failed to execute sql", K(ret), K(delete_part_sql));
  } else {
    LOG_TRACE("succeed to clean useless monitor modified_data", K(delete_table_sql),
                                                                K(affected_rows1), K(delete_part_sql),
                                                                K(affected_rows2));
  }
  return ret;
}

int ObOptStatMonitorManager::server_module_init(ObOptStatMonitorManager* &optstat_monitor_mgr)
{
  int ret = OB_SUCCESS;
  
  if (OB_LIKELY(nullptr != optstat_monitor_mgr)) {
    if (OB_FAIL(optstat_monitor_mgr->init())) {
      LOG_WARN("failed to init event list", K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::server_module_start(ObOptStatMonitorManager* &optstat_monitor_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_LIKELY(nullptr != optstat_monitor_mgr)) {
    int migrate_ret = ObDbmsStatsMaintenanceWindow::ensure_async_gather_stats_job_timer_driven(
        optstat_monitor_mgr->mysql_proxy_);
    if (OB_SUCCESS != migrate_ret) {
      // The generic scheduler also recognizes the legacy job name, so a
      // transient migration failure must not prevent server startup.
      LOG_WARN("failed to migrate async gather stats job flag", K(migrate_ret));
    }
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::ObISharedTimer>()->schedule(
        optstat_monitor_mgr->get_flush_all_task(),
        ObOptStatMonitorFlushAllTask::FLUSH_INTERVAL, true))) {
      LOG_WARN("failed to scheduler flush all task", K(ret));
    } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::share::ObISharedTimer>()->schedule(
        optstat_monitor_mgr->get_check_task(),
        OPT_STATS_MAINTENANCE_INTERVAL_US, true))) {
      LOG_WARN("failed to scheduler check task", K(ret));
    } else {
      optstat_monitor_mgr->get_flush_all_task().disable_timeout_check();
      optstat_monitor_mgr->get_flush_all_task().optstat_monitor_mgr_ = optstat_monitor_mgr;
      optstat_monitor_mgr->get_check_task().disable_timeout_check();
      optstat_monitor_mgr->get_check_task().optstat_monitor_mgr_ = optstat_monitor_mgr;
    }
  }
  return ret;
}

void ObOptStatMonitorManager::server_module_stop(ObOptStatMonitorManager* &optstat_monitor_mgr)
{
  if (OB_LIKELY(nullptr != optstat_monitor_mgr)) {
    ::oceanbase::share::server_service<::oceanbase::share::ObISharedTimer>()->cancel_task(optstat_monitor_mgr->get_flush_all_task());
    ::oceanbase::share::server_service<::oceanbase::share::ObISharedTimer>()->cancel_task(optstat_monitor_mgr->get_check_task());
  }
}

void ObOptStatMonitorManager::server_module_wait(ObOptStatMonitorManager* &optstat_monitor_mgr)
{
  if (OB_LIKELY(nullptr != optstat_monitor_mgr)) {
    ::oceanbase::share::server_service<::oceanbase::share::ObISharedTimer>()->wait_task(optstat_monitor_mgr->get_flush_all_task());
    ::oceanbase::share::server_service<::oceanbase::share::ObISharedTimer>()->wait_task(optstat_monitor_mgr->get_check_task());
  }
}

int ObOptStatMonitorManager::get_col_usage_info(const bool with_check,
                                                ObIArray<StatKey> &col_stat_keys,
                                                ObIArray<int64_t> &col_flags)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);
  if (column_usage_map_.empty() ||
      (with_check && column_usage_map_.size() < 10000)) {
    //do nothing
  } else {
    for (auto iter = column_usage_map_.begin(); OB_SUCC(ret) && iter != column_usage_map_.end(); ++iter) {
      if (OB_FAIL(col_stat_keys.push_back(iter->first)) ||
          OB_FAIL(col_flags.push_back(iter->second))) {
        LOG_WARN("failed to push back", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      column_usage_map_.reuse();
    } else {
      col_stat_keys.reset();
      col_flags.reset();
    }
  }
  return ret;
}

int ObOptStatMonitorManager::get_dml_stats(ObIArray<ObOptDmlStat> &dml_stats,
                                          int64_t *captured_epoch)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);
  if (NULL != captured_epoch) {
    *captured_epoch = monitor_modified_epoch_.load(std::memory_order_acquire);
  }
  if (dml_stat_map_.empty()) {
    //do nothing
  } else {
    for (auto iter = dml_stat_map_.begin(); OB_SUCC(ret) && iter != dml_stat_map_.end(); ++iter) {
      if (OB_FAIL(dml_stats.push_back(iter->second))) {
        LOG_WARN("failed to get dml stat sql", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      dml_stat_map_.reuse();
    } else {
      dml_stats.reset();
    }
  }
  return ret;
}

int ObOptStatMonitorManager::check_opt_stats_expired(ObIArray<ObOptDmlStat> &dml_stats)
{
  int ret = OB_SUCCESS;
  if (!dml_stats.empty()) {
    ObSEArray<OptStatExpiredTableInfo, 4> stale_infos;
    int64_t begin_ts = ObTimeUtility::current_time();
    int64_t global_async_stale_max_table_size = DEFAULT_ASYNC_STALE_MAX_TABLE_SIZE;
    if (OB_FAIL(get_async_stale_max_table_size(
                                               OB_INVALID_ID,
                                               global_async_stale_max_table_size))) {
      LOG_WARN("failed to get async stale max table size", K(ret));
    } else if (OB_UNLIKELY(global_async_stale_max_table_size <= 0)) {
      LOG_WARN("skip to check opt stats expired", K(global_async_stale_max_table_size));
    } else if (OB_FAIL(get_opt_stats_expired_table_info(dml_stats, stale_infos))) {
      LOG_WARN("failed to get opt stats expired table info", K(ret));
    } else {
      const int64_t MIN_ASYNC_GATHER_TABLE_ROW_CNT = 500;
      for (int64_t i = 0; OB_SUCC(ret) && i < stale_infos.count(); ++i) {
        if (stale_infos.at(i).inserts_ <= MIN_ASYNC_GATHER_TABLE_ROW_CNT) {
          //do nothing
        } else if (OB_FAIL(mark_the_opt_stat_expired(stale_infos.at(i)))) {
          LOG_WARN("failed to mark the opt stat expired", K(ret));
        }
      }
    }
    if (ObTimeUtility::current_time() - begin_ts > OPT_STATS_MAINTENANCE_INTERVAL_US) {
      LOG_WARN("check opt stats expired cost too much time", K(begin_ts), K(ObTimeUtility::current_time() - begin_ts), K(dml_stats));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::get_opt_stats_expired_table_info(ObIArray<ObOptDmlStat> &dml_stats,
                                                              ObIArray<OptStatExpiredTableInfo> &stale_infos)
{
  int ret = OB_SUCCESS;
  int64_t begin_idx = 0;
  while (OB_SUCC(ret) && begin_idx < dml_stats.count()) {
    ObSqlString where_list;
    int64_t end_idx = std::min(begin_idx + MAX_PROCESS_BATCH_TABLET_CNT, dml_stats.count());
    
    if (OB_FAIL(gen_tablet_list(dml_stats, begin_idx, end_idx, where_list))) {
      LOG_WARN("failed to gen tablet list", K(ret));
    } else if (where_list.empty()) {
      //do nothing
    } else if (OB_FAIL(do_get_opt_stats_expired_table_info(where_list, stale_infos))) {
      LOG_WARN("failed to do get opt stats expired table info", K(ret));
    }
    begin_idx = end_idx;
  }
  return ret;
}

int ObOptStatMonitorManager::gen_tablet_list(const ObIArray<ObOptDmlStat> &dml_stats,
                                             const int64_t begin_idx,
                                             const int64_t end_idx,
                                             ObSqlString &tablet_list)
{
  int ret = OB_SUCCESS;
  tablet_list.reset();
  int64_t begin_ts = ObTimeUtility::current_time();
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(begin_idx < 0 || end_idx < 0 ||
                  begin_idx >= end_idx || end_idx > dml_stats.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(begin_idx), K(end_idx), K(dml_stats));
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else {
    bool is_first = true;
    for (int64_t i = begin_idx; OB_SUCC(ret) && i < end_idx; ++i) {
      bool is_valid = false;
      if (OB_FAIL(ObDbmsStatsUtils::check_is_stat_table(schema_guard,
                                                        dml_stats.at(i).table_id_,
                                                        false,
                                                        is_valid))) {
        LOG_WARN("failed to check is stat table", K(ret));
      } else if (is_valid) {
        
        uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(dml_stats.at(i).table_id_);
        if (OB_FAIL(tablet_list.append_fmt("%s(%lu, %ld)", is_first ? "(" : " ,",
                                                                pure_table_id,
                                                                dml_stats.at(i).tablet_id_))) {
          LOG_WARN("failed to append sql", K(ret));
        } else {
          is_first = false;
        }
      }
    }
    if (OB_SUCC(ret) && !is_first) {
      if (OB_FAIL(tablet_list.append(")"))) {
        LOG_WARN("failed to append", K(ret));
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::do_get_opt_stats_expired_table_info(const ObSqlString &where_str,
                                                                 ObIArray<OptStatExpiredTableInfo> &stale_infos)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  if (OB_FAIL(select_sql.append_fmt("SELECT m.table_id, m.tablet_id, m.inserts  "\
                                     "FROM      %s m " \
                                     "WHERE (CASE WHEN m.last_inserts = 0 THEN 11 "\
                                                "ELSE m.inserts * 1.0 / m.last_inserts END) >  10.0 "\
                                              "AND (m.table_id, m.tablet_id) in %s",
          share::OB_ALL_MONITOR_MODIFIED_TNAME,
          where_str.ptr()))) {
    LOG_WARN("failed to append fmt", K(ret));
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
      sqlclient::ObMySQLResult *client_result = NULL;
      auto &sql_client_retry_weak = *mysql_proxy_;
      if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
        LOG_WARN("failed to execute sql", K(ret), K(select_sql));
      } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to execute sql", K(ret));
      } else {
        while (OB_SUCC(ret) && OB_SUCC(client_result->next())) {
          int64_t idx1 = 0;
          int64_t idx2 = 1;
          int64_t idx3 = 2;
          ObObj obj1;
          ObObj obj2;
          ObObj obj3;
          int64_t table_id = -1;
          int64_t tablet_id = 0;
          int64_t inserts = 0;
          if (OB_FAIL(client_result->get_obj(idx1, obj1)) ||
              OB_FAIL(client_result->get_obj(idx2, obj2)) ||
              OB_FAIL(client_result->get_obj(idx3, obj3))) {
            LOG_WARN("failed to get object", K(ret));
          } else if (OB_FAIL(obj1.get_int(table_id)) ||
                     OB_FAIL(obj2.get_int(tablet_id)) ||
                     OB_FAIL(obj3.get_int(inserts))) {
            LOG_WARN("failed to get int", K(ret), K(obj1), K(obj2), K(inserts));
          } else {
            bool is_found = false;
            for (int64_t i = 0; !is_found && OB_SUCC(ret) && i < stale_infos.count(); ++i) {
              if (table_id == stale_infos.at(i).table_id_) {
                is_found = true;
                if (OB_FAIL(stale_infos.at(i).tablet_ids_.push_back(tablet_id))) {
                  LOG_WARN("failed to push back", K(ret));
                } else {
                  stale_infos.at(i).inserts_ += inserts;
                }
              }
            }
            if (OB_SUCC(ret) && !is_found) {
              OptStatExpiredTableInfo stale_info;
              
              stale_info.table_id_ = table_id;
              stale_info.inserts_ = inserts;
              if (OB_FAIL(stale_info.tablet_ids_.push_back(tablet_id))) {
                LOG_WARN("failed to push back", K(ret));
              } else if (OB_FAIL(stale_infos.push_back(stale_info))) {
                LOG_WARN("failed to push back", K(ret));
              }
            }
          }
        }
        ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
      }
      int tmp_ret = OB_SUCCESS;
      if (NULL != client_result) {
        if (OB_SUCCESS != (tmp_ret = client_result->close())) {
          LOG_WARN("close result set failed", K(ret), K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
    LOG_TRACE("do get opt stats expired table info", K(select_sql), K(stale_infos));
  }
  return ret;
}

int ObOptStatMonitorManager::mark_the_opt_stat_expired(const OptStatExpiredTableInfo &expired_table_info)
{
  int ret = OB_SUCCESS;
  ObSEArray<PartInfo, 4> part_infos;
  ObSEArray<PartInfo, 4> subpart_infos;
  ObSEArray<int64_t, 4> partition_ids;
  ObSEArray<int64_t, 4> expired_partition_ids;
  share::schema::ObPartitionLevel part_level = share::schema::ObPartitionLevel::PARTITION_LEVEL_MAX;
  ObSEArray<ObOptTableStat, 4> table_stats;
  ObSEArray<ObOptTableStat, 4> expired_table_stats;
  ObSEArray<ObOptTableStat, 4> no_table_stats;
  ObArenaAllocator allocator("OptStatMonitor", OB_MALLOC_NORMAL_BLOCK_SIZE);
  int64_t begin_ts = ObTimeUtility::current_time();
  int64_t async_stale_max_table_size = DEFAULT_ASYNC_STALE_MAX_TABLE_SIZE;
  if (OB_FAIL(get_expired_table_part_info(allocator, expired_table_info, part_level, part_infos, subpart_infos))) {
    LOG_WARN("failed to get expired table part info", K(ret));
  } else if (part_level == share::schema::ObPartitionLevel::PARTITION_LEVEL_MAX) {
    //do nothing
  } else if (OB_FAIL(get_need_check_opt_stat_partition_ids(expired_table_info,
                                                           part_infos,
                                                           subpart_infos,
                                                           partition_ids))) {
    LOG_WARN("failed to get need check opt stat partition ids", K(ret));
  } else if (OB_FAIL(ObOptStatManager::get_instance().get_table_stat(expired_table_info.table_id_,
                                                                     partition_ids,
                                                                     table_stats))) {
    LOG_WARN("failed to get table stat", K(ret));
  } else if (OB_FAIL(get_async_stale_max_table_size(
                                                    expired_table_info.table_id_,
                                                    async_stale_max_table_size))) {
    LOG_WARN("failed to get async stale max table size", K(ret));
  } else if (OB_UNLIKELY(async_stale_max_table_size <= 0)) {
    LOG_INFO("skip to mark the opt stat expired", K(async_stale_max_table_size));
  } else if (OB_FAIL(get_need_mark_opt_stats_expired(table_stats,
                                                     expired_table_info,
                                                     async_stale_max_table_size,
                                                     begin_ts,
                                                     part_level,
                                                     part_infos,
                                                     subpart_infos,
                                                     expired_table_stats,
                                                     no_table_stats))) {
    LOG_WARN("failed to get need mark opt stats expired", K(ret));
  } else if (OB_FAIL(do_mark_the_opt_stat_expired(
                                                  expired_table_stats,
                                                  expired_partition_ids))) {
    LOG_WARN("failed to do mark the opt stat expired", K(ret));
  } else if (OB_FAIL(do_mark_the_opt_stat_missing(
                                                  no_table_stats))) {
    LOG_WARN("failed to do mark the opt stat missing", K(ret));
  } else {
    obcall::ObUpdateStatCacheArg stat_arg;
    
    
    stat_arg.table_id_ = expired_table_info.table_id_;
    stat_arg.no_invalidate_ = true;
    if (OB_FAIL(append(stat_arg.partition_ids_, expired_partition_ids))) {
      LOG_WARN("failed to append", K(ret));
    } else if (OB_FAIL(pl::ObDbmsStats::update_stat_cache(stat_arg))) {
      LOG_WARN("failed to update stat cache", K(ret));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::get_expired_table_part_info(ObIAllocator &allocator,
                                                         const OptStatExpiredTableInfo &expired_table_info,
                                                         share::schema::ObPartitionLevel &part_level,
                                                         ObIArray<PartInfo> &part_infos,
                                                         ObIArray<PartInfo> &subpart_infos)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObTableSchema *table_schema = NULL;
  part_level = share::schema::ObPartitionLevel::PARTITION_LEVEL_MAX;
  part_infos.reset();
  subpart_infos.reset();
  if (OB_ISNULL(GCTX.schema_service_) || OB_UNLIKELY(!expired_table_info.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(expired_table_info), K(GCTX.schema_service_));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
    LOG_WARN("get runtime schema guard failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema(
                                                   expired_table_info.table_id_,
                                                   table_schema))) {
    LOG_WARN("get table schema failed", K(ret), K(expired_table_info.table_id_));
  } else if (OB_ISNULL(table_schema)) {//maybe table isn't exists.
    //do nothing
  } else if (OB_FAIL(pl::ObDbmsStats::get_table_part_infos(table_schema, allocator, part_infos, subpart_infos))) {
    LOG_WARN("failed to get table part infos", K(ret));
  } else {
    part_level = table_schema->get_part_level();
  }
  return ret;
}

int ObOptStatMonitorManager::get_need_check_opt_stat_partition_ids(const OptStatExpiredTableInfo &expired_table_info,
                                                                   ObIArray<PartInfo> &part_infos,
                                                                   ObIArray<PartInfo> &subpart_infos,
                                                                   ObIArray<int64_t> &partition_ids)
{
  int ret = OB_SUCCESS;
  //non partition table
  if (part_infos.empty() && subpart_infos.empty()) {
    if (OB_FAIL(partition_ids.push_back(expired_table_info.table_id_))) {
      LOG_WARN("failed to push back", K(ret));
    }
  //partition table, global stat partition id is -1
  } else if (OB_FAIL(partition_ids.push_back(-1))) {
    LOG_WARN("failed to push back", K(ret));
  } else if (!part_infos.empty() && subpart_infos.empty()) {//part table
    if (expired_table_info.tablet_ids_.count() == part_infos.count()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < part_infos.count(); ++i) {
        if (OB_FAIL(partition_ids.push_back(part_infos.at(i).part_id_))) {
          LOG_WARN("failed to push back", K(ret));
        }
      }
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < expired_table_info.tablet_ids_.count(); ++i) {
        bool found_it = false;
        for (int64_t j = 0; OB_SUCC(ret) && !found_it && j < part_infos.count(); ++j) {
          if (expired_table_info.tablet_ids_.at(i) == static_cast<int64_t>(part_infos.at(j).tablet_id_.id())) {
            if (OB_FAIL(partition_ids.push_back(part_infos.at(j).part_id_))) {
              LOG_WARN("failed to push back", K(ret));
            } else {
              found_it = true;
            }
          }
        }
      }
    }
  } else if (!part_infos.empty() && !subpart_infos.empty()) {//subpart table
    hash::ObHashMap<int64_t, bool> partition_ids_map;
    if (expired_table_info.tablet_ids_.count() == subpart_infos.count()) {
      for (int64_t i = 0; OB_SUCC(ret) && i < part_infos.count(); ++i) {
        if (OB_FAIL(partition_ids.push_back(part_infos.at(i).part_id_))) {
          LOG_WARN("failed to push back", K(ret));
        }
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < subpart_infos.count(); ++i) {
        if (OB_FAIL(partition_ids.push_back(subpart_infos.at(i).part_id_))) {
          LOG_WARN("failed to push back", K(ret));
        }
      }
    } else if (OB_FAIL(partition_ids_map.create(part_infos.count(), "PartIdsMap", "PartIdsMapNode"))) {
      LOG_WARN("fail to create hash map", K(ret), K(expired_table_info.tablet_ids_.count()));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < expired_table_info.tablet_ids_.count(); ++i) {
        bool found_it = false;
        for (int64_t j = 0; OB_SUCC(ret) && !found_it && j < subpart_infos.count(); ++j) {
          if (expired_table_info.tablet_ids_.at(i) == static_cast<int64_t>(subpart_infos.at(j).tablet_id_.id())) {
            bool tmp_var = false;
            if (OB_FAIL(partition_ids.push_back(subpart_infos.at(j).part_id_))) {
              LOG_WARN("failed to push back", K(ret));
            } else {
              found_it = true;
              if (OB_FAIL(partition_ids_map.get_refactored(subpart_infos.at(j).first_part_id_, tmp_var))) {
                if (OB_HASH_NOT_EXIST == ret) {
                  ret = OB_SUCCESS;
                  if (OB_FAIL(partition_ids.push_back(subpart_infos.at(j).first_part_id_))) {
                    LOG_WARN("failed to push back", K(ret));
                  } else if (OB_FAIL(partition_ids_map.set_refactored(subpart_infos.at(j).first_part_id_, true))) {
                    LOG_WARN("failed to set refactored", K(ret));
                  } else {/*do nothing*/}
                } else {
                  LOG_WARN("failed to get refactored", K(ret));
                }
              }
            }
          }
        }
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret));
  }
  LOG_TRACE("get need check opt stat partition ids", K(expired_table_info), K(part_infos),
                                                     K(subpart_infos), K(partition_ids));
  return ret;
}

int ObOptStatMonitorManager::get_need_mark_opt_stats_expired(const ObIArray<ObOptTableStat> &table_stats,
                                                             const OptStatExpiredTableInfo &expired_table_info,
                                                             const int64_t async_stale_max_table_size,
                                                             const int64_t begin_ts,
                                                             const share::schema::ObPartitionLevel &part_level,
                                                             const ObIArray<PartInfo> &part_infos,
                                                             const ObIArray<PartInfo> &subpart_infos,
                                                             ObIArray<ObOptTableStat> &expired_table_stats,
                                                             ObIArray<ObOptTableStat> &no_table_stats)
{
  int ret = OB_SUCCESS;
  bool have_table_stats = false;
  for (int64_t i = 0; OB_SUCC(ret) && i < table_stats.count(); ++i) {
    bool is_stat_expired = false;
    have_table_stats |= table_stats.at(i).get_last_analyzed() > 0;
    if (table_stats.at(i).get_last_analyzed() <= 0) {
      if (!table_stats.at(i).is_stat_expired()) {
        if (OB_FAIL(no_table_stats.push_back(table_stats.at(i)))) {
          LOG_WARN("failed to push back", K(ret));
        }
      }
    } else if (table_stats.at(i).is_stat_expired() ||
               table_stats.at(i).get_last_analyzed() >= begin_ts ||
               table_stats.at(i).get_row_count() > async_stale_max_table_size) {
      //do nothing
    } else if (part_level == share::schema::ObPartitionLevel::PARTITION_LEVEL_ZERO) {
      is_stat_expired = true;
    } else if (part_level == share::schema::ObPartitionLevel::PARTITION_LEVEL_ONE) {
      if (table_stats.count() == part_infos.count() + 1 ||
          table_stats.at(i).get_object_type() == StatLevel::PARTITION_LEVEL) {
        is_stat_expired = true;
      } else if (table_stats.at(i).get_object_type() == StatLevel::TABLE_LEVEL) {
        ObSEArray<uint64_t, 4> tablet_ids;
        if (OB_FAIL(check_table_stat_expired_by_dml_info(
                                                         expired_table_info.table_id_,
                                                         tablet_ids,
                                                         is_stat_expired))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpcted error", K(ret));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(table_stats.at(i)));
      }
    } else if (part_level == share::schema::ObPartitionLevel::PARTITION_LEVEL_TWO) {
      if (table_stats.count() == part_infos.count() + subpart_infos.count() + 1 ||
          table_stats.at(i).get_object_type() == StatLevel::SUBPARTITION_LEVEL) {
        is_stat_expired = true;
      } else if (table_stats.at(i).get_object_type() == StatLevel::PARTITION_LEVEL) {
        ObSEArray<uint64_t, 4> tablet_ids;
        bool is_all_subpart_expired = true;
        for (int64_t j = 0; OB_SUCC(ret) && j < subpart_infos.count(); ++j) {
          if (table_stats.at(i).get_partition_id() == subpart_infos.at(j).first_part_id_) {
            if (OB_FAIL(tablet_ids.push_back(subpart_infos.at(j).tablet_id_.id()))) {
              LOG_WARN("failed to push back", K(ret));
            } else if (is_all_subpart_expired) {
              bool found_it = false;
              for (int64_t k = 0; !found_it && k < table_stats.count(); ++k) {
                if (table_stats.at(i).get_partition_id() == subpart_infos.at(j).part_id_) {
                  found_it = true;
                  is_all_subpart_expired &= table_stats.at(i).get_last_analyzed() > 0;
                }
              }
              is_all_subpart_expired &= found_it;
            }
          }
        }
        if (OB_SUCC(ret)) {
          if (is_all_subpart_expired) {
            is_stat_expired = true;
          } else if (OB_FAIL(check_table_stat_expired_by_dml_info(
                                                                  expired_table_info.table_id_,
                                                                  tablet_ids,
                                                                  is_stat_expired))) {
            LOG_WARN("failed to check table stat expired by dml info", K(ret));
          }
        }
      } else if (table_stats.at(i).get_object_type() == StatLevel::TABLE_LEVEL) {
        ObSEArray<uint64_t, 4> tablet_ids;
        if (OB_FAIL(check_table_stat_expired_by_dml_info(
                                                         expired_table_info.table_id_,
                                                         tablet_ids,
                                                         is_stat_expired))) {
          LOG_WARN("failed to check table stat expired by dml info", K(ret));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected error", K(ret), K(table_stats.at(i)));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected error", K(ret), K(table_stats.at(i)));
    }
    if (OB_SUCC(ret) && is_stat_expired) {
      if (OB_FAIL(expired_table_stats.push_back(table_stats.at(i)))) {
        LOG_WARN("failed to push back", K(ret));
      }
    }
    if (OB_SUCC(ret) && have_table_stats) {
      no_table_stats.reset();
    }
  }
  LOG_TRACE("get need mark opt stats expired", K(expired_table_stats), K(no_table_stats));
  return ret;
}

int ObOptStatMonitorManager::check_table_stat_expired_by_dml_info(const uint64_t table_id,
                                                                  const ObIArray<uint64_t> &tablet_ids,
                                                                  bool &is_stat_expired)
{
  int ret = OB_SUCCESS;
  ObSqlString tablet_list;
  is_stat_expired = false;
  if (OB_FAIL(gen_tablet_list(tablet_ids, tablet_list))) {
    LOG_WARN("failed to gen tablet list", K(ret));
  } else {
    
    uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(table_id);
    ObSqlString select_sql;
    if (OB_FAIL(select_sql.append_fmt("SELECT 1 "\
                                       "FROM  (SELECT  table_id,"\
                                                       "sum(inserts-deletes) AS row_cnt,"\
                                                      "sum(inserts+updates+deletes) AS total_modified_cnt,"\
                                                      "sum(last_inserts+last_updates+last_deletes) AS last_modified_cnt "\
                                                  "from     %s "\
                                                  "WHERE    table_id = %lu %s%s "\
                                                  "GROUP BY table_id) m "\
                                        "WHERE     (CASE WHEN row_cnt = 0 THEN 10.1 "\
                                                    "ELSE (total_modified_cnt  * 1.0) / last_modified_cnt END) > 10.1 "\
                                        "AND row_cnt > 0;",
          share::OB_ALL_MONITOR_MODIFIED_TNAME,
          pure_table_id,
          tablet_list.empty() ? " " : " AND tablet_id in ",
          tablet_list.empty() ? " " : tablet_list.ptr()
          ))) {
      LOG_WARN("failed to append fmt", K(ret));
    } else {
      LOG_TRACE("check table stat expired by dml info", K(select_sql));
      SMART_VAR(ObMySQLProxy::MySQLResult, proxy_result) {
        sqlclient::ObMySQLResult *client_result = NULL;
        auto &sql_client_retry_weak = *mysql_proxy_;
        if (OB_FAIL(sql_client_retry_weak.read(proxy_result, select_sql.ptr()))) {
          LOG_WARN("failed to execute sql", K(ret), K(select_sql));
        } else if (OB_ISNULL(client_result = proxy_result.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("failed to execute sql", K(ret));
        } else {
          while (OB_SUCC(ret) && !is_stat_expired && OB_SUCC(client_result->next())) {
            is_stat_expired = true;
          }
          ret = OB_ITER_END == ret ? OB_SUCCESS : ret;
        }
        int tmp_ret = OB_SUCCESS;
        if (NULL != client_result) {
          if (OB_SUCCESS != (tmp_ret = client_result->close())) {
            LOG_WARN("close result set failed", K(ret), K(tmp_ret));
            ret = COVER_SUCC(tmp_ret);
          }
        }
      }
    }
    LOG_TRACE("check_table_stat_expired_by_dml_info end", K(is_stat_expired));
  }
  return ret;
}

int ObOptStatMonitorManager::gen_tablet_list(const ObIArray<uint64_t> &tablet_ids,
                                             ObSqlString &tablet_list)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
    char prefix = (i == 0 ? '(' : ' ');
    char suffix = (i == tablet_ids.count() - 1 ? ')' : ',');
    if (OB_FAIL(tablet_list.append_fmt("%c%lu%c", prefix, tablet_ids.at(i), suffix))) {
      LOG_WARN("failed to append sql", K(ret));
    } else {/*do nothing*/}
  }
  return ret;
}

int ObOptStatMonitorManager::do_mark_the_opt_stat_missing(const ObIArray<ObOptTableStat> &no_table_stats)
{
  int ret = OB_SUCCESS;
  if (!no_table_stats.empty()) {
    int64_t begin_idx = 0;
    ObMySQLTransaction trans;
    if (OB_ISNULL(mysql_proxy_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get unexpected null", K(ret), K(mysql_proxy_));
    } else if (OB_FAIL(trans.start(mysql_proxy_))) {
      LOG_WARN("fail to start transaction", K(ret));
    } else {
      while (OB_SUCC(ret) && begin_idx < no_table_stats.count()) {
        ObSqlString insert_sql;
        ObSqlString values_list;
        int64_t affected_rows = 0;
        int64_t end_idx = std::min(begin_idx + MAX_PROCESS_BATCH_TABLET_CNT, no_table_stats.count());
        if (OB_FAIL(gen_values_list( no_table_stats, begin_idx, end_idx, values_list))) {
          LOG_WARN("failed to gen values list", K(ret));
        } else if (OB_UNLIKELY(values_list.empty())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get unexpected null", K(ret), K(values_list));
        } else if (OB_FAIL(insert_sql.append_fmt(INSERT_STALE_TABLE_STAT_SQL,
                                                 share::OB_ALL_TABLE_STAT_TNAME,
                                                 values_list.ptr()))) {
        } else if (OB_FAIL(trans.write(insert_sql.ptr(), affected_rows))) {
          LOG_WARN("fail to exec sql", K(insert_sql), K(ret));
        } else {
          begin_idx = end_idx;
          LOG_TRACE("Succeed to do mark the opt stat expired", K(insert_sql), K(no_table_stats), K(affected_rows));
        }
      }
      //end gather trans
      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("fail to commit transaction", K(ret));
        }
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
          LOG_WARN("fail to roll back transaction", K(tmp_ret));
        }
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::do_mark_the_opt_stat_expired(const ObIArray<ObOptTableStat> &expired_table_stats,
                                                          ObIArray<int64_t> &expired_partition_ids)
{
  int ret = OB_SUCCESS;
  int64_t begin_idx = 0;
  if (OB_ISNULL(mysql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret), K(mysql_proxy_));
  }
  while (OB_SUCC(ret) && begin_idx < expired_table_stats.count()) {
    ObSqlString update_sql;
    ObSqlString same_part_analyzed_list;
    ObSqlString diff_part_analyzed_list;
    int64_t affected_rows = 0;
    int64_t end_idx = std::min(begin_idx + MAX_PROCESS_BATCH_TABLET_CNT, expired_table_stats.count());
    
    uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(expired_table_stats.at(begin_idx).get_table_id());
    if (OB_FAIL(gen_part_analyzed_list(expired_table_stats, begin_idx, end_idx,
                                       same_part_analyzed_list,
                                       diff_part_analyzed_list,
                                       expired_partition_ids))) {
      LOG_WARN("failed to gen part analyzed list", K(ret));
    } else if (OB_FAIL(update_sql.append_fmt("update /*+QUERY_TIMEOUT(60000000)*/%s set stale_stats = 1 where table_id = %lu and %s",
                                              share::OB_ALL_TABLE_STAT_TNAME,
                                              pure_table_id,
                                              !same_part_analyzed_list.empty() ? same_part_analyzed_list.ptr() : diff_part_analyzed_list.ptr()))) {
    } else if (OB_FAIL(mysql_proxy_->write(update_sql.ptr(), affected_rows))) {
      LOG_WARN("fail to exec sql", K(update_sql), K(ret));
    } else {
      begin_idx = end_idx;
      LOG_TRACE("Succeed to do mark the opt stat expired", K(update_sql), K(expired_table_stats), K(affected_rows));
    }
  }
  return ret;
}

int ObOptStatMonitorManager::gen_part_analyzed_list(const ObIArray<ObOptTableStat> &expired_table_stats,
                                                    const int64_t begin_idx,
                                                    const int64_t end_idx,
                                                    ObSqlString &same_part_analyzed_list,
                                                    ObSqlString &diff_part_analyzed_list,
                                                    ObIArray<int64_t> &expired_partition_ids)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(begin_idx < 0 || end_idx < 0 ||
                  begin_idx >= end_idx || end_idx > expired_table_stats.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(begin_idx), K(end_idx), K(expired_table_stats));
  } else {
    int64_t last_analyzed = -1;
    for (int64_t i = begin_idx; OB_SUCC(ret) && i < end_idx; ++i) {
      char suffix = (i == end_idx - 1 ? ')' : ',');
      if (OB_FAIL(expired_partition_ids.push_back(expired_table_stats.at(i).get_partition_id()))) {
        LOG_WARN("failed to push back", K(ret));
      } else if (OB_FAIL(diff_part_analyzed_list.append_fmt("%s(%ld,usec_to_time(%ld))%c", i == begin_idx ? "(partition_id, last_analyzed) in (" : " ",
                                                                                           expired_table_stats.at(i).get_partition_id(),
                                                                                           expired_table_stats.at(i).get_last_analyzed(),
                                                                                           suffix))) {
        LOG_WARN("failed to append sql", K(ret));
      } else if (i == begin_idx || last_analyzed == expired_table_stats.at(i).get_last_analyzed()) {
        last_analyzed = expired_table_stats.at(i).get_last_analyzed();
        if (OB_FAIL(same_part_analyzed_list.append_fmt("%s%ld%c", i == begin_idx ? "partition_id in (" : " ",
                                                                  expired_table_stats.at(i).get_partition_id(),
                                                                  suffix))) {
          LOG_WARN("failed to append sql", K(ret));
        } else if (i == end_idx - 1) {
          if (OB_FAIL(same_part_analyzed_list.append_fmt(" AND last_analyzed = usec_to_time(%ld)", last_analyzed))) {
            LOG_WARN("failed to append sql", K(ret));
          } else {
            diff_part_analyzed_list.reset();
          }
        }
      } else {
        last_analyzed = -1;
        same_part_analyzed_list.reset();
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::gen_values_list(const ObIArray<ObOptTableStat> &no_table_stats,
                                             const int64_t begin_idx,
                                             const int64_t end_idx,
                                             ObSqlString &values_list)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(begin_idx < 0 || end_idx < 0 ||
                  begin_idx >= end_idx || end_idx > no_table_stats.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(begin_idx), K(end_idx), K(no_table_stats));
  } else {
    for (int64_t i = begin_idx; OB_SUCC(ret) && i < end_idx; ++i) {
      ObSqlString value;
      
      uint64_t pure_table_id = share::schema::ObSchemaUtils::get_extract_schema_id(no_table_stats.at(i).get_table_id());
      if (OB_FAIL(value.append_fmt(STALE_TABLE_STAT_MOCK_VALUE_PATTERN,
                                   pure_table_id,
                                   no_table_stats.at(i).get_partition_id()))) {
        LOG_WARN("failed to append fmt", K(ret));
      } else if (OB_FAIL(values_list.append_fmt("%s%s", i == begin_idx ? " " : ", ", value.ptr()))) {
        LOG_WARN("failed to push back", K(ret));
      }
    }
  }
  return ret;
}

int ObOptStatMonitorManager::get_async_stale_max_table_size(const uint64_t table_id,
                                                            int64_t &async_stale_max_table_size)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator tmp_alloc("OptStatPrefs", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObAsyncStaleMaxTableSizePrefs prefs;
  ObString opt_name(prefs.get_stat_pref_name());
  ObObj result;
  ObObj dest_obj;
  ObCastCtx cast_ctx(&tmp_alloc, NULL, CM_NONE, ObCharset::get_system_collation());
  async_stale_max_table_size = DEFAULT_ASYNC_MAX_SCAN_ROWCOUNT;
  if (OB_FAIL(ObDbmsStatsPreferences::get_prefs(mysql_proxy_, tmp_alloc,
                                                table_id, opt_name, result))) {
    LOG_WARN("failed to get prefs", K(ret));
  } else if (result.is_null()) {
    //do nothing
  } else if (OB_FAIL(ObObjCaster::to_type(ObNumberType, cast_ctx, result, dest_obj))) {
    LOG_WARN("failed to type", K(ret), K(result));
  } else if (OB_FAIL(dest_obj.get_number().extract_valid_int64_with_trunc(async_stale_max_table_size))) {
    LOG_WARN("failed to extract valid int64 with trunc", K(ret), K(result));
  } else if (async_stale_max_table_size < 0) {
    ret = OB_ERR_DBMS_STATS_PL;
    LOG_WARN("Illegal async stale max table size", K(ret), K(async_stale_max_table_size));
  }
  LOG_TRACE("get_async_stale_max_table_size", K(async_stale_max_table_size), K(result));
  return ret;
}

}
}

namespace oceanbase
{
namespace query
{

int ObOptimizerStatService::report_dml_stat(
    uint64_t table_id,
    int64_t tablet_id,
    int64_t inserted_rows,
    int64_t updated_rows,
    int64_t deleted_rows)
{
  int ret = OB_SUCCESS;
  common::ObOptDmlStat stat;
  stat.table_id_ = table_id;
  stat.tablet_id_ = tablet_id;
  stat.insert_row_count_ = inserted_rows;
  stat.update_row_count_ = updated_rows;
  stat.delete_row_count_ = deleted_rows;
  common::ObOptStatMonitorManager *monitor =
      share::server_service<common::ObOptStatMonitorManager>();
  if (OB_ISNULL(monitor)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("optimizer stat monitor manager is null", K(ret));
  } else if (OB_FAIL(monitor->update_local_cache(stat))) {
  }
  return ret;
}

} // namespace query
} // namespace oceanbase
