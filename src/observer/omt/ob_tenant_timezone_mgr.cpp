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
#include "observer/ob_server.h"

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
    : allocator_("TenantTZ"), is_inited_(false), self_(), sql_proxy_(nullptr),
      rwlock_(ObLatchIds::TIMEZONE_LOCK),
      update_task_(this),
      usable_(false),
      schema_service_(nullptr)
{
  tenant_tz_map_getter_ = ObTenantTimezoneMgr::get_tenant_timezone_default;
}

ObTenantTimezoneMgr::~ObTenantTimezoneMgr()
{
}

ObTenantTimezoneMgr &ObTenantTimezoneMgr::get_instance()
{
  static ObTenantTimezoneMgr ob_tenant_timezone_mgr;
  return ob_tenant_timezone_mgr;
}

int ObTenantTimezoneMgr::init(ObMySQLProxy &sql_proxy, const ObAddr &server,
                              share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  sql_proxy_ = &sql_proxy;
  self_ = server;
  schema_service_ = &schema_service;
  is_inited_ = true;
  if (OB_FAIL(init_timezone())) {
    LOG_WARN("init timezone info failed", K(ret));
  } else {
    tenant_tz_map_getter_ = ObTenantTimezoneMgr::get_tenant_timezone_static;
  }
  return ret;
}

int ObTenantTimezoneMgr::start()
{
  int ret = OB_SUCCESS;
  const int64_t delay = SLEEP_USECONDS;
  const bool repeat = true;
  const bool immediate = true;
  if (OB_FAIL(TG_START(lib::TGDefIDs::TIMEZONE_MGR))) {
    LOG_WARN("fail to start timer", K(ret));
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::TIMEZONE_MGR, update_task_, delay / 1000000L, repeat, immediate))) {
    LOG_WARN("schedual time zone mgr failed", K(ret));
  }
  return ret;
}

void ObTenantTimezoneMgr::init(tenant_timezone_map_getter tz_map_getter)
{
  tenant_tz_map_getter_ = tz_map_getter;
  is_inited_ = true;
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
  TG_DESTROY(lib::TGDefIDs::TIMEZONE_MGR);
  ob_delete(timezone_);
}

int ObTenantTimezoneMgr::init_timezone()
{
  int ret = OB_SUCCESS;
  if (! is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant timezone mgr not inited", K(ret));
  } else if (OB_NOT_NULL(timezone_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tenant timezone already inited", K(ret));
  } else {
    timezone_ = OB_NEW(ObTenantTimezone, "TenantTZ", OBSERVER.get_mysql_proxy(), OB_SYS_TENANT_ID);
    if (OB_ISNULL(timezone_)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc new tenant timezone failed", K(ret));
    } else if (OB_FAIL(timezone_->init())) {
      LOG_WARN("new tenant timezone init failed", K(ret));
    }
    if (OB_FAIL(ret)) {
      ob_delete(timezone_);
    }
  }
  return ret;
}

int ObTenantTimezoneMgr::refresh_timezone_info()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(timezone_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant tz is null", K(ret));
  } else if (OB_FAIL(timezone_->get_tz_mgr().fetch_time_zone_info())) {
    LOG_WARN("fail to update time zone info", K(ret));
  }
  return ret;
}
int ObTenantTimezoneMgr::get_tenant_timezone(const uint64_t /*tenant_id*/,
                                             ObTZMapWrap &timezone_wrap,
                                             ObTimeZoneInfoManager *&tz_info_mgr)
{
  int ret = OB_SUCCESS;
  DRWLock::RDLockGuard guard(rwlock_);
  if (OB_ISNULL(timezone_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant tz is null", K(ret));
  } else {
    timezone_wrap.set_tz_map(timezone_->get_tz_map());
    tz_info_mgr = &(timezone_->get_tz_mgr());
  }
  return ret;
}

int ObTenantTimezoneMgr::get_tenant_timezone_static(const uint64_t tenant_id,
                                                        ObTZMapWrap &timezone_wrap)
{
   ObTimeZoneInfoManager *tz_info_mgr = NULL;
  return get_instance().get_tenant_timezone(tenant_id, timezone_wrap, tz_info_mgr);
}

int ObTenantTimezoneMgr::get_tenant_tz(const uint64_t tenant_id,
                                      ObTZMapWrap &timezone_wrap)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_tz_map_getter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant tz map getter is null", K(ret), K(tenant_id));
  } else if (OB_FAIL(tenant_tz_map_getter_(tenant_id, timezone_wrap))) {
    LOG_WARN("get tenant tz map failed", K(ret), K(tenant_id));
  }
  return ret;
}

int ObTenantTimezoneMgr::get_tenant_timezone_default(const uint64_t tenant_id,
                                                      ObTZMapWrap &timezone_wrap)
{
  int ret = OB_SUCCESS;
  static ObTZInfoMap tz_map;
  UNUSED(tenant_id);
  if (OB_UNLIKELY(! tz_map.is_inited()) &&
      OB_FAIL(tz_map.init(SET_USE_500("TzMapStatic")))) {
    LOG_WARN("init time zone info map failed", K(ret));
  } else {
    timezone_wrap.set_tz_map(&tz_map);
  }
  return ret;
}

} //omt
} //oceanbase
