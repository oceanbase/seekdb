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

#ifndef OCEANBASE_OBSERVER_OB_VECTOR_INDEX_ASYNC_TASK_H_
#define OCEANBASE_OBSERVER_OB_VECTOR_INDEX_ASYNC_TASK_H_

#include "query/vector/ob_vector_query_result.h"
#include "observer/vector_index/ob_vector_index_async_task_util.h"
#include "query/vector/ob_vector_index_adaptor.h"
#include "observer/vector_index/ob_vector_index_i_task_executor.h"

namespace oceanbase
{
namespace share
{
// Schedule HNSW vector index tasks for an LS.
class ObPluginVectorIndexMgr;
class ObVecAsyncTaskExector : public ObVecITaskExecutor
{
public:
  ObVecAsyncTaskExector()
    : ObVecITaskExecutor()
  {}
  virtual ~ObVecAsyncTaskExector() {}
  int load_task(uint64_t &task_trace_base_num) override;
  int check_and_set_thread_pool() override;
};

class ObVecTaskManager
{
public:
  ObVecTaskManager(int64_t index_table_id, ObVecIndexAsyncTaskType task_type)
      : index_table_id_(index_table_id),
        task_type_(task_type),
        task_ids_()
  {}
  ~ObVecTaskManager() {}
  int process_task();
  int create_task();
  int check_task_status();
  TO_STRING_KV(K_(index_table_id), K_(task_type), K_(task_ids));

private:
  int64_t index_table_id_;
  ObVecIndexAsyncTaskType task_type_;
  ObSEArray<int64_t, 4> task_ids_;
};

/**
 * the task for clear vec history task in __all_vector_index_task_history
*/
class ObVectorIndexHistoryTask : public common::ObTimerTask
{
public:
  ObVectorIndexHistoryTask()
  : sql_proxy_(nullptr),
    is_inited_(false),
    is_paused_(false)
  {}
  ~ObVectorIndexHistoryTask() {}
  int init(common::ObMySQLProxy &sql_proxy);
  virtual void runTimerTask() override;
  void destroy() {}
  void pause();
  void resume();
  void do_work();

  static const int64_t OB_VEC_INDEX_TASK_HISTORY_SAVE_TIME_US = 7 * 24 * 60 * 60 * 1000 * 1000ll; // 7 day
  static const int64_t OB_VEC_INDEX_TASK_MOVE_BATCH_SIZE = 1024L;
  static const int64_t OB_VEC_INDEX_TASK_DEL_COUNT_PER_TASK = 4096L;

private:
  int clear_history_task();
  int move_task_to_history_table();

private:
  common::ObMySQLProxy *sql_proxy_;
  bool is_inited_;
  bool is_paused_;
};

class ObVecAsyncTaskScheduler
{
public:
  static const int64_t VEC_INDEX_CLEAR_TASK_PERIOD = 10 * 1000L * 1000L; // 10s
  explicit ObVecAsyncTaskScheduler()
    : is_inited_(false),
      timer_(),
      vec_history_task_()
  {}

  virtual ~ObVecAsyncTaskScheduler() {}
  int init(ObMySQLProxy &sql_proxy);
  int start();
  void wait();
  void stop();
  void destroy();
  void resume();
  void pause();
private:
  bool is_inited_;

  common::ObTimer timer_;
  ObVectorIndexHistoryTask vec_history_task_;
};

} // end namespace share
} // end namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OB_VECTOR_INDEX_ASYNC_TASK_H_ */
