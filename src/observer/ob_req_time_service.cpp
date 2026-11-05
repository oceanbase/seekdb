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

#include "ob_req_time_service.h"

namespace oceanbase
{
namespace observer
{
ObReqTimeInfo::ObReqTimeInfo()
  : start_time_(0), end_time_(0), reentrant_cnt_(0)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(ObGlobalReqTimeService::get_instance().add_req_time_info(this))) {
    SERVER_LOG(ERROR, "failed to add req time info to list", K(ret));
  }
}

ObReqTimeInfo::~ObReqTimeInfo()
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(ObGlobalReqTimeService::get_instance().rm_req_time_info(this))) {
    SERVER_LOG(ERROR, "failed to remove req time info from list", K(ret));
  }
  if (0 != reentrant_cnt_) {
    SERVER_LOG(ERROR, "invalid reentrant cnt", K(reentrant_cnt_));
  }
  start_time_ = 0;
  end_time_ = 0;
  reentrant_cnt_ = 0;
}

void ObGlobalReqTimeService::check_req_timeinfo()
{
#if !defined(NDEBUG)
  observer::ObReqTimeInfo &req_timeinfo =  observer::ObReqTimeInfo::get_thread_local_instance();

  OB_ASSERT(req_timeinfo.reentrant_cnt_ > 0);
#endif
}
} // end namespace server
} // end namesapce oceanbase

