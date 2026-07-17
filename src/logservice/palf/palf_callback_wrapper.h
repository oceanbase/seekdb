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

#ifndef OCEANBASE_LOGSERVICE_PALF_CALLBACK_WRAPPER_
#define OCEANBASE_LOGSERVICE_PALF_CALLBACK_WRAPPER_
#include "lib/list/ob_dlink_node.h"
#include "lib/list/ob_dlist.h"
#include "lib/ob_errno.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "palf_callback.h"
#include "share/ob_delegate.h"
namespace oceanbase
{
namespace palf
{

struct PalfFSCbNode : public common::ObDLinkBase<PalfFSCbNode>
{
  PalfFSCbNode(PalfFSCb *fs_cb) : fs_cb_(fs_cb) {}
  PalfFSCb *fs_cb_;
  TO_STRING_KV(KP(fs_cb_), KP(prev_), KP(next_));
};

class PalfFSCbWrapper
{
public:
  PalfFSCbWrapper();
  ~PalfFSCbWrapper();
  virtual int add_cb_impl(PalfFSCbNode *cb_impl);
  virtual void del_cb_impl(PalfFSCbNode *cb_impl);
  virtual int update_end_lsn(const LSN &end_lsn, const share::SCN &end_scn);
private:
  // The head of list
  ObDList<PalfFSCbNode> list_;
  ObSpinLock lock_;
};

#define PALF_PLUGINS_DELEGATE_PTR(delegate_obj, lock_, func_name)   \
  template <typename ...Args>                                       \
  int func_name(Args &&...args) {                                   \
    int ret = OB_SUCCESS;                                           \
    common::RWLock::RLockGuard guard(lock_);                        \
    if (OB_UNLIKELY(OB_ISNULL(delegate_obj))) {                     \
      ret = OB_NOT_INIT;                                            \
    } else {                                                        \
      ret = delegate_obj->func_name(std::forward<Args>(args)...);   \
    }                                                               \
    return ret;                                                     \
  }

class LogPlugins
{
public:
  LogPlugins();
  ~LogPlugins();
  void destroy();
  template<typename T>
  int add_plugin(T *plugin);
  template<typename T>
  int del_plugin(T *plugin);

  PALF_PLUGINS_DELEGATE_PTR(palf_monitor_, palf_monitor_lock_, record_set_base_lsn_event);
  PALF_PLUGINS_DELEGATE_PTR(palf_monitor_, palf_monitor_lock_, record_advance_base_info_event);
  PALF_PLUGINS_DELEGATE_PTR(palf_monitor_, palf_monitor_lock_, record_truncate_event);

  TO_STRING_KV(KP_(palf_monitor));
private:
  common::RWLock palf_monitor_lock_;
  PalfMonitorCb *palf_monitor_;
};
} // end namespace palf
} // end namespace oceanbase
#endif
