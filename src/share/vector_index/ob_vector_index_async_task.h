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

#ifndef OCEANBASE_OBSERVER_OB_VECTOR_INDEX_ASYNC_TASK_DEFINE_H_
#define OCEANBASE_OBSERVER_OB_VECTOR_INDEX_ASYNC_TASK_DEFINE_H_

#include "share/vector_index/ob_vector_index_async_task_util.h"
#include "share/vector_index/ob_plugin_vector_index_adaptor.h"
#include "share/vector_index/ob_vector_index_i_task_executor.h"

namespace oceanbase 
{
namespace share 
{
// schedule vector tasks for a ls
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
private:  
  bool check_operation_allow() override;
};

class ObVecTaskManager
{
public:
  ObVecTaskManager(uint64_t tenant_id, int64_t index_table_id, ObVecIndexAsyncTaskType task_type)
      : tenant_id_(tenant_id),
        index_table_id_(index_table_id),
        task_type_(task_type),
        task_ids_()
  {}
  ~ObVecTaskManager() {}
  int process_task();
  int create_task();
  int check_task_status();
  TO_STRING_KV(K_(tenant_id), K_(index_table_id), K_(task_type), K_(task_ids));
private:
  uint64_t tenant_id_;
  int64_t index_table_id_;
  ObVecIndexAsyncTaskType task_type_;
  ObSEArray<int64_t, 4> task_ids_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_VECTOR_INDEX_ASYNC_TASK_DEFINE_H_
