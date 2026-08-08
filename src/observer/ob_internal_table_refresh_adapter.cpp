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

#include "observer/ob_internal_table_refresh_adapter.h"

#include "observer/omt/ob_srs_service.h"
#include "share/ob_timezone_mgr.h"

namespace oceanbase
{
namespace observer
{

int ObInternalTableRefreshAdapter::init(
    omt::ObTimezoneMgr &timezone_mgr,
    omt::ObSrsService &srs_service)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(timezone_mgr_) || OB_NOT_NULL(srs_service_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("internal table refresh adapter initialized twice", KR(ret));
  } else {
    timezone_mgr_ = &timezone_mgr;
    srs_service_ = &srs_service;
  }
  return ret;
}

void ObInternalTableRefreshAdapter::reset()
{
  timezone_mgr_ = nullptr;
  srs_service_ = nullptr;
}

int ObInternalTableRefreshAdapter::activate()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(timezone_mgr_) || OB_ISNULL(srs_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("internal table refresh adapter is not initialized", KR(ret));
  } else {
    srs_service_->mark_stale();
    if (OB_FAIL(timezone_mgr_->schedule_retry())) {
      LOG_WARN("failed to schedule timezone refresh", KR(ret));
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
