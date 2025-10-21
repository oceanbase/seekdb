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

#ifndef OCEANBASE_ROOTSERVER_OB_ROOT_BALANCER_H_
#define OCEANBASE_ROOTSERVER_OB_ROOT_BALANCER_H_

#include "share/ob_define.h"
#include "rootserver/ob_rs_reentrant_thread.h"      // ObRsReentrantThread
#include "ob_thread_idling.h"                       // ObThreadIdling
#include "ob_unit_stat_manager.h"                   // ObUnitStatManager
#include "ob_server_balancer.h"                     // ObServerBalancer
#include "rootserver/ob_rootservice_util_checker.h" // ObRootServiceUtilChecker

namespace oceanbase
{
namespace common
{
class ObServerConfig;
}
namespace share
{
namespace schema
{
class ObMultiVersionSchemaService;
}
}
namespace rootserver
{
class ObUnitManager;
class ObServerManager;
class ObZoneManager;
class ObRootBalancer;

class ObRootBalanceIdling : public ObThreadIdling
{
public:
  explicit ObRootBalanceIdling(volatile bool &stop, const ObRootBalancer &host)
    : ObThreadIdling(stop), host_(host) {}

  virtual int64_t get_idle_interval_us();
private:
  const ObRootBalancer &host_;
};

class ObRootBalancer : public ObRsReentrantThread, public share::ObCheckStopProvider
{
public:
  ObRootBalancer();
  virtual ~ObRootBalancer();

  virtual void run3() override;
  virtual int blocking_run() { BLOCKING_RUN_IMPLEMENT(); }


  // main entry, never exit.
  virtual int do_balance();
  // do balance for all tenant
  virtual int all_balance();

  void stop();
  void wakeup();

  bool is_inited() const { return inited_; }
  bool is_stop() const { return stop_; }
  void set_active();
  // return OB_CANCELED if stop, else return OB_SUCCESS
  int check_stop() const;
  int idle() const;
  int64_t get_schedule_interval() const;
private:
  static const int64_t LOG_INTERVAL = 30 * 1000 * 1000;
private:
  bool inited_;
  volatile int64_t active_;
  mutable ObRootBalanceIdling idling_;
  ObServerBalancer server_balancer_;
  ObRootServiceUtilChecker rootservice_util_checker_;
};
} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_ROOT_BALANCER_H_
