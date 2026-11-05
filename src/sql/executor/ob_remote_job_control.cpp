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

#include "sql/executor/ob_remote_job_control.h"
using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

ObRemoteJobControl::ObRemoteJobControl()
{
}

ObRemoteJobControl::~ObRemoteJobControl()
{
}

int ObRemoteJobControl::get_ready_jobs(ObIArray<ObJob*> &jobs, bool serial_sched) const
{
  int ret = OB_SUCCESS;
  UNUSED(serial_sched);
  for (int64_t i = 0; OB_SUCC(ret) && i < jobs_.count(); ++i) {
    ObJob *job = jobs_.at(i);
    if (OB_ISNULL(job)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("job is NULL", K(ret));
    } else if (OB_JOB_STATE_INITED == job->get_state()) {
      if (OB_FAIL(jobs.push_back(job))) {
        LOG_WARN("fail to push back job", K(ret), K(i));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (2 != jobs.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("the count of ready jobs is not 2", K(jobs.count()));
  }
  return ret;
}
}/* ns sql*/
}/* ns oceanbase */
