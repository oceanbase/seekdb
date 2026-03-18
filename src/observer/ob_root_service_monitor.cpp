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

#include "rootserver/ob_root_service.h"
#include "logservice/ob_log_service.h"
namespace oceanbase
{
using namespace common;
using namespace obrpc;
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
  } else if (OB_FAIL(TG_START(lib::TGDefIDs::RootServiceMonitor))) {
    FLOG_WARN("start root service monitor thread failed", KR(ret));
  } else if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::RootServiceMonitor, timer_task_,
                                 MONITOR_ROOT_SERVICE_INTERVAL_US, true/*repeat*/, false/*immediate*/))) {
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
    TG_STOP(lib::TGDefIDs::RootServiceMonitor);
  }
}

void ObRootServiceMonitor::wait()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else {
    TG_WAIT(lib::TGDefIDs::RootServiceMonitor);
  }
}


int ObRootServiceMonitor::monitor_root_service()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  } else {
    const uint64_t tenant_id = OB_SYS_TENANT_ID;
    MTL_SWITCH(tenant_id) {
      ObRole role = FOLLOWER;
      bool palf_exist = false;
      int64_t proposal_id = 0;  // unused
      palf::PalfHandleGuard palf_handle_guard;
      logservice::ObLogService *log_service = nullptr;
      if (OB_ISNULL(log_service = MTL(logservice::ObLogService*))) {
        ret = OB_ERR_UNEXPECTED;
        FLOG_WARN("MTL ObLogService is null", KR(ret), K(tenant_id));
      } else if (OB_FAIL(log_service->check_palf_exist(SYS_LS, palf_exist))) {
        FLOG_WARN("fail to check palf exist", KR(ret), K(tenant_id), K(SYS_LS));
      } else if (!palf_exist) {
        // bypass
      } else if (OB_FAIL(log_service->open_palf(SYS_LS, palf_handle_guard))) {
        FLOG_WARN("open palf failed", KR(ret), K(tenant_id), K(SYS_LS));
      } else if (OB_FAIL(palf_handle_guard.get_role(role, proposal_id))) {
        FLOG_WARN("get role failed", KR(ret), K(tenant_id));
      }
      if (OB_FAIL(ret)) {
      } else if (root_service_.is_stopping()) {
        //need exit
        if (OB_FAIL(root_service_.stop_service())) {
          FLOG_WARN("root_service stop_service failed", KR(ret));
        }
      } else if (root_service_.is_need_stop()) {
        FLOG_INFO("root service is starting, stop_service need wait");
      } else {
        if (is_strong_leader(role)) {
          if (root_service_.in_service()) {
            //already started or is starting
            //nothing todo
          } else if (!root_service_.can_start_service()) {
            LOG_ERROR("bug here. root service can not start service");
          } else {
            DEBUG_SYNC(BEFORE_START_RS);
            if (OB_FAIL(try_start_root_service())) {
              FLOG_WARN("fail to start root_service", KR(ret));
            }
          }
        } else {
          // possible follower or doesn't have role yet
          //DEBUG_SYNC(BEFORE_STOP_RS);
          //leader does not exist.
          if (!root_service_.is_start()) {
            //nothing todo
          } else {
            if (OB_FAIL(root_service_.revoke_rs())) {
              FLOG_WARN("fail to revoke rootservice", KR(ret));
              if (root_service_.is_need_stop()) {
                //nothing todo
              } else if (root_service_.is_stopping()) {
                if (OB_FAIL(root_service_.stop_service())) {
                  FLOG_WARN("root_service stop_service failed", KR(ret));
                }
              } else {
                ret = OB_ERR_UNEXPECTED;
                FLOG_WARN("inalid root service status", KR(ret));
              }
            }
          }
        }
      }
    } else {
      if (OB_TENANT_NOT_IN_SERVER == ret) {
        ret = OB_SUCCESS;
      } else {
        FLOG_WARN("fail to get tenant", KR(ret), "tenant_id", OB_SYS_TENANT_ID);
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
