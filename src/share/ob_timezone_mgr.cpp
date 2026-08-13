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

#define USING_LOG_PREFIX SERVER_OMT
#include "ob_timezone_mgr.h"

using namespace oceanbase::common;


namespace oceanbase {
namespace omt {

void ObTimezoneMgr::UpdateTimezoneTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(timezone_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("time zone manager is null", K(ret));
  } else if (OB_FAIL(timezone_mgr_->refresh_timezone_info())) {
    LOG_WARN("update time zone failed", K(ret));
  }
}

ObTimezoneMgr::ObTimezoneMgr()
    : is_inited_(false),
      update_task_(this),
      timer_(),
      usable_(false)
{
}

ObTimezoneMgr::~ObTimezoneMgr()
{
}

ObTimezoneMgr &ObTimezoneMgr::get_instance()
{
  static ObTimezoneMgr ob_timezone_mgr;
  return ob_timezone_mgr;
}

int ObTimezoneMgr::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  is_inited_ = true;
  if (OB_FAIL(init_timezone(sql_proxy))) {
    LOG_WARN("init timezone info failed", K(ret));
  } else if (OB_FAIL(timer_.init("TimezoneMgr", ObMemAttr("TimezoneMgr")))) {
    LOG_WARN("init timezone timer failed", K(ret));
  }
  return ret;
}

int ObTimezoneMgr::start()
{
  int ret = OB_SUCCESS;
  const int64_t delay = SLEEP_USECONDS;
  const bool repeat = true;
  const bool immediate = false;
  if (OB_FAIL(timer_.start())) {
    LOG_WARN("fail to start timer", K(ret));
  } else if (OB_FAIL(timer_.schedule(update_task_, delay, repeat, immediate))) {
    LOG_WARN("schedual time zone mgr failed", K(ret));
  }
  return ret;
}

void ObTimezoneMgr::stop()
{
  timer_.stop();
}

void ObTimezoneMgr::wait()
{
  timer_.wait();
}

void ObTimezoneMgr::destroy()
{
  timer_.destroy();
  ob_delete(tz_info_mgr_);
}

int ObTimezoneMgr::init_timezone(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (! is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("time zone manager is not initialized", K(ret));
  } else if (OB_NOT_NULL(tz_info_mgr_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("time zone manager is already initialized", K(ret));
  } else {
    tz_info_mgr_ = OB_NEW(ObTimeZoneInfoManager, "Timezone", sql_proxy);
    if (OB_ISNULL(tz_info_mgr_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate time zone manager failed", K(ret));
    } else if (OB_FAIL(tz_info_mgr_->init())) {
      LOG_WARN("initialize time zone manager failed", K(ret));
    }
    if (OB_FAIL(ret)) {
      ob_delete(tz_info_mgr_);
    }
  }
  return ret;
}

int ObTimezoneMgr::refresh_timezone_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tz_info_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("time zone manager is null", K(ret));
  } else if (OB_FAIL(tz_info_mgr_->fetch_time_zone_info())) {
    LOG_WARN("fail to update time zone info", K(ret));
  }
  return ret;
}

int ObTimezoneMgr::schedule_retry()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(timer_.schedule(update_task_, 1000000, false))) {
    LOG_WARN("schedule timezone retry timer failed", K(ret));
  } else {
    LOG_INFO("[TIMEZONE] retry timer scheduled");
  }
  return ret;
}

int ObTimezoneMgr::get_timezone(
    ObTZMapWrap &timezone_wrap,
    ObTimeZoneInfoManager *&tz_info_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tz_info_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("time zone manager is null", K(ret));
  } else {
    timezone_wrap.set_tz_map(tz_info_mgr_->get_tz_info_map());
    tz_info_mgr = tz_info_mgr_;
  }
  return ret;
}

int ObTimezoneMgr::get_timezone_map(ObTZMapWrap &timezone_wrap)
{
  ObTimeZoneInfoManager *tz_info_mgr = NULL;
  return get_timezone(timezone_wrap, tz_info_mgr);
}

} //omt
} //oceanbase
