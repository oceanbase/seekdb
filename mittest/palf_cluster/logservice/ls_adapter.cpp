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

#include "ls_adapter.h"
#include "logservice/replayservice/ob_replay_status.h"

namespace oceanbase
{
namespace palfcluster
{
MockLSAdapter::MockLSAdapter() :
    is_inited_(false)
  {}

MockLSAdapter::~MockLSAdapter()
{
  destroy();
}

int MockLSAdapter::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    CLOG_LOG(WARN, "MockLSAdapter init twice", K(ret));
  } else {
    is_inited_ = true;
    CLOG_LOG(INFO, "MockLSAdapter init success", K(ret));
  }
  return ret;
}

void MockLSAdapter::destroy()
{
  is_inited_ = false;
}

int MockLSAdapter::replay(logservice::ObLogReplayTask *replay_task)
{
  int ret = OB_SUCCESS;
  return ret;
}

int MockLSAdapter::wait_append_sync(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  return ret;
}

} // end namespace palfcluster
} // end namespace oceanbase
