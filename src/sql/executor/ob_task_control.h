/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
