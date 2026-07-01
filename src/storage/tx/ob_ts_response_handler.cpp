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

#define USING_LOG_PREFIX TRANS

#include "ob_ts_response_handler.h"
#include "ob_ts_mgr.h"

using namespace oceanbase::transaction;
using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::obcall;

int64_t ObTsResponseTaskFactory::alloc_count_;
int64_t ObTsResponseTaskFactory::free_count_;

void ObTsResponseHandler::reset()
{
  task_ = NULL;
  ts_mgr_ = NULL;
}

int ObTsResponseHandler::init(observer::ObSrvTask *task, ObTsMgr *ts_mgr)
{
  int ret = OB_SUCCESS;

  if (NULL == task || NULL == ts_mgr) {
    ret = OB_INVALID_ARGUMENT;;
    TRANS_LOG(WARN, "invalid argument", KR(ret), KP(task), KP(ts_mgr));
  } else {
    //Indicates that the task is a task generated internally by the Observer
    task_ = task;
    ts_mgr_ = ts_mgr;
  }

  return ret;
}

int ObTsResponseHandler::run()
{
  int ret = OB_SUCCESS;
  if (NULL == task_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "task is null, unexpected error", KR(ret), KP_(task));
  } else if (NULL == ts_mgr_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "ts mgr is null, unexpected error", KR(ret), KP_(ts_mgr));
  } else {
    ObTsResponseTask *task = static_cast<ObTsResponseTask *>(task_);
    if (OB_FAIL(ts_mgr_->handle_gts_result(task->get_arg1(), task->get_ts_type()))) {
    }
    //op_reclaim_free(task);
    //task = NULL;
  }
  return ret;
}

void ObTsResponseTask::reset()
{
  arg1_ = 0;
  handler_.reset();
  ts_type_ = TS_SOURCE_UNKNOWN;
}

int ObTsResponseTask::init(const int64_t arg1,
                           ObTsMgr *ts_mgr,
                           int ts_type)
{
  int ret = OB_SUCCESS;

  if (!true
      || NULL == ts_mgr
      || !is_valid_ts_source(ts_type)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(arg1), KP(ts_mgr), K(ts_type));
  } else if (OB_FAIL(handler_.init(this, ts_mgr))) {
  } else {
    //Different from the task of sql disconnection, it is used for memory release
    set_type(ObRequest::OB_TS_TASK);
    arg1_ = arg1;
    ts_type_ = ts_type;
  }

  return ret;
}

ObTsResponseTask *ObTsResponseTaskFactory::alloc()
{
  ObTsResponseTask *task = NULL;
  if (NULL != (task = op_reclaim_alloc(ObTsResponseTask))) {
    (void)ATOMIC_FAA(&alloc_count_, 1);
    alloc_count_++;
    if (REACH_TIME_INTERVAL(3 * 1000 * 1000)) {
    }
  }
  return task;
}

void ObTsResponseTaskFactory::free(ObTsResponseTask *task)
{
  if (NULL != task) {
    op_reclaim_free(task);
    task = NULL;
    (void)ATOMIC_FAA(&free_count_, 1);
  }
}

