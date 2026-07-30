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

// Asynchronous persistence for local tablet runtime metadata.


#include "ob_tablet_runtime_meta_updater.h"
#include "observer/omt/ob_server_runtime_controller.h"
#include "share/tablet/ob_tablet_table_operator.h"  // for ObTabletOperator
#include "share/rc/ob_module_provider.h"
#include "observer/ob_service.h"                    // for is_mini_mode
#include "share/ob_tablet_local_checksum_operator.h" // for ObTabletLocalChecksumItem
#include "storage/compaction/ob_compaction_diagnose.h"
#include "share/storage/ob_sqlite_connection.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace observer
{

void TSITabletRuntimeMetaUpdateStatistics::reset()
{
  suc_cnt_ = 0;
  fail_cnt_ = 0;
  remove_task_cnt_ = 0;
  update_task_cnt_ = 0;
  total_wait_us_ = 0;
  total_exec_us_ = 0;
}

void TSITabletRuntimeMetaUpdateStatistics::calc(
     int64_t succ_cnt,
     int64_t fail_cnt,
     int64_t remove_task_cnt,
     int64_t update_task_cnt,
     int64_t wait_us,
     int64_t exec_us)
{
  total_wait_us_ += wait_us;
  total_exec_us_ += exec_us;
  suc_cnt_ += succ_cnt;
  fail_cnt_ += fail_cnt;
  remove_task_cnt_ += remove_task_cnt;
  update_task_cnt_ += update_task_cnt;
}

void TSITabletRuntimeMetaUpdateStatistics::dump()
{
  int64_t total_cnt = suc_cnt_ + fail_cnt_;
  FLOG_INFO("[TABLET_RUNTIME_META_UPDATE_STATISTIC] dump tablet runtime metadata update statistics",
           K_(suc_cnt), K_(fail_cnt), K_(remove_task_cnt), K_(update_task_cnt),
           "avg_wait_us", total_wait_us_ / total_cnt,
           "avg_exec_us", total_exec_us_ / total_cnt);
}

/*
 * ObTabletRuntimeMetaUpdateTask implement
 * */
ObTabletRuntimeMetaUpdateTask::~ObTabletRuntimeMetaUpdateTask()
{
}

int ObTabletRuntimeMetaUpdateTask::init(
    const ObTabletID &tablet_id,
    const int64_t add_timestamp,
    const bool need_diagnose/*false*/)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_id.is_valid()
      || 0 >= add_timestamp)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("task init failed", KR(ret), K(tablet_id), K(add_timestamp));
  } else {
    tablet_id_ = tablet_id;
    add_timestamp_ = add_timestamp;
    need_diagnose_ = need_diagnose;
  }
  return ret;
}

int ObTabletRuntimeMetaUpdateTask::assign(const ObTabletRuntimeMetaUpdateTask &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    tablet_id_ = other.get_tablet_id();
    add_timestamp_ = other.get_add_timestamp();
    need_diagnose_ = other.need_diagnose_;
    start_timestamp_ = other.start_timestamp_;
  }
  return ret;
}

bool ObTabletRuntimeMetaUpdateTask::operator ==(const ObTabletRuntimeMetaUpdateTask &other) const
{
  bool equal = false;
  if (this == &other) { // same pointer
    equal = true;
  } else {
    equal = tablet_id_ == other.tablet_id_;
  }
  return equal;
}

void ObTabletRuntimeMetaUpdateTask::reset()
{
  tablet_id_.reset();
  add_timestamp_ = OB_INVALID_TIMESTAMP;
  need_diagnose_ = false;
}

bool ObTabletRuntimeMetaUpdateTask::compare_without_version(
         const ObTabletRuntimeMetaUpdateTask &other) const
{
  bool equal = false;
  if (&other == this) {
    equal = true;
  } else  {
    equal = tablet_id_ == other.tablet_id_;
  }
  return equal;
}

void ObTabletRuntimeMetaUpdateTask::check_task_status() const
{
  int64_t now = ObTimeUtility::current_time();
  const int64_t safe_interval = TABLET_CHECK_INTERVAL;
  // need to print a WARN log if this task is not executed correctly since two minuts ago
  if (now - add_timestamp_ > safe_interval) {
    FLOG_WARN_RET(OB_ERR_UNEXPECTED, "tablet runtime metadata update task cost too much time to execute",
              K(*this), K(safe_interval), "cost_time", now - add_timestamp_);
  }
}

void ObTabletRuntimeMetaUpdateTask::set_start_timestamp()
{
  start_timestamp_ = ObTimeUtility::current_time();
}

int64_t ObTabletRuntimeMetaUpdateTask::get_start_timestamp() const
{
  return start_timestamp_;
}

bool ObTabletRuntimeMetaUpdateTask::is_valid() const
{
  return tablet_id_.is_valid()
      && 0 < add_timestamp_;
}

bool ObTabletRuntimeMetaUpdateTask::is_barrier() const
{
  return false;
}

int64_t ObTabletRuntimeMetaUpdateTask::hash() const
{
  uint64_t hash_val = 0;
  hash_val = murmurhash(&tablet_id_, sizeof(tablet_id_), hash_val);
  return hash_val;
}

/*
 * ObTabletRuntimeMetaUpdater implement
 * */
int ObTabletRuntimeMetaUpdater::server_module_init(ObTabletRuntimeMetaUpdater *&tablet_runtime_meta_updater)
{
  return tablet_runtime_meta_updater->init();
}

int ObTabletRuntimeMetaUpdater::init()
{
  int ret = OB_SUCCESS;
  const int64_t update_queue_size = !lib::is_mini_mode()
                                    ? UPDATE_QUEUE_SIZE
                                    : MINI_MODE_UPDATE_QUEUE_SIZE;
  // TODO: allow set thread_cnt in config file
  const int64_t update_task_thread_cnt = cal_thread_count_();
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("inited twice", KR(ret));
  } else if (FALSE_IT(use_shared_executor_ = lib::is_mini_mode())) {
  } else if (use_shared_executor_
             && (OB_ISNULL(share::g_mp)
                 || OB_ISNULL(background_executor_ =
                     share::g_mp->background_task_executor()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("background task executor is null", K(ret),
        KP(share::g_mp), KP(background_executor_));
  } else if (use_shared_executor_
             && OB_FAIL(update_queue_.init_without_thread(
                 this, 1, update_queue_size, "TbltMetaUp", &is_stop_))) {
    LOG_WARN("init externally driven tablet runtime metadata queue failed",
        KR(ret), K(update_queue_size));
  } else if (!use_shared_executor_
             && OB_FAIL(update_queue_.init(this,
                 update_task_thread_cnt,
                 update_queue_size,
                 "TbltMetaUp"))) {
    LOG_WARN("init tablet runtime metadata updater queue failed", KR(ret),
             "thread_count", update_task_thread_cnt,
             "queue_size", update_queue_size);
  } else if (use_shared_executor_) {
    share::ObBackgroundTaskSourceConfig config;
    config.name_ = "TbltMetaUp";
    config.max_concurrency_ = 1;
    if (OB_FAIL(background_executor_->register_source(
        *this, config, source_handle_))) {
      LOG_WARN("register tablet runtime metadata source failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    is_inited_ = true;
    ATOMIC_STORE(&is_stop_, false);
  } else {
    (void) unregister_source_(true);
    update_queue_.destroy();
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("init a ObTabletRuntimeMetaUpdater success", K(update_task_thread_cnt));
  }
  return ret;
}

void ObTabletRuntimeMetaUpdater::stop()
{
  if (is_inited_) {
    ATOMIC_STORE(&is_stop_, true);
    if (use_shared_executor_) {
      const int tmp_ret = unregister_source_(false);
      if (OB_SUCCESS != tmp_ret && OB_EAGAIN != tmp_ret) {
        LOG_WARN_RET(tmp_ret, "fail to stop tablet runtime metadata source",
            K(tmp_ret));
      }
    } else {
      update_queue_.stop();
    }
    LOG_INFO("stop ObTabletRuntimeMetaUpdater success");
  }
}

void ObTabletRuntimeMetaUpdater::wait()
{
  if (is_inited_) {
    if (use_shared_executor_) {
      const int tmp_ret = unregister_source_(true);
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN_RET(tmp_ret, "fail to wait tablet runtime metadata source",
            K(tmp_ret));
      }
    } else {
      update_queue_.wait();
    }
    LOG_INFO("wait ObTabletRuntimeMetaUpdater");
  }
}

void ObTabletRuntimeMetaUpdater::destroy()
{
  stop();
  wait();
  update_queue_.destroy();
  is_inited_ = false;
  ATOMIC_STORE(&is_stop_, true);
}

int64_t ObTabletRuntimeMetaUpdater::cal_thread_count_()
{
  int tmp_ret = OB_SUCCESS;
  int64_t thread_cnt = MINI_MODE_UPDATE_TASK_THREAD_CNT;
  if (!lib::is_mini_mode()) {
    double max_cpu = 0;
    double min_cpu = 0;
    omt::ObServerRuntimeController *runtime_controller = GCTX.server_runtime_controller_;
    if (NULL == runtime_controller) {
      tmp_ret = OB_INVALID_ARGUMENT;
      LOG_WARN_RET(tmp_ret, "invalid argument", K(tmp_ret), KP(runtime_controller));
    } else if (OB_TMP_FAIL(runtime_controller->get_server_cpu(min_cpu, max_cpu))) {
      LOG_WARN_RET(tmp_ret, "fail to get server CPU", K(tmp_ret), K(min_cpu), K(max_cpu));
    } else {
      thread_cnt = std::max(MIN_UPDATE_TASK_THREAD_CNT,
          static_cast<int64_t>(lround(MIN_UPDATE_TASK_THREAD_CNT * UPDATE_TASK_THREAD_RATIO * max_cpu)));
      thread_cnt = std::min(thread_cnt, MAX_UPDATE_TASK_THREAD_CNT);
    }
  }
  return thread_cnt;
}

int ObTabletRuntimeMetaUpdater::submit_update_task(
    const ObTabletID &tablet_id,
    const bool need_diagnose/*false*/)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletRuntimeMetaUpdater is not inited", KR(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id));
  } else if (OB_FAIL(async_update(tablet_id, need_diagnose))) {
    LOG_WARN("fail to async update tablet", KR(ret), K(tablet_id));
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::async_update(
    const common::ObTabletID &tablet_id,
    const bool need_diagnose/*false*/)
{
  int ret = OB_SUCCESS;
  int64_t add_timestamp = ObTimeUtility::current_time();
  ObTabletRuntimeMetaUpdateTask task;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletRuntimeMetaUpdater is not inited", KR(ret));
  } else if (tablet_id.is_reserved_tablet()) {
    LOG_TRACE("no need to update reserved tablet", KR(ret), K(tablet_id));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id));
  } else if (OB_FAIL(task.init(tablet_id,
                               add_timestamp,
                               need_diagnose))) {
    LOG_WARN("set update task failed", KR(ret), K(tablet_id),
             K(add_timestamp));
  } else if (OB_FAIL(add_task_(task))){
    LOG_WARN("fail to add task", KR(ret), K(tablet_id),
             K(add_timestamp));
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::add_task_(
    const ObTabletRuntimeMetaUpdateTask &task)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (ATOMIC_LOAD(&is_stop_)) {
    ret = OB_IN_STOP_STATE;
    LOG_WARN("tablet runtime metadata updater is stopping", K(ret));
  } else if (!task.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid task", KR(ret), K(task));
  }
  if (OB_SUCC(ret)) {
    const int queue_ret = update_queue_.add(task);
    if (OB_SUCCESS != queue_ret && OB_EAGAIN != queue_ret) {
      ret = queue_ret;
    } else {
      // A duplicate means that runnable work is already pending. Notify again
      // so a prior transient wakeup failure cannot strand that task.
      if (use_shared_executor_) {
        const int notify_ret = background_executor_->notify(
            source_handle_, share::BG_TASK_NORMAL);
        if (OB_SUCCESS != notify_ret) {
          // The queue already owns the task. notify() is only a wakeup hint
          // and must not change the submit result.
          LOG_WARN_RET(notify_ret,
              "fail to notify tablet runtime metadata source",
              K(notify_ret));
        }
      } else if (OB_EAGAIN == queue_ret) {
        // TODO: deal with barrier-tasks when execute
        LOG_TRACE("tablet runtime metadata update task exists", K(task));
      }
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("add tablet runtime metadata update task failed", KR(ret), K(task));
    if (task.need_diagnose() && OB_TMP_FAIL(compaction::ADD_SUSPECT_INFO(
        compaction::MEDIUM_MERGE, share::ObDiagnoseTabletType::TYPE_RUNTIME_META_UPDATE,
        task.get_tablet_id(),
        ObSuspectInfoType::SUSPECT_RUNTIME_META_UPDATE_ADD_FAILED,
        static_cast<int64_t>(ret)))) {
      LOG_WARN_RET(tmp_ret, "fail to add suspect info", K(tmp_ret));
    }
  } else {
    if (task.need_diagnose()) {
      DEL_SUSPECT_INFO(
        compaction::MEDIUM_MERGE,
        task.get_tablet_id(),
        share::ObDiagnoseTabletType::TYPE_RUNTIME_META_UPDATE);
    }
    LOG_TRACE("add tablet runtime metadata update task success", KR(ret), K(task));
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::reput_to_queue_(
    const ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else { // try to push task back to queue, ignore ret code
    ARRAY_FOREACH_NORET(tasks, i) {
      const ObTabletRuntimeMetaUpdateTask &task = tasks.at(i);
      if (OB_UNLIKELY(!task.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid task", KR(ret), K(task));
      } else if (OB_FAIL(add_task_(task))) {
        LOG_ERROR("fail to reput to queue", KR(ret), K(task));
      }
    }
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::process_barrier(
    const ObTabletRuntimeMetaUpdateTask &task,
    bool &stopped)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(task);
  UNUSED(stopped);
  LOG_WARN("not supported now", KR(ret), K(task), K(stopped));
  return ret;
}

int ObTabletRuntimeMetaUpdater::set_thread_count()
{
  int ret = OB_SUCCESS;
  int64_t thread_count = cal_thread_count_();
  if (use_shared_executor_) {
    // Mini mode is intentionally capped at one source quantum at a time.
  } else if (OB_FAIL(update_queue_.set_thread_count(thread_count))) {
    LOG_WARN("fail to set thread count", K(ret), K(thread_count));
  } else {
    LOG_TRACE("success to set thread count", K(thread_count));
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::process_one_quantum(
    const share::ObBackgroundTaskPriority priority,
    share::ObBackgroundTaskRunResult &result)
{
  int ret = OB_SUCCESS;
  int64_t processed_count = 0;
  bool has_more_ready = false;
  if (!use_shared_executor_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (share::BG_TASK_NORMAL != priority) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(update_queue_.process_one_quantum(
      processed_count, has_more_ready))) {
    LOG_WARN("fail to process tablet runtime metadata quantum", K(ret));
  } else {
    result.processed_count_ = processed_count;
    result.has_more_ready_ = has_more_ready;
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::unregister_source_(const bool wait_running)
{
  int ret = OB_SUCCESS;
  if (use_shared_executor_
      && OB_NOT_NULL(background_executor_)
      && source_handle_.is_valid()) {
    do {
      ret = background_executor_->unregister_source(source_handle_);
      if (wait_running && OB_EAGAIN == ret) {
        ob_usleep(10 * 1000L);
      }
    } while (wait_running && OB_EAGAIN == ret);
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::check_exist(
    const ObTabletID &tablet_id,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id));
  } else {
    ObTabletRuntimeMetaUpdateTask task(tablet_id, ObClockGenerator::getClock());
    if (OB_FAIL(update_queue_.check_exist(task, exist))) {
      LOG_WARN("fail to check task exist", K(ret), K(task), K(exist));
    }
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::check_processing_exist(
    const ObTabletID &tablet_id,
    bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id));
  } else {
    ObTabletRuntimeMetaUpdateTask task(tablet_id, ObClockGenerator::getClock());
    if (OB_FAIL(update_queue_.check_processing_exist(task, exist))) {
      LOG_WARN("fail to check processing task exist", K(ret), K(task), K(exist));
    }
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::diagnose_existing_task(
    ObIArray<ObTabletRuntimeMetaUpdateTask> &waiting_tasks,
    ObIArray<ObTabletRuntimeMetaUpdateTask> &processing_tasks)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(update_queue_.diagnose_waiting_task(waiting_tasks))) {
    LOG_WARN("fail to diagnose waiting task", K(ret));
  } else if (OB_FAIL(update_queue_.diagnose_processing_task(processing_tasks))) {
    LOG_WARN("fail to diagnose processing task", K(ret));
  }
  return ret;
}

void ObTabletRuntimeMetaUpdater::diagnose_batch_tasks_(
    const ObIArray<ObTabletRuntimeMetaUpdateTask> &batch_tasks,
    const int error_code)
{
  int tmp_ret = OB_SUCCESS;
  int64_t diagnose_cnt = 0;
  for (int64_t i = 0; i < batch_tasks.count() && diagnose_cnt < DIAGNOSE_MAX_BATCH_COUNT; ++i) {
    const ObTabletRuntimeMetaUpdateTask &task = batch_tasks.at(i);
    if (task.need_diagnose()) {
      if (OB_TMP_FAIL(compaction::ADD_SUSPECT_INFO(
          compaction::MEDIUM_MERGE, share::ObDiagnoseTabletType::TYPE_RUNTIME_META_UPDATE,
          task.get_tablet_id(),
          ObSuspectInfoType::SUSPECT_RUNTIME_META_UPDATE_PROGRESS_FAILED,
          static_cast<int64_t>(error_code)))) {
        LOG_WARN_RET(tmp_ret, "fail to add suspect info", K(tmp_ret));
      } else {
        ++diagnose_cnt;
      }
    }
  }
}

int ObTabletRuntimeMetaUpdater::push_task_info_(
    const ObTabletRuntimeMetaUpdateTask &task,
    const share::ObTabletRuntimeInfo &tablet_info,
    ObArray<share::ObTabletRuntimeInfo> &tablet_infos,
    ObArray<ObTabletRuntimeMetaUpdateTask> &task_list)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(task_list.reserve(UNIQ_TASK_QUEUE_BATCH_EXECUTE_NUM))) {
    // reserve() is reentrant, do not have to check whether first time
    LOG_WARN("fail to reserver task_list", KR(ret), K(UNIQ_TASK_QUEUE_BATCH_EXECUTE_NUM));
  } else if (OB_FAIL(task_list.push_back(task))) {
    LOG_WARN("fail to push back remove task", KR(ret), K(task));
  } else if (OB_FAIL(tablet_infos.reserve(UNIQ_TASK_QUEUE_BATCH_EXECUTE_NUM))) {
    LOG_WARN("fail to reserver tablet_infos", KR(ret), K(UNIQ_TASK_QUEUE_BATCH_EXECUTE_NUM));
  } else if (OB_FAIL(tablet_infos.push_back(tablet_info))) {
    LOG_WARN("fail to push back tablet_info", KR(ret), K(tablet_info));
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::generate_tasks_(
    const ObIArray<ObTabletRuntimeMetaUpdateTask> &batch_tasks,
    ObArray<ObTabletRuntimeInfo> &update_tablet_infos,
    ObArray<ObTabletRuntimeInfo> &remove_tablet_infos,
    ObArray<ObTabletLocalChecksumItem> &update_tablet_checksums,
    UpdateTaskList &update_tablet_tasks,
    RemoveTaskList &remove_tablet_tasks)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t retry_tablet_count = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletRuntimeMetaUpdater is not inited", KR(ret));
  } else if (OB_ISNULL(GCTX.tablet_operator_) || OB_ISNULL(GCTX.ob_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.tablet_operator_), KP(GCTX.ob_service_));
  } else if (OB_UNLIKELY(batch_tasks.count() <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("batch_tasks count <= 0", KR(ret), "tasks_count", batch_tasks.count());
  }

  ObTabletRuntimeInfo tablet_info;
  ObTabletLocalChecksumItem checksum_item;
  FOREACH_CNT_X(task, batch_tasks, OB_SUCC(ret)) {
    // split tasks into remove and update
    if (OB_ISNULL(task)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid task", KR(ret), K(task));
    } else if (FALSE_IT(task->check_task_status())) {
    } else if (FALSE_IT(tablet_info.reset())) {
    } else if (FALSE_IT(checksum_item.reset())) {
    } else if (OB_FAIL(GCTX.ob_service_->fill_tablet_runtime_info(task->get_tablet_id(),
                                                                 tablet_info,
                                                                 checksum_item))) {
      bool is_remove_task = false;
      if (OB_EAGAIN == ret) {
        if (OB_TMP_FAIL(add_task_(*task))) {
          LOG_WARN("fail to add task", KR(tmp_ret), KPC(task));
        } else {
          retry_tablet_count++;
          ret = OB_SUCCESS; // do not affect update of other tablets
        }
      } else if (OB_SERVER_RUNTIME_NOT_READY != ret && OB_LS_NOT_EXIST != ret && OB_TABLET_NOT_EXIST != ret) {
        LOG_WARN("failed to fill tablet runtime info", KR(ret), KPC(task));
      } else if (OB_SERVER_RUNTIME_NOT_READY == ret) {
        is_remove_task = true;
        ret = OB_SUCCESS;
      } else {
        is_remove_task = true;
        ret = OB_SUCCESS;
      }

      if (OB_FAIL(ret) || !is_remove_task) {
        // do nothing
      } else if (OB_FAIL(tablet_info.init(task->get_tablet_id(),
                                      1/*snapshot_version*/,
                                      1/*data_size*/,
                                      1/*required_size*/,
                                      0/*report_scn*/,
                                      ObTabletRuntimeInfo::SCN_STATUS_IDLE))) {
        LOG_WARN("fail to init ObTabletRuntimeInfo", KR(ret), KPC(task));
      } else if (OB_FAIL(push_task_info_(*task, tablet_info, remove_tablet_infos, remove_tablet_tasks))) {
        LOG_WARN("failed to push remove task", K(ret), KPC(task));
      }
    } else {
      LOG_TRACE("fill tablet success", K(task), K(tablet_info));
      if (OB_FAIL(push_task_info_(*task, tablet_info, update_tablet_infos, update_tablet_tasks))) {
        LOG_WARN("failed to push update task info", KR(ret), KPC(task), K(tablet_info));
      } else if (OB_FAIL(update_tablet_checksums.reserve(UNIQ_TASK_QUEUE_BATCH_EXECUTE_NUM))) {
        // reserve() is reentrant, do not have to check whether first time
        LOG_WARN("fail to reserve update_tablet_checksums", KR(ret), K(UNIQ_TASK_QUEUE_BATCH_EXECUTE_NUM));
      } else if (OB_FAIL(update_tablet_checksums.push_back(checksum_item))) {
        LOG_WARN("fail to push back checksum item", KR(ret), K(checksum_item));
      }
    }
  } //FOREACH

  if (OB_FAIL(ret)) {
  } else if (update_tablet_tasks.count() != update_tablet_infos.count()
          || update_tablet_tasks.count() != update_tablet_checksums.count()
          || ((update_tablet_tasks.count() + remove_tablet_tasks.count() + retry_tablet_count != batch_tasks.count()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet task count and tablet_info count not match", KR(ret),
             "tablet_update_tasks count", update_tablet_tasks.count(),
             "tablet_update_infos count", update_tablet_infos.count(),
             "tablet_update_checksums count", update_tablet_checksums.count(),
             "tablet_remove_tasks count", remove_tablet_tasks.count(),
             K(retry_tablet_count),
             "batch_tasks count", batch_tasks.count());
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::batch_process_tasks(
    const ObIArray<ObTabletRuntimeMetaUpdateTask> &batch_tasks,
    bool &stopped)
{
  UNUSED(stopped);
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  const int64_t start_time = ObTimeUtility::current_time();
  ObArray<ObTabletRuntimeInfo> update_tablet_infos;
  ObArray<ObTabletRuntimeInfo> remove_tablet_infos;
  ObArray<ObTabletLocalChecksumItem> update_tablet_checksums;
  UpdateTaskList update_tablet_tasks;
  RemoveTaskList remove_tablet_tasks;
  ObCurTraceId::init(GCONF.self_addr_);
  int64_t succ_cnt = 0;
  int64_t update_task_cnt = 0;
  int64_t remove_task_cnt = 0;
  int64_t wait_cost = 0;
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; i < batch_tasks.count(); i++) { // overwrite ret
      wait_cost += (start_time - batch_tasks.at(i).get_add_timestamp());
    }
  }
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletRuntimeMetaUpdater is not inited", KR(ret));
  } else if (batch_tasks.count() <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid batch_tasks", KR(ret), "task count", batch_tasks.count());
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(generate_tasks_(
      batch_tasks,
      update_tablet_infos,
      remove_tablet_infos,
      update_tablet_checksums,
      update_tablet_tasks,
      remove_tablet_tasks))) {
    //There is a situation where there are too many tablet holds and cannot be obtained
    LOG_WARN("generate_tasks failed", KR(ret), "batch_tasks count", batch_tasks.count(),
              "update_tablet_infos", update_tablet_infos.count(),
              "remove_tablet_infos", remove_tablet_infos.count(),
              "update_tablet_checksums", update_tablet_checksums.count(),
              "update_tablet_tasks", update_tablet_tasks.count(),
              "remove_tablet_tasks", remove_tablet_tasks.count());
  } else {
    update_task_cnt = update_tablet_infos.count();
    remove_task_cnt = remove_tablet_infos.count();
    if (update_tablet_tasks.count() > 0) {
      tmp_ret = do_batch_update_(start_time, update_tablet_tasks, update_tablet_infos, update_tablet_checksums);
      if (OB_SUCCESS != tmp_ret) {
        ret = OB_SUCC(ret) ? tmp_ret : ret;
        LOG_WARN("do_batch_update_ failed", KR(tmp_ret), K(start_time),
            "tasks count", update_tablet_tasks.count(),
            "tablet_info count", update_tablet_infos.count());
        diagnose_batch_tasks_(update_tablet_tasks, tmp_ret);
      } else {
        succ_cnt += update_task_cnt;
      }
    }
    if (remove_tablet_tasks.count() > 0) {
      tmp_ret = do_batch_remove_(start_time, remove_tablet_tasks, remove_tablet_infos);
      if (OB_SUCCESS != tmp_ret) {
        ret = OB_SUCC(ret) ? tmp_ret : ret;
        LOG_WARN("do_batch_remove_ failed", KR(tmp_ret), K(start_time),
            "tasks count", remove_tablet_tasks.count(),
            "remove tablet_infos count", remove_tablet_infos.count());
        diagnose_batch_tasks_(remove_tablet_tasks, tmp_ret);
      } else {
        succ_cnt += remove_task_cnt;
      }
    }
  }
  const int64_t end = ObTimeUtility::current_time();
  auto* statistics = GET_TSI(TSITabletRuntimeMetaUpdateStatistics);
  if (OB_ISNULL(statistics)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get statistic", "ret", OB_ERR_UNEXPECTED);
  } else {
    (void)statistics->calc(succ_cnt, batch_tasks.count() - succ_cnt,
        remove_task_cnt, update_task_cnt, wait_cost, end - start_time);
    const int64_t interval = 10 * 1000 * 1000; // 1s
    if (TC_REACH_TIME_INTERVAL(interval)) {
      (void)statistics->dump();
      (void)statistics->reset();
    }
  }
  return ret;
}

int ObTabletRuntimeMetaUpdater::do_batch_remove_(
    const int64_t start_time,
    const ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t tasks_count = tasks.count();
  const int64_t batch_remove_start_time = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(tasks_count != tablet_infos.count() || OB_ISNULL(GCTX.tablet_operator_))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tasks count", KR(ret), K(tasks_count), KP(GCTX.tablet_operator_));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ not initialized", K(ret));
  } else {
    // Use SQLite transaction for multi-table operations
    share::ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
    if (!guard) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to acquire connection", K(ret));
    } else if (OB_FAIL(guard->begin_transaction())) {
      LOG_WARN("fail to start transaction", KR(ret));
    } else if (OB_FAIL(GCTX.tablet_operator_->batch_remove(guard.get_connection(), tablet_infos))) {
      LOG_WARN("remove tablet runtime metadata failed, try to reput to queue", KR(ret),
               "escape time", ObTimeUtility::current_time() - start_time);
    } else if (OB_FAIL(ObTabletLocalChecksumOperator::batch_remove_with_trans(guard.get_connection(), tablet_infos))) {
      LOG_WARN("remove local tablet checksum failed, try to reput to queue", KR(ret),
               "escape time", ObTimeUtility::current_time() - start_time);
    }

    if (guard->is_in_transaction()) {
      if (OB_FAIL(ret)) {
        int rollback_ret = guard->rollback();
        if (OB_SUCCESS != rollback_ret) {
          LOG_WARN("fail to rollback transaction", KR(rollback_ret));
        }
      } else {
        int commit_ret = guard->commit();
        if (OB_SUCCESS != commit_ret) {
          LOG_ERROR("fail to commit transaction", KR(commit_ret));
          ret = commit_ret;
        }
      }
    }
    if (OB_FAIL(ret)) {
      (void) throttle_(ret, ObTimeUtility::current_time() - start_time);
      if (OB_SUCCESS != (tmp_ret = reput_to_queue_(tasks))) {
        LOG_ERROR("fail to reput remove task to queue", KR(tmp_ret), K(tasks_count));
      } else {
        LOG_TRACE("reput remove task to queue success", K(tasks_count));
      }
    }
  }
  LOG_INFO("RUNTIME_META: batch remove tablets finished", KR(ret), K(tasks_count), K(tasks),
      "cost_time", ObTimeUtility::current_time() - batch_remove_start_time);
  return ret;
}

int ObTabletRuntimeMetaUpdater::do_batch_update_(
    const int64_t start_time,
    const ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos,
    const ObIArray<ObTabletLocalChecksumItem> &checksums)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  const int64_t batch_update_start_time = ObTimeUtility::current_time();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (tasks.count() != tablet_infos.count()
      || tasks.count() != checksums.count()
      || OB_ISNULL(GCTX.tablet_operator_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("tasks num not match or invalid tablet_operator", KR(ret), "task_cnt", tasks.count(),
             "tablet_info_cnt", tablet_infos.count(), "checksum_cnt", checksums.count(), K(GCTX.tablet_operator_));
  } else {
    if (OB_ISNULL(GCTX.meta_db_pool_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("meta_db_pool_ not initialized", K(ret));
    } else {
      // Use SQLite transaction for multi-table operations
      share::ObSQLiteConnectionGuard guard(GCTX.meta_db_pool_);
      if (!guard) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("failed to acquire connection", K(ret));
      } else if (OB_FAIL(guard->begin_transaction())) {
        LOG_WARN("fail to start transaction", KR(ret));
      } else if (OB_FAIL(GCTX.tablet_operator_->batch_update(guard.get_connection(), tablet_infos))) {
        LOG_WARN("update tablet runtime metadata failed, try to reput to queue", KR(ret),
              "escape time", ObTimeUtility::current_time() - start_time);
      } else if (OB_FAIL(ObTabletLocalChecksumOperator::batch_update_with_trans(guard.get_connection(), checksums))) {
        LOG_WARN("update local tablet checksum failed, try to reput to queue", KR(ret),
             "escape time", ObTimeUtility::current_time() - start_time);
      }

      if (guard->is_in_transaction()) {
        if (OB_FAIL(ret)) {
          int rollback_ret = guard->rollback();
          if (OB_SUCCESS != rollback_ret) {
            LOG_WARN("fail to rollback transaction", KR(rollback_ret));
          }
        } else {
          int commit_ret = guard->commit();
          if (OB_SUCCESS != commit_ret) {
            LOG_ERROR("fail to commit transaction", KR(commit_ret));
            ret = commit_ret;
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
      (void) throttle_(ret, ObTimeUtility::current_time() - start_time);
      if (OB_SUCCESS != (tmp_ret = reput_to_queue_(tasks))) {
        LOG_ERROR("fail to reput update task to queue", KR(tmp_ret), K(tasks.count()));
      } else {
        LOG_TRACE("reput update task to queue success", K(tasks.count()));
      }
    }
  }
  LOG_TRACE("RUNTIME_META: batch update tablets finished", KR(ret), K(tablet_infos.count()), K(tasks),
      "cost_time", ObTimeUtility::current_time() - batch_update_start_time);
  return ret;
}

int ObTabletRuntimeMetaUpdater::throttle_(
    const int return_code,
    const int64_t execute_time_us)
{
  int ret = OB_SUCCESS;
  int64_t sleep_us = 0;
  if (OB_SUCCESS != return_code) {
    sleep_us = 2l * 1000 * 1000; // 2s
  } else if (execute_time_us > 20 * 1000 * 1000) { // 20s
    sleep_us = MIN(1L * 1000 * 1000, (execute_time_us - 20 * 1000 * 1000));
    LOG_WARN("detected slow update, may be too many concurrent updating", K(sleep_us));
  }
  const static int64_t sleep_step_us = 20 * 1000; // 20ms
  for (; !ATOMIC_LOAD(&is_stop_) && sleep_us > 0;
      sleep_us -= sleep_step_us) {
    ob_usleep(static_cast<int32_t>(std::min(sleep_step_us, sleep_us)), true /*is_idle_sleep*/);
  }
  return ret;
}

int64_t ObTabletRuntimeMetaUpdateTaskQueue::task_count() const
{
  if (GCTX.in_bootstrap_) {
    return 0;
  } else {
    return ObUniqTaskQueue<ObTabletRuntimeMetaUpdateTask, ObTabletRuntimeMetaUpdater>::task_count();
  }
}

} // end namespace observer
} // end namespace oceanbase
