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
#define USING_LOG_PREFIX OBLOG_FETCHER

#include "ob_log_fetcher_bg_worker.h"
#include "ob_log_fetcher.h"

namespace oceanbase
{
namespace logfetcher
{

void UpdateProtoTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  const ObLogFetcherConfig *cfg = nullptr;
  const uint64_t source_min_observer_version = fetcher_.get_source_min_observer_version();
  if (OB_FAIL(fetcher_.get_fetcher_config(cfg))) {
    LOG_WARN("failed to get fetcher config");
  } else if (OB_ISNULL(cfg)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get null config from fetcher");
  // TODO
  //} else if (cfg->logfetcher_parallel_log_transport &&
  //    ((source_min_observer_version >= MOCK_CLUSTER_VERSION_4_2_4_0 && source_min_observer_version < CLUSTER_VERSION_4_3_0_0) ||
  //     source_min_observer_version >= CLUSTER_VERSION_4_3_4_0)) {
  //  fetcher_.update_fetch_log_protocol(obrpc::ObCdcFetchLogProtocolType::RawLogDataProto);
  //  LOG_INFO("update fetch log protocol to RawLogDataProto",
  //      "logfetcher_parallel_log_transport", cfg->logfetcher_parallel_log_transport.get(),
  //      "source_min_observer_version", fetcher_.get_source_min_observer_version());
  } else if (false) {
  } else {
    fetcher_.update_fetch_log_protocol(obrpc::ObCdcFetchLogProtocolType::LogGroupEntryProto);
    LOG_INFO("update fetch log protocol to LogGroupEntryProto",
        "logfetcher_parallel_log_transport", cfg->logfetcher_parallel_log_transport.get(),
        "source_min_observer_version", fetcher_.get_source_min_observer_version());
  }
}

void DataBufferRecycleTask::runTimerTask()
{
  buffer_pool_.try_recycle_expired_buffer();
}

//////////////////////////////// ObLogFetcherBGWorker ////////////////////////////////

ObLogFetcherBGWorker::ObLogFetcherBGWorker():
    is_inited_(false),
    timer_id_(-1),
    tenant_id_(OB_INVALID_TENANT_ID),
    source_tenant_id_(OB_INVALID_TENANT_ID),
    alloc_(),
    task_list_()
{
}

int ObLogFetcherBGWorker::init(const uint64_t tenant_id,
    const uint64_t source_tenant_id,
    ObLogFetcher &log_fetcher,
    LogFileDataBufferPool &pool)
{
  int ret = OB_SUCCESS;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_ERROR("ObLogFetcherBGWorker has been initialized", K(is_inited_));
  } else if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::LogFetcherBGWorker, timer_id_))) {
    LOG_ERROR("failed to create LogFetcher Background Timer");
  } else {
    is_inited_ = true;
    tenant_id_ = tenant_id;
    source_tenant_id_ = source_tenant_id;
    alloc_.set_attr(lib::ObMemAttr(tenant_id_, "LogFetchBGTask"));
    if (OB_FAIL(init_task_list_(log_fetcher, pool))) {
      LOG_ERROR("failed to init bg task list", K(tenant_id_));
    } else {
      LOG_INFO("ObLogFetcherBGWorker finish to init", K(tenant_id_), K(source_tenant_id_),
          K(timer_id_), "task_count", task_list_.count());
    }
  }

  return ret;
}

int ObLogFetcherBGWorker::start()
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("ObLogFetcherBGWorker has not been initialized yet", K(is_inited_));
  } else if (OB_FAIL(TG_START(timer_id_))) {
    LOG_ERROR("ObLogFetcherBGWorker failed to start timer", K(timer_id_));
  } else if (OB_FAIL(schedule_task_list_())) {
    LOG_ERROR("ObLogFetcherBGWorker failed to schedule task_list_");
  } else {
    LOG_INFO("ObLogFetcherBGWorker start", K(timer_id_));
  }

  return ret;
}

void ObLogFetcherBGWorker::stop()
{
  TG_STOP(timer_id_);
}

void ObLogFetcherBGWorker::wait()
{
  TG_WAIT(timer_id_);
}

void ObLogFetcherBGWorker::destroy()
{
  if (IS_INIT) {
    TG_DESTROY(timer_id_);
    timer_id_ = -1;
    destroy_task_list_();
    alloc_.reset();
    tenant_id_ = OB_INVALID_TENANT_ID;
    source_tenant_id_ = OB_INVALID_TENANT_ID;
    is_inited_ = false;
  }
}

int ObLogFetcherBGWorker::init_task_list_(ObLogFetcher &log_fetcher,
    LogFileDataBufferPool &pool)
{
  int ret = OB_SUCCESS;

  void *tmp_buf = nullptr;
  if (OB_ISNULL(tmp_buf = alloc_.alloc(sizeof(UpdateProtoTask)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to alloc GetServerVersionTask");
  } else if (OB_FAIL(task_list_.push_back(ObLogFetcherBGTask(UpdateProtoTask::SCHEDULE_INTERVAL,
      UpdateProtoTask::NEED_REPEAT, new (tmp_buf) UpdateProtoTask(log_fetcher))))) {
    LOG_ERROR("failed to push back GetServerVersionTask");
  } else if (OB_ISNULL(tmp_buf = alloc_.alloc(sizeof(DataBufferRecycleTask)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("failed to alloc GetServerVersionTask");
  } else if (OB_FAIL(task_list_.push_back(ObLogFetcherBGTask(DataBufferRecycleTask::SCHEDULE_INTERVAL,
      DataBufferRecycleTask::NEED_REPEAT, new (tmp_buf) DataBufferRecycleTask(pool))))) {
    LOG_ERROR("failed to push back GetServerVersionTask");
  } else {
    LOG_INFO("ObLogFetcherBGWorker finished to init task list", "task_num", task_list_.count());
  }

  return ret;
}

int ObLogFetcherBGWorker::schedule_task_list_()
{
  int ret = OB_SUCCESS;
  run_tasks_once_();
  ARRAY_FOREACH(task_list_, idx) {
    ObLogFetcherBGTask &timer_task = task_list_.at(idx);
    if (OB_ISNULL(timer_task.get_task())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("timer task is null, unexpected", K(timer_task));
    } else if (OB_FAIL(TG_SCHEDULE(timer_id_, *timer_task.get_task(), timer_task.get_schedule_interval(),
        timer_task.need_repeat(), false))) {
      LOG_ERROR("failed to schedule timer task", K(timer_task));
    } else {
      // success
      LOG_INFO("LogFetcherBGWorker start to schedule a timer_task", K(idx), K(timer_task));
    }
  }
  return ret;
}

void ObLogFetcherBGWorker::run_tasks_once_()
{
  int ret = OB_SUCCESS;
  ARRAY_FOREACH_NORET(task_list_, idx) {
    ObLogFetcherBGTask &timer_task = task_list_.at(idx);
    if (OB_ISNULL(timer_task.get_task())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("timer task is null, unexpected", K(timer_task));
    } else {
      timer_task.get_task()->runTimerTask();
      LOG_INFO("LogFetcherBGWorker finish running timer_task once at start", K(idx), K(timer_task));
    }
  }
}

void ObLogFetcherBGWorker::destroy_task_list_()
{
  ARRAY_FOREACH_NORET(task_list_, idx) {
    ObLogFetcherBGTask &timer_task = task_list_.at(idx);
    ObTimerTask *inner_task = timer_task.get_task();
    inner_task->~ObTimerTask();
    alloc_.free(inner_task);
    timer_task.reset();
  }
  task_list_.reset();
}

}
}
