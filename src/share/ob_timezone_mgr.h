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

#ifndef OCEANBASE_TIMEZONE_MGR_H_
#define OCEANBASE_TIMEZONE_MGR_H_

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

class ObTimezoneMgr
{
private:
  class UpdateTimezoneTask : public common::ObTimerTask
  {
  public:
    explicit UpdateTimezoneTask(ObTimezoneMgr *timezone_mgr) : timezone_mgr_(timezone_mgr) {}
    virtual ~UpdateTimezoneTask() {}
    UpdateTimezoneTask(const UpdateTimezoneTask &) = delete;
    UpdateTimezoneTask &operator=(const UpdateTimezoneTask &) = delete;
    void runTimerTask(void) override;
    ObTimezoneMgr *timezone_mgr_;
  };
  friend UpdateTimezoneTask;
public:
  virtual ~ObTimezoneMgr();
  ObTimezoneMgr(const ObTimezoneMgr &timezone) = delete;
  ObTimezoneMgr & operator=(const ObTimezoneMgr &) = delete;

  static ObTimezoneMgr &get_instance();
  int init(common::ObMySQLProxy &sql_proxy);
  int start();

  // observer and liboblog get time-zone map with the following function.
  int get_timezone_map(common::ObTZMapWrap &timezone_wrap);
  int get_timezone(common::ObTZMapWrap &timezone_wrap,
                   common::ObTimeZoneInfoManager *&tz_info_mgr);
  bool is_inited() { return is_inited_; }
  bool is_usable() { return usable_; }
  void set_usable() { usable_ = true; }

  void stop();
  void wait();
  void destroy();

  int refresh_timezone_info();
private:
  int init_timezone(common::ObMySQLProxy &sql_proxy);
  int refresh_timezone_info_if_changed_();
private:
  ObTimezoneMgr();
  bool is_inited_;
  UpdateTimezoneTask update_task_;
  common::ObTimer timer_;
  common::ObTimeZoneInfoManager *tz_info_mgr_ = nullptr;
  bool usable_;
  uint64_t sys_stat_change_seq_;
  uint64_t timezone_name_change_seq_;
  uint64_t timezone_transition_change_seq_;
  uint64_t timezone_transition_type_change_seq_;
public:
  const uint64_t SLEEP_USECONDS = 5000000;
};

} // omt
} // oceanbase

#define OTTZ_MGR (::oceanbase::omt::ObTimezoneMgr::get_instance())

#endif
