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
#include "ob_remote_fetch_context.h"

namespace oceanbase
{
namespace logservice
{

ObRemoteFetchContext &ObRemoteFetchContext::operator=(const ObRemoteFetchContext &other)
{
  issue_task_num_ = other.issue_task_num_;
  issue_version_ = other.issue_version_;
  last_fetch_ts_ = other.last_fetch_ts_;
  max_submit_lsn_ = other.max_submit_lsn_;
  max_fetch_lsn_ = other.max_fetch_lsn_;
  max_fetch_scn_ = other.max_fetch_scn_;
  error_context_ = other.error_context_;
  return *this;
}

void ObRemoteFetchContext::reset()
{
  issue_task_num_ = 0;
  issue_version_ = OB_INVALID_TIMESTAMP;
  last_fetch_ts_ = OB_INVALID_TIMESTAMP;
  max_submit_lsn_.reset();
  max_fetch_lsn_.reset();
  max_fetch_scn_.reset();
  error_context_.reset();
  (void)reset_sorted_tasks();
  submit_array_.reset();
}

int ObRemoteFetchContext::reset_sorted_tasks()
{
  int ret = OB_SUCCESS;
  while (! submit_array_.empty() && OB_SUCC(ret)) {
    ObFetchLogTask *task = NULL;
    if (OB_FAIL(submit_array_.pop_back(task))) {
      CLOG_LOG(ERROR, "pop failed", K(ret));
    } else {
      task->reset();
      share::mtl_free(task);
    }
  }
  return ret;
}

void ObRemoteFetchContext::set_issue_version()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_INVALID_TIMESTAMP != issue_version_)) {
    ret = OB_ERR_UNEXPECTED;
    CLOG_LOG(ERROR, "issue_version is valid", KPC(this));
  } else {
    issue_version_ = common::ObTimeUtility::current_time_ns();
  }
}
} // namespace logservice
} // namespace oceanbase
