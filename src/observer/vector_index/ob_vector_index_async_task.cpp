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

#define USING_LOG_PREFIX SERVER

#include "data_plane/vector/ob_i_vector_index_runtime.h"
#include "observer/vector_index/ob_vector_index_async_task.h"
#include "observer/vector_index/ob_vector_index_async_task_util.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "storage/ls/ob_ls.h"

using namespace oceanbase::share;
using namespace oceanbase::common;

namespace oceanbase
{

namespace share
{

// ---------------------------------- ObVectorIndexHistoryTask -----------------------------------------//

void ObVectorIndexHistoryTask::runTimerTask()
{
  ObCurTraceId::init(GCONF.self_addr_);
  do_work(); // ignore error
}

void ObVectorIndexHistoryTask::do_work()
{
  ObCurTraceId::init(GCONF.self_addr_);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("vector index history task is not init", KR(ret));
  } else if (is_paused_) {
    // timer paused or not leader, do nothing
  } else if (!ObVecIndexAsyncTaskUtil::check_runtime_ready()) { // skip
  } else if (OB_FAIL(move_task_to_history_table())) {
    LOG_WARN("fail to move task to history table", K(ret));
  } else if (OB_FAIL(clear_history_task())) {
    LOG_WARN("fail to clear history task", K(ret));
  }
}

int ObVectorIndexHistoryTask::clear_history_task()
{
  int ret = OB_SUCCESS;

  const int64_t batch_size = OB_VEC_INDEX_TASK_DEL_COUNT_PER_TASK; // 4096
  int64_t affect_rows = 0;
  ObMySQLTransaction trans;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(sql_proxy_));
  } else if (is_paused_) {
    ret = OB_EAGAIN;
    FLOG_INFO("exit timer task once cuz leader switch", KR(ret));
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
    LOG_WARN("fail start transaction", KR(ret));
  } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::clear_history_expire_task_record(batch_size, trans, affect_rows))) {
    LOG_WARN("fail to clear expired vector index task history", KR(ret));
  } else {
    LOG_DEBUG("success to clear_history_task", K(ret), K(affect_rows));
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_ERROR("fail to commit trans", KR(ret), K(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  return ret;
}


// batch move all
int ObVectorIndexHistoryTask::move_task_to_history_table()
{
  int ret = OB_SUCCESS;
  int64_t batch_size = OB_VEC_INDEX_TASK_MOVE_BATCH_SIZE;
  int64_t move_rows = batch_size;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(sql_proxy_));
  } else {
    while (OB_SUCC(ret) && move_rows != 0) {
      ObMySQLTransaction trans;
      if (is_paused_) {
        ret = OB_EAGAIN;
        FLOG_INFO("exit timer task once cuz leader switch", K(ret));
      } else if (OB_FAIL(trans.start(sql_proxy_))) {
        LOG_WARN("fail start transaction", KR(ret));
      } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::move_task_to_history_table(batch_size, trans, move_rows))) {
        LOG_WARN("fail to move task to history table", KR(ret));
      }
      if (trans.is_started()) {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
          LOG_ERROR("fail to commit trans", KR(ret), K(tmp_ret));
          ret = OB_SUCC(ret) ? tmp_ret : ret;
        }
      }
    }
  }
  LOG_DEBUG("do move task to history table", K(ret));
  return ret;
}

int ObVectorIndexHistoryTask::init(common::ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("vector index history task initialized twice", KR(ret));
  } else {
    sql_proxy_ = &sql_proxy;
    disable_timeout_check();
    is_inited_ = true;
  }
  return ret;
}

void ObVectorIndexHistoryTask::resume()
{
  is_paused_ = false;
}

void ObVectorIndexHistoryTask::pause()
{
  is_paused_ = true;
}

// ---------------------------------- ObVecAsyncTaskScheduler -----------------------------------------//

int ObVecAsyncTaskScheduler::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("vector index scheduler initialized twice", KR(ret));
  } else if (OB_FAIL(timer_.init(
      "VecIdxManager", common::ObMemAttr("VecIdxManager")))) {
    LOG_WARN("fail to init timer", KR(ret));
  } else if (OB_FAIL(vec_history_task_.init(sql_proxy))) { // History table cleanup
    LOG_WARN("fail to init clear history task");
  } else {
    is_inited_ = true;

    LOG_INFO("vector index scheduler is initialized");
  }
  return ret;
}

int ObVecAsyncTaskScheduler::start()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("vector index scheduler begins to start");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(timer_.schedule(vec_history_task_, VEC_INDEX_CLEAR_TASK_PERIOD, true))) {
    LOG_WARN("fail to start vector index clear history task", KR(ret));
  }
  FLOG_INFO("vector index scheduler start finished", KR(ret));

  return ret;
}

void ObVecAsyncTaskScheduler::wait()
{
  FLOG_INFO("wait for vector index async task scheduler");
  if (timer_.inited()) {
    timer_.wait();
  }
  FLOG_INFO("finished waiting for vector index async task scheduler");
}

void ObVecAsyncTaskScheduler::stop()
{
  FLOG_INFO("stop vector index async task scheduler");
  if (timer_.inited()) {
    timer_.stop();
  }
  FLOG_INFO("finished stopping vector index async task scheduler");
}

void ObVecAsyncTaskScheduler::destroy()
{
  FLOG_INFO("destroy vector index async task scheduler");
  timer_.destroy();
  FLOG_INFO("finished destroying vector index async task scheduler");
}

void ObVecAsyncTaskScheduler::resume()
{
  vec_history_task_.resume();
}

void ObVecAsyncTaskScheduler::pause()
{
  vec_history_task_.pause();
}

// ---------------------------------- ObVecAsyncTaskExector -----------------------------------------//

int ObVecAsyncTaskExector::check_and_set_thread_pool()
{
  int ret = OB_SUCCESS;
  ObPluginVectorIndexMgr *index_mgr = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("vector index load task not inited", K(ret));
  } else if (OB_ISNULL(vector_index_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret));
  } else if (OB_FAIL(get_index_mgr(index_mgr))) {
    LOG_WARN("fail to get index ls mgr", K(ret));
  } else {
    ObVecIndexAsyncTaskHandler &thread_pool_handle = vector_index_service_->get_vec_async_task_handle();
    if (0 == index_mgr->get_complete_adapter_map().size()) { // no vector index exists, skip
    } else {
      common::ObSpinLockGuard init_guard(thread_pool_handle.lock_);
      if (thread_pool_handle.is_inited()) { // no need to init twice
      } else if (OB_FAIL(thread_pool_handle.init())) {
        LOG_WARN("fail to init vec async task handle", K(ret));
      } else if (OB_FAIL(thread_pool_handle.start())) {
        LOG_WARN("fail to start thread pool", K(ret));
      }
    }
  }
  return ret;
}

int ObVecAsyncTaskExector::load_task(uint64_t &task_trace_base_num)
{
  int ret = OB_SUCCESS;
  ObPluginVectorIndexMgr *index_mgr = nullptr;
  ObArray<ObVecIndexAsyncTaskCtx*> task_ctx_array;
  bool is_active_time = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("vector async task not init", KR(ret));
  // vector_index_optimize_duty_time only constrains AUTO-triggered per-tablet
  // HNSW optimize task creation here. MANUAL tasks use ObVecTaskManager.
  } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::in_active_time(is_active_time))) {
    LOG_WARN("fail to get active time", KR(ret));
  } else if (!is_active_time) {
    LOG_INFO("skip auto-create per-tablet hnsw optimize tasks, not in active time");
  } else if (OB_FAIL(get_index_mgr(index_mgr))) {
    LOG_WARN("fail to get index manager", K(ret));
  } else {
    ObVecIndexAsyncTaskOption &task_opt = index_mgr->get_async_task_opt();
    ObIAllocator *allocator = task_opt.get_allocator();
    const int64_t current_task_cnt = ObVecIndexAsyncTaskUtil::get_processing_task_cnt(task_opt);

    RWLock::RLockGuard lock_guard(index_mgr->get_adapter_map_lock());
    FOREACH_X(iter, index_mgr->get_complete_adapter_map(),
        OB_SUCC(ret) && (task_ctx_array.count() + current_task_cnt <= MAX_ASYNC_TASK_PROCESSING_COUNT)) {
      ObTabletID tablet_id = iter->first;
      ObPluginVectorIndexAdaptor *adapter = iter->second;
      if (OB_ISNULL(adapter)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected nullptr", K(ret));
      } else if (adapter->is_need_async_optimal()) {
        int64_t new_task_id = OB_INVALID_ID;
        int64_t index_table_id = OB_INVALID_ID;
        bool inc_new_task = false;
        common::ObCurTraceId::TraceId new_trace_id;

        char *task_ctx_buf = static_cast<char *>(allocator->alloc(sizeof(ObVecIndexAsyncTaskCtx)));
        ObVecIndexAsyncTaskCtx* task_ctx = nullptr;
        if (OB_ISNULL(task_ctx_buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("async task ctx is null", K(ret));
        } else if (FALSE_IT(task_ctx = new(task_ctx_buf) ObVecIndexAsyncTaskCtx())) {
        } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_task_id(new_task_id))) {
          LOG_WARN("fail to fetch new task id", K(ret));
        } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::get_table_id_from_adapter(
                       adapter, tablet_id, index_table_id))) {
          LOG_WARN("fail to get table id from adapter", K(ret), K(tablet_id));
        } else if (OB_INVALID_ID == index_table_id) {
          LOG_DEBUG("index table id is invalid, skip", K(ret));
        } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_trace_id(
                       ++task_trace_base_num, allocator, new_trace_id))) {
          LOG_WARN("fail to fetch new trace id", K(ret), K(tablet_id));
        } else {
          LOG_DEBUG("start load task", K(ret), K(tablet_id), K(task_trace_base_num));
          task_ctx->ls_ = ls_;
          task_ctx->task_status_.tablet_id_ = tablet_id.id();
          task_ctx->task_status_.table_id_ = index_table_id;
          task_ctx->task_status_.task_id_ = new_task_id;
          task_ctx->task_status_.task_type_ = ObVecIndexAsyncTaskType::OB_VECTOR_ASYNC_INDEX_OPTINAL;
          task_ctx->task_status_.trigger_type_ = ObVecIndexAsyncTaskTriggerType::OB_VEC_TRIGGER_AUTO;
          task_ctx->task_status_.status_ = ObVecIndexAsyncTaskStatus::OB_VECTOR_ASYNC_TASK_PREPARE;
          task_ctx->task_status_.trace_id_ = new_trace_id;
          task_ctx->task_status_.target_scn_.convert_from_ts(ObTimeUtility::current_time());

          if (OB_FAIL(task_opt.add_task_ctx(tablet_id, task_ctx, inc_new_task))) {
            LOG_WARN("fail to add task ctx", K(ret));
          } else if (inc_new_task && OB_FAIL(task_ctx_array.push_back(task_ctx))) {
            LOG_WARN("fail to push back task status", K(ret), K(task_ctx));
          }
        }
        if (OB_FAIL(ret) || !inc_new_task) {
          if (OB_NOT_NULL(task_ctx)) {
            task_ctx->~ObVecIndexAsyncTaskCtx();
            allocator->free(task_ctx);
            task_ctx = nullptr;
          }
        }
      }
    }
    LOG_INFO("finish load async task", K(ret), K(task_ctx_array.count()), K(current_task_cnt));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(insert_new_task(task_ctx_array))) {
    LOG_WARN("fail to insert new tasks", K(ret));
  }
  if (OB_FAIL(ret) && !task_ctx_array.empty()) {
    if (OB_FAIL(clear_task_ctxs(index_mgr->get_async_task_opt(), task_ctx_array))) {
      LOG_WARN("fail to clear task ctx", K(ret));
    }
  }
  return ret;
}

// ---------------------------------- ObVecTaskManager -----------------------------------------//

int ObVecTaskManager::process_task()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(create_task())) {
    LOG_WARN("fail to create task", K(ret));
  }
  while (OB_SUCC(ret) && !task_ids_.empty()) {
    if (OB_FAIL(check_task_status())) {
      LOG_WARN("fail to check task status", K(ret));
    } else {
      ob_usleep(1LL * 1000 * 1000);
    }

    if (REACH_TIME_INTERVAL(10 * 60L * 1000000)) {
      LOG_INFO("vector index task not finished", K(ret), K(task_ids_));
    }
  }
  return ret;
}

int ObVecTaskManager::create_task()
{
  int ret = OB_SUCCESS;
  uint64_t trace_base_num = 0;
  ObSEArray<ObTabletID, 1> tablet_ids;
  ObArray<ObVecIndexAsyncTaskCtx*> task_ctx_array;
  ObArenaAllocator allocator("VecTaskCtx", OB_MALLOC_NORMAL_BLOCK_SIZE);
  if (OB_FAIL(ObDDLUtil::get_tablets(*GCTX.schema_service_, index_table_id_, tablet_ids))) {
    LOG_WARN("failed to get tablet ids", K(ret));
  } else {
    for (int i = 0; i < tablet_ids.count() && OB_SUCC(ret); i++) {
      int64_t new_task_id = OB_INVALID_ID;
      ObTabletID tablet_id = tablet_ids.at(i);
      ObVecIndexAsyncTaskCtx* task_ctx = nullptr;
      common::ObCurTraceId::TraceId new_trace_id;
      char *task_ctx_buf = static_cast<char *>(allocator.alloc(sizeof(ObVecIndexAsyncTaskCtx)));
      if (OB_ISNULL(task_ctx_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("async task ctx is null", K(ret));
      } else if (FALSE_IT(task_ctx = new(task_ctx_buf) ObVecIndexAsyncTaskCtx())) {
      } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_task_id(new_task_id))) {
        LOG_WARN("fail to fetch new task id", K(ret));
      } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_trace_id(
                     ++trace_base_num, &allocator, new_trace_id))) {
        LOG_WARN("fail to fetch new trace id", K(ret), K(tablet_id));
      } else {
        task_ctx->task_status_.tablet_id_ = tablet_id.id();
        task_ctx->task_status_.table_id_ = index_table_id_;
        task_ctx->task_status_.task_id_ = new_task_id;
        task_ctx->task_status_.task_type_ = task_type_;
        task_ctx->task_status_.trigger_type_ = ObVecIndexAsyncTaskTriggerType::OB_VEC_TRIGGER_MANUAL;
        task_ctx->task_status_.status_ = ObVecIndexAsyncTaskStatus::OB_VECTOR_ASYNC_TASK_PREPARE;
        task_ctx->task_status_.trace_id_ = new_trace_id;
        task_ctx->task_status_.target_scn_.convert_from_ts(ObTimeUtility::current_time());
        if (OB_FAIL(task_ctx_array.push_back(task_ctx))) {
          LOG_WARN("fail to push back task status", K(ret), K(task_ctx));
        } else if (OB_FAIL(task_ids_.push_back(new_task_id))) {
          LOG_WARN("fail to push back task id", K(ret), K(new_task_id));
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::insert_new_task(task_ctx_array))) {
    LOG_WARN("fail to insert new tasks", K(ret));
  }
  return ret;
}

int ObVecTaskManager::check_task_status()
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  ObSEArray<int64_t, 4> finished_task;
  ObSEArray<int64_t, 4> tmp_task;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret), KP(sql_proxy));
  } else {
    for (int i = 0; i < task_ids_.count() && OB_SUCC(ret); i++) {
      ObSqlString sql;
      ObVecIndexFieldArray filters;
      ObVecIndexTaskStatusField field;
      field.field_name_ = "task_id";
      field.data_.uint_ = task_ids_.at(i);
      if (OB_FAIL(filters.push_back(field))) {
        LOG_WARN("fail to push back field", K(ret));
      } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::construct_read_task_sql(
                     OB_ALL_VECTOR_INDEX_TASK_HISTORY_TNAME, false, false,
                     filters, *sql_proxy, sql))) {
        LOG_WARN("fail to construct read task sql", K(ret));
      } else {
        SMART_VAR(ObMySQLProxy::MySQLResult, res) {
          ObVecIndexTaskStatus task_result;
          sqlclient::ObMySQLResult* result = nullptr;
          if (OB_FAIL(sql_proxy->read(res, sql.ptr()))) {
            LOG_WARN("fail to execute sql", KR(ret), K(sql));
          } else if (OB_ISNULL(result = res.get_result())) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("error unexpected, query result must not be NULL", K(ret));
          } else if (OB_FAIL(result->next())) {
            if (OB_ITER_END == ret) {
              ret = OB_SUCCESS;
            } else {
              LOG_WARN("fail to get next row", K(ret));
            }
          } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::extract_one_task_sql_result(
                         result, task_result))) {
            LOG_WARN("fail to extract one result", K(ret));
          } else if (OB_FAIL(task_result.ret_code_)) {
            LOG_WARN("task exec failed", K(ret), K(task_result));
          } else if (OB_FAIL(finished_task.push_back(task_result.task_id_))) {
            LOG_WARN("fail to push back task id", K(ret));
          }
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    if (OB_EAGAIN == ret) {
      ret = OB_OP_NOT_ALLOW;
      LOG_USER_ERROR(OB_OP_NOT_ALLOW,
                     "call dbms_vector.refresh_index/rebuild_index before vector index adapter ready is");
      LOG_INFO("call dbms_vector.refresh_index/rebuild_index before vector index adapter ready is not supported, please try again",
               K(ret));
    }
  } else if (finished_task.empty()) {
  } else if (OB_FAIL(get_difference(finished_task, task_ids_, tmp_task))) {
    LOG_WARN("failed to get difference", K(ret), K(finished_task), K(task_ids_));
  } else if (FALSE_IT(task_ids_.reuse())) {
  } else if (OB_FAIL(task_ids_.assign(tmp_task))) {
    LOG_WARN("failed to assign task id", K(ret), K(tmp_task));
  }
  return ret;
}


} // end namespace share
} // end namespace oceanbase

namespace oceanbase
{
namespace data_plane
{

int process_vector_index_embedding_task(const int64_t index_table_id)
{
  share::ObVecTaskManager manager(
      index_table_id,
      share::ObVecIndexAsyncTaskType::OB_VECTOR_ASYNC_HYBRID_VECTOR_EMBEDDING);
  return manager.process_task();
}

int process_vector_index_optimization_task(const int64_t index_table_id)
{
  share::ObVecTaskManager manager(
      index_table_id,
      share::ObVecIndexAsyncTaskType::OB_VECTOR_ASYNC_INDEX_OPTINAL);
  return manager.process_task();
}

} // namespace data_plane
} // namespace oceanbase
