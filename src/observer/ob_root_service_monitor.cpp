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

#include "ob_root_service_monitor.h"
#include "share/rc/ob_module_provider.h"

#include "rootserver/ob_root_service.h"
#include "logservice/ob_log_service.h"
#include "logservice/palf_handle_guard.h"
namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace rootserver;
using namespace storage;
namespace observer
{

void ObRootServiceMonitor::TimerTask::runTimerTask()
{
  monitor_.run_task();
}
ObRootServiceMonitor::ObRootServiceMonitor(ObRootService &root_service)
  : inited_(false),
    root_service_(root_service),
    fail_count_(0),
    timer_(),
    timer_task_(*this)
{
}

ObRootServiceMonitor::~ObRootServiceMonitor()
{
  if (inited_) {
    stop();
  }
}

int ObRootServiceMonitor::init()
{
  int ret = OB_SUCCESS;
  if (inited_) {
    ret = OB_INIT_TWICE;
    FLOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(timer_.init("RootSvcMonitor", ObMemAttr("RootSvcMon")))) {
    FLOG_WARN("init root service monitor timer failed", KR(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

void ObRootServiceMonitor::run_task()
{
  int ret = OB_SUCCESS;
  ObRSThreadFlag rs_work;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else {
    if (OB_FAIL(monitor_root_service())) {
      FLOG_WARN("monitor root service failed", KR(ret));
    }
  }
}

int ObRootServiceMonitor::start()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else if (OB_FAIL(timer_.schedule(
      timer_task_, MONITOR_ROOT_SERVICE_INTERVAL_US, true/*repeat*/, false/*immediate*/))) {
    FLOG_WARN("failed to schedule root service monitor timer task", K(ret));
  }
  return ret;
}

void ObRootServiceMonitor::stop()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else {
    timer_.stop();
  }
}

void ObRootServiceMonitor::wait()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else {
    timer_.wait();
  }
}


int ObRootServiceMonitor::monitor_root_service()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else {
    
    MOD_SCOPE {
      if (root_service_.is_stopping()) {
        //need exit
        if (OB_FAIL(root_service_.stop_service())) {
          FLOG_WARN("root_service stop_service failed", KR(ret));
        }
      } else if (root_service_.is_need_stop()) {
        FLOG_INFO("root service is starting, stop_service need wait");
      } else if (root_service_.in_service()) {
        // already started or is starting
      } else if (!root_service_.can_start_service()) {
        LOG_ERROR("bug here. root service can not start service");
      } else {
        DEBUG_SYNC(BEFORE_START_RS);
        if (OB_FAIL(try_start_root_service())) {
          FLOG_WARN("fail to start root_service", KR(ret));
        }
      }
    } else {
      if (OB_TENANT_NOT_IN_SERVER == ret) {
        ret = OB_SUCCESS;
      } else {
        FLOG_WARN("fail to get tenant", KR(ret));
      }
    }
  }
  return ret;
}

int ObRootServiceMonitor::try_start_root_service()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("try start root service begin");
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    FLOG_WARN("ObRootServiceMonitor is not inited", KR(ret));
  } else if (OB_FAIL(root_service_.start_service())) {
    FLOG_WARN("root_service start_service failed", KR(ret));
  }
  FLOG_INFO("try start root service finish", KR(ret));
  return ret;
}

}//end namespace observer
}//end namespace oceanbase
