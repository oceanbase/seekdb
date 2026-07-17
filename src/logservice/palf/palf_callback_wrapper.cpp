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

#include "palf_callback_wrapper.h"
namespace oceanbase
{
namespace palf
{
PalfFSCbWrapper::PalfFSCbWrapper() : list_() {}
PalfFSCbWrapper::~PalfFSCbWrapper() {}

int PalfFSCbWrapper::add_cb_impl(PalfFSCbNode *cb_impl)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (false == list_.add_last(cb_impl)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    PALF_LOG(INFO, "PalfFSCbWrapper add_cb_impl success");
  }
  return ret;
}

void PalfFSCbWrapper::del_cb_impl(PalfFSCbNode *cb_impl)
{
  ObSpinLockGuard guard(lock_);
  (void)list_.remove(cb_impl);
}

int PalfFSCbWrapper::update_end_lsn(const LSN &end_lsn, const share::SCN &end_scn)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(true == list_.is_empty())) {
    PALF_LOG(TRACE, "the block size callback list is empty", K(end_lsn));
  } else {
    int tmp_ret = OB_SUCCESS;
    ObSpinLockGuard guard(lock_);
    DLIST_FOREACH(node, list_) {
      PalfFSCb *cb = node->fs_cb_;
      if (NULL == cb) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(ERROR, "PalfFSCb is NULL, unexpect error", KPC(node));
      } else if (OB_SUCCESS != (tmp_ret = cb->update_end_lsn(end_lsn, end_scn))) {
        PALF_LOG(ERROR, "update_end_lsn failed", K(tmp_ret), K(end_lsn), K(end_scn), KPC(node));
      }
    }
  }
  return ret;
}

LogPlugins::LogPlugins()
  : palf_monitor_lock_(),
    palf_monitor_(NULL) { }

LogPlugins::~LogPlugins()
{
  destroy();
}

void LogPlugins::destroy()
{
  {
    common::RWLock::WLockGuard guard(palf_monitor_lock_);
    palf_monitor_ = NULL;
  }
}

template<>
int LogPlugins::add_plugin(PalfMonitorCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(palf_monitor_lock_);
  if (OB_ISNULL(plugin)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "Palf plugin is NULL", KP(plugin));
  } else if (OB_NOT_NULL(palf_monitor_)) {
    ret = OB_OP_NOT_ALLOW;
    PALF_LOG(INFO, "Palf plugin is not NULL", KP(plugin), KP_(palf_monitor));
  } else {
    palf_monitor_ = plugin;
    PALF_LOG(INFO, "add_plugin success", KP(plugin));
  }
  return ret;
}

template<>
int LogPlugins::del_plugin(PalfMonitorCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(palf_monitor_lock_);
  if (OB_NOT_NULL(palf_monitor_)) {
    PALF_LOG(INFO, "del_plugin success", KP_(palf_monitor));
    palf_monitor_ = NULL;
  }
  return ret;
}

}; // end namespace palf
}; // end namespace oceanbase
