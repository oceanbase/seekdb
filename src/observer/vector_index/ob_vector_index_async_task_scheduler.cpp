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

#include "observer/vector_index/ob_vector_index_async_task_scheduler.h"
#include "observer/vector_index/ob_vector_index_async_task_util.h"
#include "share/ob_max_id_fetcher.h"

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
  } else if (is_paused_) {
    // timer paused or not leader, do nothing
  } else if (!ObVecIndexAsyncTaskUtil::check_can_do_work()) { // skip
  } else if (!ObVecIndexAsyncTaskUtil::check_can_process_tasks()) { // skip
  } else if (OB_FAIL(move_task_to_history_table())) {
  } else if (OB_FAIL(clear_history_task())) {
  }
}

int ObVectorIndexHistoryTask::clear_history_task()
{
  int ret = OB_SUCCESS;

  const int64_t batch_size = OB_VEC_INDEX_TASK_DEL_COUNT_PER_TASK; // 4096
  const int64_t now = ObTimeUtility::current_time();
  int64_t delete_timestamp = now - OB_VEC_INDEX_TASK_HISTORY_SAVE_TIME_US;
  int64_t affect_rows = 0;
  ObSqlString sql;
  ObMySQLTransaction trans;
  if (OB_ISNULL(sql_proxy_) || false) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_paused_) {
    ret = OB_EAGAIN;
    FLOG_INFO("exit timer task once cuz leader switch", KR(ret));
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::clear_history_expire_task_record(batch_size, trans, affect_rows))) {
  } else {
    LOG_DEBUG("success to clear_history_task", K(ret), K(sql), K(affect_rows));
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
  if (OB_ISNULL(sql_proxy_) || false) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    while (OB_SUCC(ret) && move_rows != 0) {
      ObMySQLTransaction trans;
      if (is_paused_) {
        ret = OB_EAGAIN;
        FLOG_INFO("exit timer task once cuz leader switch", K(ret));
      } else if (OB_FAIL(trans.start(sql_proxy_))) {
      } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::move_task_to_history_table(batch_size, trans, move_rows))) {
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

// ---------------------------------- ObVectorIndexAsyncTaskScheduler -----------------------------------------//

int ObVectorIndexAsyncTaskScheduler::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(timer_.init(
      "VecIdxMgr", common::ObMemAttr("VecIdxMgr")))) {
  } else if (OB_FAIL(vec_history_task_.init(sql_proxy))) { // History table cleanup
    LOG_WARN("fail to init clear history task");
  } else {
    is_inited_ = true;

    LOG_INFO("vector index mgr is inited");
  }
  return ret;
}

int ObVectorIndexAsyncTaskScheduler::start()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("vector manager begin to start");
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(timer_.schedule(vec_history_task_, VEC_INDEX_CLEAR_TASK_PERIOD, true))) {
  }
  FLOG_INFO("start vector index manager", KR(ret));

  return ret;
}

void ObVectorIndexAsyncTaskScheduler::wait()
{
  FLOG_INFO("wait vector index async task manager");
  if (timer_.inited()) {
    timer_.wait();
  }
  FLOG_INFO("finish to wait vector index async task manager");
}

void ObVectorIndexAsyncTaskScheduler::stop()
{
  FLOG_INFO("stop vector index async task manager");
  if (timer_.inited()) {
    timer_.stop();
  }
  FLOG_INFO("finish to stop vector index async task manager");
}

void ObVectorIndexAsyncTaskScheduler::destroy()
{
  FLOG_INFO("destroy vector index async task manager");
  timer_.destroy();
  FLOG_INFO("finish to destroy vector index async task manager");
}

void ObVectorIndexAsyncTaskScheduler::resume()
{
  vec_history_task_.resume();
}

void ObVectorIndexAsyncTaskScheduler::pause()
{
  vec_history_task_.pause();
}


} // end namespace share
} // end namespace oceanbase
