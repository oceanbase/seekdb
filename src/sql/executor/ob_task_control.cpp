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

#define USING_LOG_PREFIX SQL_EXE

#include "ob_task_control.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;







ObTaskControl::ObTaskControl()
  : tasks_(),
//    is_scan_job_(false),
    is_root_job_(false),
    is_select_plan_(false)
{
}

ObTaskControl::~ObTaskControl()
{}

void ObTaskControl::reset()
{
  tasks_.reset();
//  is_scan_job_ = false;
  is_root_job_ = false;
  is_select_plan_ = false;
}

int ObTaskControl::find_task(uint64_t task_id, ObTaskInfo *&task) const
{
  int ret = OB_ENTRY_NOT_EXIST;
  for (int64_t i = 0; OB_ENTRY_NOT_EXIST == ret && i < tasks_.count(); ++i) {
    ObTaskInfo *task_info = tasks_.at(i);
    if (OB_ISNULL(task_info)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (task_info->get_task_location().get_task_id() == task_id) {
      task = task_info;
      ret = OB_SUCCESS;
    }
  }
  return ret;
}


int ObTaskControl::prepare(int64_t job_parallel_degree)
{
  int ret = OB_SUCCESS;
  if (!is_root_job_) {
    int64_t parallel_degree = MIN(tasks_.count(), job_parallel_degree);
    for (int64_t i = 0; OB_SUCC(ret) && i < parallel_degree; ++i) {
      ObTaskInfo *task = tasks_.at(i);
      if (OB_ISNULL(task)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("task is NULL", K(ret), K(tasks_.count()),
                  K(parallel_degree), K(job_parallel_degree));
      } else {
        task->set_state(OB_TASK_STATE_INITED);
      }
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tasks_.count(); i++) {
      ObTaskInfo *task = tasks_.at(i);
      if (OB_ISNULL(task)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task is NULL", K(ret));
      } else {
        ObTaskLocation dummy_task_loc;
        task->set_task_location(dummy_task_loc);
        task->set_state(OB_TASK_STATE_INITED);
      }
    }
  }
  return ret;
}

int ObTaskControl::get_ready_tasks(common::ObIArray<ObTaskInfo *> &tasks) const
{
  int ret = OB_SUCCESS;
  bool iter_end = true;
  const int64_t count = tasks_.count();
  tasks.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ObTaskInfo *task = tasks_.at(i);
    if (OB_ISNULL(task)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_TASK_STATE_NOT_INIT == task->get_state()) {
      iter_end = false;
    } else if (OB_TASK_STATE_INITED == task->get_state()) {
      iter_end = false;
      ret = tasks.push_back(tasks_.at(i));
    }
  }
  if (iter_end && OB_SUCC(ret)) {
    ret = OB_ITER_END;
  }
  return ret;
}






int ObTaskControl::get_all_tasks(common::ObIArray<ObTaskInfo *> &tasks) const
{
  int ret = OB_SUCCESS;
  tasks.reset();
  if (OB_FAIL(tasks.assign(tasks_))) {
    LOG_WARN("fail to assign task array", K(ret), K(tasks_.count()));
  }
  return ret;
}

int ObTaskControl::get_task_by_state(common::ObIArray<ObTaskInfo *> &tasks_out, int state) const
{
  int ret = OB_SUCCESS;
  const int64_t count = tasks_.count();
  tasks_out.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < count; i++) {
    if (state == tasks_.at(i)->get_state()) {
      ret = tasks_out.push_back(tasks_.at(i));
    }
  }
  return ret;
}
