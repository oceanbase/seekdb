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

#ifndef OCEANBASE_OBSERVER_OB_TABLET_RUNTIME_META_UPDATER_H_
#define OCEANBASE_OBSERVER_OB_TABLET_RUNTIME_META_UPDATER_H_

#include "observer/ob_uniq_task_queue.h"   // for ObIUniqTaskQueueTask
#include "common/ob_tablet_id.h"           // for ObTablet

namespace oceanbase
{
namespace share
{
class ObTabletRuntimeInfo;
struct ObTabletLocalChecksumItem;
}
namespace observer
{
class ObTabletRuntimeMetaUpdater;
struct TSITabletRuntimeMetaUpdateStatistics
{
public:
  TSITabletRuntimeMetaUpdateStatistics() { reset(); }
  void reset();
  void calc(int64_t succ_cnt,
            int64_t fail_cnt,
            int64_t remove_task_cnt,
            int64_t update_task_cnt,
            int64_t wait_us,
            int64_t exec_us);
  void dump();
private:
  int64_t suc_cnt_;
  int64_t fail_cnt_;
  int64_t remove_task_cnt_;
  int64_t update_task_cnt_;
  int64_t total_wait_us_;
  int64_t total_exec_us_;
};
class ObTabletRuntimeMetaUpdateTask : public ObIUniqTaskQueueTask<ObTabletRuntimeMetaUpdateTask>
{
public:
  friend class ObTabletRuntimeMetaUpdater;

  ObTabletRuntimeMetaUpdateTask()
      : need_diagnose_(false),
        tablet_id_(),
        add_timestamp_(OB_INVALID_TIMESTAMP),
        start_timestamp_(OB_INVALID_TIMESTAMP) {}
  explicit ObTabletRuntimeMetaUpdateTask(
      const common::ObTabletID &tablet_id,
      const int64_t add_timestamp)
      : need_diagnose_(false),
        tablet_id_(tablet_id),
        add_timestamp_(add_timestamp),
        start_timestamp_(OB_INVALID_TIMESTAMP) {}
  explicit ObTabletRuntimeMetaUpdateTask(
      const common::ObTabletID &tablet_id)
      : need_diagnose_(false),
        tablet_id_(tablet_id),
        add_timestamp_(OB_INVALID_TIMESTAMP),
        start_timestamp_(OB_INVALID_TIMESTAMP) {}
  virtual ~ObTabletRuntimeMetaUpdateTask();
  int init(
      const common::ObTabletID &tablet_id,
      const int64_t add_timestamp,
      const bool need_diagnose = false);
  void reset();
  // operator-related functions for ObTabletRuntimeMetaUpdateTask
  bool is_valid() const;
  void check_task_status() const;
  int assign(const ObTabletRuntimeMetaUpdateTask &other);
  virtual bool operator==(const ObTabletRuntimeMetaUpdateTask &other) const;
  virtual void set_start_timestamp() override;
  virtual int64_t get_start_timestamp() const override;
  virtual bool need_diagnose() const override { return need_diagnose_; }

  // get-related functions for member in ObTabletRuntimeMetaUpdateTask
  inline const common::ObTabletID &get_tablet_id() const { return tablet_id_; }
  inline int64_t get_add_timestamp() const { return add_timestamp_; }

  // other functions
  bool need_process_alone() const { return false; }
  uint64_t get_group_id() const { return tablet_id_.id(); }
  int64_t hash() const;
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; };
  bool compare_without_version(
      const ObTabletRuntimeMetaUpdateTask &other) const;
  // TODO: need to realize barrier related functions
  bool is_barrier() const;

  TO_STRING_KV(K_(tablet_id), K_(need_diagnose), K_(add_timestamp), K_(start_timestamp));
private:
  const int64_t TABLET_CHECK_INTERVAL = 2 * 3600 * 1000L * 1000L; //2 hour
  bool need_diagnose_; // task for compaction need diagnose
  common::ObTabletID tablet_id_;
  int64_t add_timestamp_;
  int64_t start_timestamp_;
};

class ObTabletRuntimeMetaUpdateTaskQueue : public ObUniqTaskQueue<ObTabletRuntimeMetaUpdateTask, ObTabletRuntimeMetaUpdater>
{
public:
  ObTabletRuntimeMetaUpdateTaskQueue() : ObUniqTaskQueue<ObTabletRuntimeMetaUpdateTask, ObTabletRuntimeMetaUpdater>() {}
  virtual ~ObTabletRuntimeMetaUpdateTaskQueue() {}
  virtual int64_t task_count() const override;
};

typedef ObArray<ObTabletRuntimeMetaUpdateTask> UpdateTaskList;
typedef ObArray<ObTabletRuntimeMetaUpdateTask> RemoveTaskList;

class ObTabletRuntimeMetaUpdater
{
public:
  ObTabletRuntimeMetaUpdater()
      : is_inited_(false),
        is_stop_(true),
        update_queue_() {}
  virtual ~ObTabletRuntimeMetaUpdater() { destroy(); }
  static int server_module_init(ObTabletRuntimeMetaUpdater *&tablet_runtime_meta_updater);
  int init();
  inline bool is_inited() const { return is_inited_; }
  void stop();
  void wait();
  void destroy();

  int submit_update_task(
      const common::ObTabletID &tablet_id,
      const bool need_diagnose = false);

  // async update tablets - add task to queue
  // @param [in] tablet_id, to report which tablet
  int async_update(
      const common::ObTabletID &tablet_id,
      const bool need_diagnose = false);

  // batch_process_tasks - divide tasks into different group, and do reput when failed
  // @parma [in] tasks, tasks to process
  // @param [in] stopped, whether this process is working or stopped
  int batch_process_tasks(
      const common::ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks,
      bool &stopped);

  int64_t get_tablet_runtime_meta_updater_update_queue_size() const { return update_queue_.task_count(); }
  // TODO: need to realize barrier related functions
  int process_barrier(const ObTabletRuntimeMetaUpdateTask &task, bool &stopped);
  int set_thread_count();
  // for diagnose
  int check_exist(
      const common::ObTabletID &tablet_id,
      bool &exist);
  int check_processing_exist(
      const common::ObTabletID &tablet_id,
      bool &exist);
  int diagnose_existing_task(
      ObIArray<ObTabletRuntimeMetaUpdateTask> &waiting_tasks,
      ObIArray<ObTabletRuntimeMetaUpdateTask> &processing_tasks);
private:
  int64_t cal_thread_count_();
  void diagnose_batch_tasks_(
      const ObIArray<ObTabletRuntimeMetaUpdateTask> &batch_tasks,
      const int error_code);

  // generate_tasks_ - split batch_tasks into update_tasks and remove_tasks
  // @parma [in] batch_tasks, input tasks
  // @parma [out] update_tablet_infos, generated update tablet_infos
  // @parma [out] remove_tablet_infos, generated remove tablet_infos
  // @parma [out] update_tablet_checksums, generated update tablet checksums
  // @parma [out] update_tablet_tasks, generated update tasks
  // @parma [out] remove_tablet_tasks, generated remove tasks
  int generate_tasks_(
      const ObIArray<ObTabletRuntimeMetaUpdateTask> &batch_tasks,
      ObArray<share::ObTabletRuntimeInfo> &update_tablet_infos,
      ObArray<share::ObTabletRuntimeInfo> &remove_tablet_infos,
      ObArray<share::ObTabletLocalChecksumItem> &update_tablet_checksums,
      UpdateTaskList &update_tablet_tasks,
      RemoveTaskList &remove_tablet_tasks);

  // do_batch_update - the real action to update a batch of tasks
  // @parma [in] start_time, the time to start this execution
  // @parma [in] tasks, batch of tasks to execute
  // @parma [in] tablet_infos, related tablet_info to each task
  // @parma [in] checksums, related checksum to each task
  int do_batch_update_(
      const int64_t start_time,
      const ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks,
      const ObIArray<share::ObTabletRuntimeInfo> &tablet_infos,
      const ObIArray<share::ObTabletLocalChecksumItem> &checksums);

  // do_batch_remove - the real action to remove a batch of tasks
  // @parma [in] start_time, the time to start this execution
  // @parma [in] tasks, batch of tasks to execute
  // @parma [in] tablet_infos, related tablet_info to each task
  int do_batch_remove_(
      const int64_t start_time,
      const ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks,
      const ObIArray<share::ObTabletRuntimeInfo> &tablet_infos);

  // add_update_task - add a task to task_queue
  // @parma [in] task, task to add
  int add_task_(const ObTabletRuntimeMetaUpdateTask &task);

  // throttle - wait a certain time before reput task to queue
  // @param [in] return_code, pre-procedure's running result
  // @parma [in] execute_time_us, execute time of pre-procedure
  int throttle_(
      const int return_code,
      const int64_t execute_time_us);

  // reput_to_update_queue - reput tasks to update_queue when failure occurs
  // @param [in] tasks, tasks to reput to queue
  int reput_to_queue_(
    const common::ObIArray<ObTabletRuntimeMetaUpdateTask> &tasks);

  // push_task_info_ - add update / remove task to array
  int push_task_info_(
      const ObTabletRuntimeMetaUpdateTask &task,
      const share::ObTabletRuntimeInfo &tablet_info,
      ObArray<share::ObTabletRuntimeInfo> &tablet_infos,
      ObArray<ObTabletRuntimeMetaUpdateTask> &task_list);
private:
  const int64_t MINI_MODE_UPDATE_TASK_THREAD_CNT = 1;
  const int64_t MIN_UPDATE_TASK_THREAD_CNT = 2;
  const int64_t MAX_UPDATE_TASK_THREAD_CNT = 7;
  const double UPDATE_TASK_THREAD_RATIO = 0.2;
  const int64_t MINI_MODE_UPDATE_QUEUE_SIZE = 5 * 10000;
  const int64_t UPDATE_QUEUE_SIZE = 10 * 10000;
  const int64_t DIAGNOSE_MAX_BATCH_COUNT = 3;
  bool is_inited_;
  bool is_stop_;
  ObTabletRuntimeMetaUpdateTaskQueue update_queue_;
  DISALLOW_COPY_AND_ASSIGN(ObTabletRuntimeMetaUpdater);
};

} // end namespace observer
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_TABLET_RUNTIME_META_UPDATER_H_
