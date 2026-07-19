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

#ifndef OCEANBASE_TENANT_TIMEZONE_MGR_H_
#define OCEANBASE_TENANT_TIMEZONE_MGR_H_

#include "lib/task/ob_timer.h"
#include "share/ob_lease_struct.h"
#include "share/rc/ob_context.h"
#include "share/ob_time_zone_info_manager.h"


namespace oceanbase {
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace omt {

class ObTenantTimezoneMgr
{
private:
  class UpdateTenantTZTask : public common::ObTimerTask
  {
  public:
    UpdateTenantTZTask(ObTenantTimezoneMgr *tenant_tz_mgr) : tenant_tz_mgr_(tenant_tz_mgr) {}
    virtual ~UpdateTenantTZTask() {}
    UpdateTenantTZTask(const UpdateTenantTZTask &) = delete;
    UpdateTenantTZTask &operator=(const UpdateTenantTZTask &) = delete;
    void runTimerTask(void) override;
    ObTenantTimezoneMgr *tenant_tz_mgr_;
  };
  friend UpdateTenantTZTask;
public:
  virtual ~ObTenantTimezoneMgr();
  ObTenantTimezoneMgr(const ObTenantTimezoneMgr &timezone) = delete;
  ObTenantTimezoneMgr & operator=(const ObTenantTimezoneMgr &) = delete;

  static ObTenantTimezoneMgr &get_instance();
  int init(common::ObMySQLProxy &sql_proxy);
  int start();

  // Return the tenant timezone map used by local SQL execution.
  int get_tenant_tz(common::ObTZMapWrap &timezone_wrap);
  int get_tenant_timezone(common::ObTZMapWrap &timezone_wrap,
                          common::ObTimeZoneInfoManager *&tz_info_mgr);
  bool is_inited() { return is_inited_; }
  bool is_usable() { return usable_; }
  void set_usable() { usable_ = true; }

  void stop();
  void wait();
  void destroy();

  int refresh_timezone_info();
  int schedule_retry();
private:
  int init_timezone(common::ObMySQLProxy &sql_proxy);
private:
  ObTenantTimezoneMgr();
  bool is_inited_;
  UpdateTenantTZTask update_task_;
  common::ObTimer timer_;
  common::ObTimeZoneInfoManager *tz_info_mgr_ = nullptr;
  bool usable_;
public:
  const uint64_t SLEEP_USECONDS = 5000000;
};

} // omt
} // oceanbase

#define OTTZ_MGR (::oceanbase::omt::ObTenantTimezoneMgr::get_instance())

#endif
