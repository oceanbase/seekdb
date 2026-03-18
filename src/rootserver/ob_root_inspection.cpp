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

#define USING_LOG_PREFIX RS


#include "ob_root_inspection.h"
#include "rootserver/ob_root_service.h"
#include "share/ob_global_stat_proxy.h"//ObGlobalStatProxy

namespace oceanbase
{
using namespace common;
using namespace common::sqlclient;
using namespace share;
using namespace share::schema;
using namespace sql;
namespace rootserver
{
////////////////////////////////////////////////////////////////
ObPurgeRecyclebinTask::ObPurgeRecyclebinTask(ObRootService &rs)
    :ObAsyncTimerTask(rs.get_inspect_task_queue()),
    root_service_(rs)
{}

int ObPurgeRecyclebinTask::process()
{
  LOG_INFO("purge recyclebin task begin");
  int ret = OB_SUCCESS;
  const int64_t PURGE_EACH_TIME = 1000;
  int64_t delay = 1 * 60 * 1000 * 1000;
  int64_t expire_time = GCONF.recyclebin_object_expire_time;
  int64_t purge_interval = GCONF._recyclebin_object_purge_frequency;
  if (expire_time > 0 && purge_interval > 0) {
   if (OB_FAIL(root_service_.purge_recyclebin_objects(PURGE_EACH_TIME))) {
      LOG_WARN("fail to purge recyclebin objects", KR(ret));
    }
    delay = purge_interval;
  }
  // the error code is only for outputtion log, the function will return success.
  // the task no need retry, because it will be triggered periodically.
  if (OB_FAIL(root_service_.schedule_recyclebin_task(delay))) {
    // overwrite ret
    LOG_WARN("schedule purge recyclebin task failed", KR(ret), K(delay));
  } else {
    LOG_INFO("submit purge recyclebin task success", K(delay));
  }
  LOG_INFO("purge recyclebin task end", K(delay));
  return OB_SUCCESS;
}

ObAsyncTask *ObPurgeRecyclebinTask::deep_copy(char *buf, const int64_t buf_size) const
{
  ObPurgeRecyclebinTask *task = NULL;
  if (NULL == buf || buf_size < static_cast<int64_t>(sizeof(*this))) {
    LOG_WARN_RET(OB_BUF_NOT_ENOUGH, "buffer not large enough", K(buf_size));
  } else {
    task = new(buf) ObPurgeRecyclebinTask(root_service_);
  }
  return task;
}

}//end namespace rootserver
}//end namespace oceanbase
