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
#include "share/ob_internal_table_change_notifier.h"
#include "share/ob_server_struct.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"

using namespace oceanbase::common;


namespace oceanbase {
namespace omt {

void ObTimezoneMgr::UpdateTimezoneTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(timezone_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("time zone manager is null", K(ret));
  } else if (OB_FAIL(timezone_mgr_->refresh_timezone_info_if_changed_())) {
    LOG_WARN("update time zone failed", K(ret));
  }
}

ObTimezoneMgr::ObTimezoneMgr()
    : is_inited_(false), update_task_(this), timer_(), usable_(false),
      table_change_watcher_() {}

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
  } else if (OB_FAIL(timer_.init("TimezoneMgr", ObMemAttr("TimezoneMgr")))) {
    LOG_WARN("init timezone timer failed", K(ret));
  } else if (OB_FAIL(table_change_watcher_.init_watched_tables(
                 {share::OB_ALL_SYS_STAT_TID,
                  share::OB_ALL_TIME_ZONE_NAME_TID,
                  share::OB_ALL_TIME_ZONE_TRANSITION_TID,
                  share::OB_ALL_TIME_ZONE_TRANSITION_TYPE_TID}))) {
    LOG_WARN("register timezone table change tracking failed", K(ret));
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
  } else if (OB_FAIL(timer_.schedule(update_task_, delay, repeat, immediate))) {
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
  } else if (OB_NOT_NULL(tz_info_mgr_)) {
    ret = OB_INIT_TWICE;
  } else {
    tz_info_mgr_ = OB_NEW(ObTimeZoneInfoManager, "Timezone", sql_proxy);
    if (OB_ISNULL(tz_info_mgr_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(tz_info_mgr_->init())) {
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
  } else if (OB_FAIL(tz_info_mgr_->fetch_time_zone_info())) {
  }
  return ret;
}

int ObTimezoneMgr::schedule_retry()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(timer_.schedule(update_task_, 1000000, false))) {
  } else {
    LOG_INFO("[TIMEZONE] retry timer scheduled");
  }
  return ret;
}

int ObTimezoneMgr::refresh_timezone_info_if_changed_()
{
  int ret = OB_SUCCESS;
  share::InternalTableChangeWatcher::CheckResult check_result;
  if (OB_FAIL(table_change_watcher_.check_changed(check_result))) {
    LOG_WARN("fail to check timezone table change, defer refresh", K(ret));
  } else if (share::InternalTableChangeWatcher::NO_CHANGE == check_result) {
    // The timer remains active, but unchanged tables need no inner SQL.
  } else if (share::InternalTableChangeWatcher::CHANGE_NOT_VISIBLE ==
             check_result) {
    LOG_DEBUG("defer timezone refresh until GTS reaches commit version");
  } else if (OB_FAIL(refresh_timezone_info())) {
    LOG_WARN("refresh changed timezone tables failed", K(ret));
  } else if (OB_FAIL(table_change_watcher_.mark_refreshed())) {
    LOG_WARN("failed to acknowledge refreshed timezone tables", K(ret));
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
