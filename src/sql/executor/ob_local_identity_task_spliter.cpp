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

#include "sql/executor/ob_local_identity_task_spliter.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

ObLocalIdentityTaskSpliter::ObLocalIdentityTaskSpliter()
  : task_(NULL)
{
}

ObLocalIdentityTaskSpliter::~ObLocalIdentityTaskSpliter()
{
  if (NULL != task_) {
    task_->~ObTaskInfo();
    task_ = NULL;
  }
}

int ObLocalIdentityTaskSpliter::get_next_task(ObTaskInfo *&task)
{
  int ret = OB_SUCCESS;
  if (OB_I(t1) OB_UNLIKELY(OB_ISNULL(allocator_) || OB_ISNULL(job_))) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(allocator_), K(job_));
  } else if (NULL != task_) {
    ret = OB_ITER_END;
  } else {
    void *ptr = allocator_->alloc(sizeof(ObTaskInfo));
    if (OB_ISNULL(ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("fail allocate ObTaskInfo", K(ret));
    } else {
      task_ = new(ptr) ObTaskInfo(*allocator_);
      ObTaskID ob_task_id;
      ObTaskLocation task_loc;
      ob_task_id.set_ob_job_id(job_->get_ob_job_id());
      ob_task_id.set_task_id(0);
      task_loc.set_ob_task_id(ob_task_id);
      task_loc.set_server(server_);
      task_->set_task_split_type(get_type());
      task_->set_location_idx(0);
      task_->set_pull_slice_id(0);
      task_->set_task_location(task_loc);
      task_->set_state(OB_TASK_STATE_NOT_INIT);
      task_->set_root_spec(job_->get_root_spec());
      // The purpose of making task_ a class member is to ensure that the second call to get_next_task returns OB_ITER_END
      task = task_;
    }
  }
  return ret;
}


