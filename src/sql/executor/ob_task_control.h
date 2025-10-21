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

#ifndef OCEANBASE_SQL_EXECUTOR_TASK_CONTROL_
#define OCEANBASE_SQL_EXECUTOR_TASK_CONTROL_

#include "sql/executor/ob_task_info.h"
#include "lib/queue/ob_lighty_queue.h"
#include "lib/container/ob_array.h"

namespace oceanbase
{
namespace sql
{
class ObTaskEvent;

class ObTaskResult
{
public:
  ObTaskResult() : slice_events_(NULL) {}
  virtual ~ObTaskResult() {}
  void reset() { task_location_.reset(); slice_events_ = NULL; }
  bool is_valid() const { return task_location_.is_valid() && NULL != slice_events_; }
  const ObTaskLocation &get_task_location() const { return task_location_; }
  uint64_t get_task_id() const { return task_location_.get_task_id(); }

  TO_STRING_KV(K_(task_location), K_(slice_events));
protected:
  ObTaskLocation task_location_;
  const common::ObIArray<ObSliceEvent> *slice_events_;
};


// Base class of all kinds of job control
class ObTaskControl
{
public:
  ObTaskControl();
  virtual ~ObTaskControl();
  void reset();
  int add_task(ObTaskInfo *task) { return tasks_.push_back(task); }
  int64_t get_task_count() const { return tasks_.count(); }
  int find_task(uint64_t task_id, ObTaskInfo *&task) const;
  int prepare(int64_t job_parallel_degree);
  int get_ready_tasks(common::ObIArray<ObTaskInfo *> &tasks) const;
  int get_all_tasks(common::ObIArray<ObTaskInfo *> &tasks) const;
  void set_is_select_plan(bool is_select_plan) { is_select_plan_ = is_select_plan; }
  inline void set_root_job() { is_root_job_ = true; }
  inline bool is_root_job() const { return is_root_job_; }
  TO_STRING_KV("tasks", tasks_);
private:
  int get_task_by_state(common::ObIArray<ObTaskInfo *> &tasks, int state) const;
  // Array index converted to void* pointer that can be stored in queue
  inline void *id_to_ptr(uint64_t id)
  {
    // Since pushing a NULL pointer into the queue will cause an error, and array indices generally start from 0, therefore, we need to change it to start from 1
    return reinterpret_cast<void*>(id + 1);
  }
  // queueinextract void*pointerconvert toarrayindex
  // queueextract void* pointer converted to array index
  // queue retrieved void* pointer converted to array index
  inline uint64_t ptr_to_id(void *ptr)
  {
    // Pointer returns to start from 1, while array index starts from 0, therefore subtract 1
    return reinterpret_cast<uint64_t>(ptr) - 1;
  }
  DISALLOW_COPY_AND_ASSIGN(ObTaskControl);
private:
  common::ObSEArray<ObTaskInfo *, 2> tasks_;
  //  bool is_scan_job_;
  bool is_root_job_;
  // Used for sending the underlying scan_task
  // The plan must not contain insert update delete etc data modification operations
  // Just select/select for update. In this case, partition-level retry can be considered
  // 
  bool is_select_plan_;
};
}
}
#endif /* OCEANBASE_SQL_EXECUTOR_TASK_CONTROL_ */
//// end of header file
