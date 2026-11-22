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

#include "logservice/wal_adapter/callback_wrapper.h"
namespace oceanbase
{
namespace wal
{
FsyncCallbackWrapper::FsyncCallbackWrapper() : list_() {}
FsyncCallbackWrapper::~FsyncCallbackWrapper() {}

int FsyncCallbackWrapper::add_cb_impl(FsyncCallbackNode *cb_impl)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (false == list_.add_last(cb_impl)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    WAL_LOG(INFO, "FsyncCallbackWrapper add_cb_impl success");
  }
  return ret;
}

void FsyncCallbackWrapper::del_cb_impl(FsyncCallbackNode *cb_impl)
{
  ObSpinLockGuard guard(lock_);
  (void)list_.remove(cb_impl);
}

int FsyncCallbackWrapper::update_end_lsn(const LSN &end_lsn, const share::SCN &end_scn)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(true == list_.is_empty())) {
  } else {
    int tmp_ret = OB_SUCCESS;
    ObSpinLockGuard guard(lock_);
    DLIST_FOREACH(node, list_) {
      FsyncCallback *cb = node->fs_cb_;
      if (NULL == cb) {
        ret = OB_ERR_UNEXPECTED;
        WAL_LOG(ERROR, "FsyncCallback is NULL, unexpect error", KPC(node));
      } else if (OB_SUCCESS != (tmp_ret = cb->update_end_lsn(end_lsn, end_scn))) {
      }
    }
  }
  return ret;
}

LogPlugins::LogPlugins()
  : wal_monitor_lock_(),
    wal_monitor_(NULL) { }

LogPlugins::~LogPlugins()
{
  destroy();
}

void LogPlugins::destroy()
{
  {
    common::RWLock::WLockGuard guard(wal_monitor_lock_);
    wal_monitor_ = NULL;
  }
}

template<>
int LogPlugins::add_plugin(MonitorCallback *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(wal_monitor_lock_);
  if (OB_ISNULL(plugin)) {
    ret = OB_INVALID_ARGUMENT;
    WAL_LOG(WARN, "WAL plugin is NULL", KP(plugin));
  } else if (OB_NOT_NULL(wal_monitor_)) {
    ret = OB_OP_NOT_ALLOW;
    WAL_LOG(INFO, "WAL plugin is not NULL", KP(plugin), KP_(wal_monitor));
  } else {
    wal_monitor_ = plugin;
    WAL_LOG(INFO, "add_plugin success", KP(plugin));
  }
  return ret;
}

template<>
int LogPlugins::del_plugin(MonitorCallback *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(wal_monitor_lock_);
  if (OB_NOT_NULL(wal_monitor_)) {
    WAL_LOG(INFO, "del_plugin success", KP_(wal_monitor));
    wal_monitor_ = NULL;
  }
  return ret;
}

}; // end namespace wal
}; // end namespace oceanbase
