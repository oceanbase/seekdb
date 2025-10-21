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

#define USING_LOG_PREFIX SERVER

#include "ob_construct_queue.h"
#include "ob_mysql_request_manager.h"

using namespace oceanbase::obmysql;

ObConstructQueueTask::ObConstructQueueTask()
    :request_manager_(NULL),
    is_tp_trigger_(false)
{

}

ObConstructQueueTask::~ObConstructQueueTask()
{
  request_manager_ = NULL;
  is_tp_trigger_ = false;
}

int ObConstructQueueTask::init(const ObMySQLRequestManager *request_manager)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(request_manager)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(request_manager_), K(ret));
  } else {
    request_manager_ = const_cast<ObMySQLRequestManager*>(request_manager);
    disable_timeout_check();
  }
  return ret;
}

void ObConstructQueueTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  // for testing hung scene
  int64_t code = 0;
  code = OB_E(EventTable::EN_SQL_AUDIT_CONSTRUCT_BACK_THREAD_STUCK) OB_SUCCESS;
  if (OB_UNLIKELY(OB_SUCCESS != code && is_tp_trigger_)) {
    sleep(abs(code));
    LOG_INFO("Construct sleep", K(abs(code)));
    is_tp_trigger_ = false;
  } else if (OB_SUCCESS == code) {
    is_tp_trigger_ = true;
  }

  if (OB_ISNULL(request_manager_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(request_manager_), K(ret));
  } else if (OB_FAIL(request_manager_->get_queue().prepare_alloc_queue())) {
    LOG_WARN("fail to prepare alloc queue", K(ret));
  }
}
