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

#ifndef OCEANBASE_OBSERVER_OB_ROOT_SERVICE_MONITOR_H_
#define OCEANBASE_OBSERVER_OB_ROOT_SERVICE_MONITOR_H_

#include "share/ob_define.h"
#include "share/ob_thread_pool.h"
#include "lib/task/ob_timer_service.h"

namespace oceanbase
{
namespace rootserver
{
class ObRootService;
}
namespace share
{
class ObRsMgr;
}

namespace observer
{
class ObRootServiceMonitor
{
public:
  ObRootServiceMonitor(rootserver::ObRootService &root_service,
                       share::ObRsMgr &rs_mgr);
  virtual ~ObRootServiceMonitor();
  int init();
  void run_task();
  int start();
  void stop();
  void wait();
private:
  static const int64_t MONITOR_ROOT_SERVICE_INTERVAL_US = 10 * 1000;  //10ms

  int monitor_root_service();
  int try_start_root_service();
  int wait_rs_finish_start();

private:
  class TimerTask : public common::ObTimerTask
  {
  public:
    TimerTask(ObRootServiceMonitor &monitor) : monitor_(monitor)
    {}
    virtual ~TimerTask() = default;

    void runTimerTask() override;

  private:
    ObRootServiceMonitor &monitor_;
  };

private:
  bool inited_;
  rootserver::ObRootService &root_service_;
  int64_t fail_count_;
  share::ObRsMgr &rs_mgr_;

  TimerTask timer_task_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRootServiceMonitor);
};
}//end namespace observer
}//end namespace oceanbase
#endif //OCEANBASE_OBSERVER_OB_ROOT_SERVICE_MONITOR_H_
