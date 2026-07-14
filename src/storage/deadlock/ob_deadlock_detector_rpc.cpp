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

#include "ob_deadlock_detector_rpc.h"
#include "share/ob_ex_rpc.h"
#include "share/rc/ob_module_provider.h"

namespace oceanbase
{

using namespace common;
using namespace share;
using namespace detector;

namespace share
{
namespace detector
{

int ObDeadLockDetectorRpc::init(const common::ObAddr &self)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    DETECT_LOG(WARN, "init twice", KR(ret));
  } else if (false == self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "argument invalid", KR(ret), K(self));
  } else {
    self_ = self;
    is_inited_ = true;
  }

  return ret;
}

void ObDeadLockDetectorRpc::destroy()
{
  if (is_inited_) {
    is_inited_ = false;
  } else {
    DETECT_LOG_RET(ERROR, common::OB_ERR_UNEXPECTED, "ObDeadLockDetectorRpc has been destroyed", K(lbt()));
  }
}

int ObDeadLockDetectorRpc::post_lcl_message(const ObAddr &dest_addr,
                                            const ObLCLMessage &msg)
{
  int ret = OB_SUCCESS;

  DETECT_TIME_GUARD(100_ms);
  if (false == is_inited_) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "ObDeadLockDetectorRpc not inited", KR(ret));
  } else if (false == msg.is_valid() || false == dest_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "invalid argument",
              KR(ret), K(dest_addr), K(msg));
  } else {
    // single-replica: dest is always self; dispatch async in-process (ex-RPC),
    // restoring the original async post() decoupling (handler runs on a worker thread,
    // keeping cycle propagation off the sender stack). msg is serialized; tenant
    // context is restored on the worker via MTL_SWITCH.
    
    (void)ex_rpc::async_call<void>(msg, [dest_addr](const ObLCLMessage &m) {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        ObDeadLockDetectorMgr *p_deadlock_detector_mgr = share::g_mp->dead_lock_detector_mgr();
        if (OB_ISNULL(p_deadlock_detector_mgr)) {
          ret = OB_ERR_UNEXPECTED; DETECT_LOG(ERROR, "can not get ObDeadLockDetectorMgr", KR(ret), KP(p_deadlock_detector_mgr));
        } else if (OB_FAIL(p_deadlock_detector_mgr->process_lcl_message(m))) {
          DETECT_LOG(WARN, "process lcl message failed", KR(ret), K(dest_addr), K(m));
        }
      }
    });
  }

  return ret;
}

int ObDeadLockDetectorRpc::post_collect_info_message(const ObAddr &dest_addr,
                                                     const ObDeadLockCollectInfoMessage &msg)
{
  int ret = OB_SUCCESS;

  DETECT_TIME_GUARD(100_ms);
  DETECT_LOG(INFO, "post collect info msg", K(dest_addr), K(msg));
  if (false == is_inited_) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "ObDeadLockDetectorRpc not inited", KR(ret));
  } else if (false == msg.is_valid() || false == dest_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "invalid argument",
              KR(ret), K(dest_addr), K(msg));
  } else {
    // single-replica: dest is always self; dispatch async in-process (ex-RPC),
    // restoring the original async post() decoupling (handler runs on a worker thread,
    // keeping cycle propagation off the sender stack). msg is serialized; tenant
    // context is restored on the worker via MTL_SWITCH.
    
    (void)ex_rpc::async_call<void>(msg, [dest_addr](const ObDeadLockCollectInfoMessage &m) {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        ObDeadLockDetectorMgr *p_deadlock_detector_mgr = share::g_mp->dead_lock_detector_mgr();
        if (OB_ISNULL(p_deadlock_detector_mgr)) {
          ret = OB_ERR_UNEXPECTED; DETECT_LOG(ERROR, "can not get ObDeadLockDetectorMgr", KR(ret), KP(p_deadlock_detector_mgr));
        } else if (OB_FAIL(p_deadlock_detector_mgr->process_collect_info_message(m))) {
          DETECT_LOG(WARN, "process collect info message failed", KR(ret), K(dest_addr), K(m));
        }
      }
    });
  }

  return ret;
}

int ObDeadLockDetectorRpc::post_notify_parent_message(const ObAddr &dest_addr,
                                                      const ObDeadLockNotifyParentMessage &msg)
{
  int ret = OB_SUCCESS;

  DETECT_TIME_GUARD(100_ms);
  DETECT_LOG(INFO, "post notify parent msg", K(dest_addr), K(msg));
  if (false == is_inited_) {
    ret = OB_NOT_INIT;
    DETECT_LOG(WARN, "ObDeadLockDetectorRpc not inited", KR(ret));
  } else if (false == msg.is_valid() || false == dest_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    DETECT_LOG(WARN, "invalid argument",
              KR(ret), K(dest_addr), K(msg));
  } else {
    // single-replica: dest is always self; dispatch async in-process (ex-RPC),
    // restoring the original async post() decoupling (handler runs on a worker thread,
    // keeping cycle propagation off the sender stack). msg is serialized; tenant
    // context is restored on the worker via MTL_SWITCH.
    
    (void)ex_rpc::async_call<void>(msg, [dest_addr](const ObDeadLockNotifyParentMessage &m) {
      int ret = OB_SUCCESS;
      MOD_SCOPE {
        ObDeadLockDetectorMgr *p_deadlock_detector_mgr = share::g_mp->dead_lock_detector_mgr();
        if (OB_ISNULL(p_deadlock_detector_mgr)) {
          ret = OB_ERR_UNEXPECTED; DETECT_LOG(ERROR, "can not get ObDeadLockDetectorMgr", KR(ret), KP(p_deadlock_detector_mgr));
        } else if (OB_FAIL(p_deadlock_detector_mgr->process_notify_parent_message(m))) {
          DETECT_LOG(WARN, "process notify parent message failed", KR(ret), K(dest_addr), K(m));
        }
      }
    });
  }

  return ret;
}

}// namespace detector
}// namespace share
}// namespace oceanbase
