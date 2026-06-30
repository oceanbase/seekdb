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
#include "ob_tenant_timezone_mgr.h"
#include "share/ob_thread_mgr.h"  // TG IDs, previously hidden behind a transitive include(free within share)
#include "share/rc/ob_module_provider.h"
#include "share/ob_internal_table_change_notifier.h"

using namespace oceanbase::common;


namespace oceanbase {
namespace omt {

void ObTenantTimezoneMgr::UpdateTenantTZTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_tz_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("update tenant task, tenant tz mgr is null", K(ret));
  } else if (OB_FAIL(tenant_tz_mgr_->refresh_timezone_info())) {
    LOG_WARN("update tenant time zone failed", K(ret));
  }
}

ObTenantTimezoneMgr::ObTenantTimezoneMgr()
    : is_inited_(false),
      update_task_(this),
      usable_(false)
{
}

ObTenantTimezoneMgr::~ObTenantTimezoneMgr()
{
}

ObTenantTimezoneMgr &ObTenantTimezoneMgr::get_instance()
{
  static ObTenantTimezoneMgr ob_tenant_timezone_mgr;
  return ob_tenant_timezone_mgr;
}

int ObTenantTimezoneMgr::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  is_inited_ = true;
  if (OB_FAIL(init_timezone(sql_proxy))) {
    LOG_WARN("init timezone info failed", K(ret));
  } else {
    // Register with notifier. Role-change-driven switch_to_leader will
    // trigger the initial refresh after LS promotion; import path triggers
    // via notify().
    share::ObInternalTableChangeNotifier::get_instance().register_module(
        table::ObModuleDataArg::TIMEZONE,
        []() -> int {
          LOG_INFO("[TIMEZONE_NOTIFIER] scheduling async refresh");
          OTTZ_MGR.schedule_retry();
          return OB_SUCCESS;
        });
  }
  return ret;
}

int ObTenantTimezoneMgr::start()
{
  int ret = OB_SUCCESS;
  const int64_t delay = SLEEP_USECONDS;
  const bool repeat = true;
  const bool immediate = false;
  if (OB_FAIL(TG_START(lib::TGDefIDs::TIMEZONE_MGR))) {
    LOG_WARN("fail to start timer", K(ret));
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::TIMEZONE_MGR, update_task_, delay, repeat, immediate))) {
    LOG_WARN("schedual time zone mgr failed", K(ret));
  }
  return ret;
}

void ObTenantTimezoneMgr::stop()
{
  TG_STOP(lib::TGDefIDs::TIMEZONE_MGR);
}

void ObTenantTimezoneMgr::wait()
{
  TG_WAIT(lib::TGDefIDs::TIMEZONE_MGR);
}

void ObTenantTimezoneMgr::destroy()
{
  ob_delete(tz_info_mgr_);
}

int ObTenantTimezoneMgr::init_timezone(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (! is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant timezone mgr not inited", K(ret));
  } else if (OB_NOT_NULL(tz_info_mgr_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tenant timezone already inited", K(ret));
  } else {
    tz_info_mgr_ = OB_NEW(ObTimeZoneInfoManager, "TenantTZ", sql_proxy);
    if (OB_ISNULL(tz_info_mgr_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc new tenant timezone failed", K(ret));
    } else if (OB_FAIL(tz_info_mgr_->init())) {
      LOG_WARN("new tenant timezone init failed", K(ret));
    }
    if (OB_FAIL(ret)) {
      ob_delete(tz_info_mgr_);
    }
  }
  return ret;
}

int ObTenantTimezoneMgr::refresh_timezone_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tz_info_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant tz is null", K(ret));
  } else if (OB_FAIL(tz_info_mgr_->fetch_time_zone_info())) {
    LOG_WARN("fail to update time zone info", K(ret));
  }
  return ret;
}

int ObTenantTimezoneMgr::schedule_retry()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::TIMEZONE_MGR,
                          update_task_, 1000000, false))) {
    LOG_WARN("schedule timezone retry timer failed", K(ret));
  } else {
    LOG_INFO("[TIMEZONE] retry timer scheduled");
  }
  return ret;
}

int ObTenantTimezoneMgr::get_tenant_timezone(
                                             ObTZMapWrap &timezone_wrap,
                                             ObTimeZoneInfoManager *&tz_info_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tz_info_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant tz is null", K(ret));
  } else {
    timezone_wrap.set_tz_map(tz_info_mgr_->get_tz_info_map());
    tz_info_mgr = tz_info_mgr_;
  }
  return ret;
}

int ObTenantTimezoneMgr::get_tenant_tz(ObTZMapWrap &timezone_wrap)
{
  ObTimeZoneInfoManager *tz_info_mgr = NULL;
  return get_tenant_timezone(timezone_wrap, tz_info_mgr);
}

} //omt
} //oceanbase
