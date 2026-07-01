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
  }
  return ret;
}

void PalfFSCbWrapper::del_cb_impl(PalfFSCbNode *cb_impl)
{
  ObSpinLockGuard guard(lock_);
  (void)list_.remove(cb_impl);
}

int PalfFSCbWrapper::update_end_lsn(int64_t id, const LSN &end_lsn, const share::SCN &end_scn, const int64_t proposal_id)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(true == list_.is_empty())) {
  } else {
    int tmp_ret = OB_SUCCESS;
    ObSpinLockGuard guard(lock_);
    DLIST_FOREACH(node, list_) {
      PalfFSCb *cb = node->fs_cb_;
      if (NULL == cb) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_SUCCESS != (tmp_ret = cb->update_end_lsn(id, end_lsn, end_scn, proposal_id))) {
      }
    }
  }
  return ret;
}

PalfRoleChangeCbWrapper::PalfRoleChangeCbWrapper() : list_() {}
PalfRoleChangeCbWrapper::~PalfRoleChangeCbWrapper() {}

int PalfRoleChangeCbWrapper::add_cb_impl(PalfRoleChangeCbNode *cb_impl)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (false == list_.add_last(cb_impl)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
  }
  return ret;
}

void PalfRoleChangeCbWrapper::del_cb_impl(PalfRoleChangeCbNode *cb_impl)
{
  ObSpinLockGuard guard(lock_);
  if (NULL == list_.remove(cb_impl)) {
  } else {
  }
}

int PalfRoleChangeCbWrapper::on_role_change(int64_t id)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(true == list_.is_empty())) {
  } else {
    ObSpinLockGuard guard(lock_);
    DLIST_FOREACH(node, list_) {
      PalfRoleChangeCb *rc_cb = node->rc_cb_;
      if (NULL == rc_cb) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(ERROR, "PalfRoleChangeCb is NULL, unexpect error", K(ret), KPC(node));
      } else if (OB_FAIL(rc_cb->on_role_change(id))) {
      }
    }
  }
  return ret;
}

int PalfRoleChangeCbWrapper::on_need_change_leader(const int64_t id, const ObAddr &dest_addr)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(true == list_.is_empty())) {
  } else {
    ObSpinLockGuard guard(lock_);
    DLIST_FOREACH(node, list_) {
      PalfRoleChangeCb *rc_cb = node->rc_cb_;
      if (NULL == rc_cb) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(ERROR, "PalfRoleChangeCb is NULL, unexpect error", K(ret), KPC(node), K(id), K(dest_addr));
      } else if (OB_FAIL(rc_cb->on_need_change_leader(id, dest_addr))) {
      }
    }
  }
  return ret;
}

PalfRebuildCbWrapper::PalfRebuildCbWrapper() : list_() {}
PalfRebuildCbWrapper::~PalfRebuildCbWrapper() {}

int PalfRebuildCbWrapper::add_cb_impl(PalfRebuildCbNode *cb_impl)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (false == list_.add_last(cb_impl)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
  }
  return ret;
}

void PalfRebuildCbWrapper::del_cb_impl(PalfRebuildCbNode *cb_impl)
{
  ObSpinLockGuard guard(lock_);
  if (NULL == list_.remove(cb_impl)) {
  } else {
  }
}

int PalfRebuildCbWrapper::on_rebuild(const int64_t id, const LSN &lsn)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(true == list_.is_empty())) {
  } else {
    ObSpinLockGuard guard(lock_);
    DLIST_FOREACH(node, list_) {
      PalfRebuildCb *rebuild_cb = node->rebuild_cb_;
      if (NULL == rebuild_cb) {
        ret = OB_ERR_UNEXPECTED;
        PALF_LOG(ERROR, "PalfRebuildCb is NULL, unexpect error", K(ret), KPC(node));
      } else if (OB_FAIL(rebuild_cb->on_rebuild(id, lsn))) {
      }
    }
  }
  return ret;
}


LogPlugins::LogPlugins()
  : loc_lock_(),
    loc_cb_(NULL),
    palf_monitor_lock_(),
    palf_monitor_(NULL),
    palflite_monitor_lock_(),
    palflite_monitor_(NULL) { }

LogPlugins::~LogPlugins()
{
  destroy();
}

void LogPlugins::destroy()
{
  {
    common::RWLock::WLockGuard guard(loc_lock_);
    loc_cb_ = NULL;
  }
  {
    common::RWLock::WLockGuard guard(palf_monitor_lock_);
    palf_monitor_ = NULL;
  }
  {
    common::RWLock::WLockGuard guard(palflite_monitor_lock_);
    palflite_monitor_ = NULL;
  }
}

template<>
int LogPlugins::add_plugin(PalfLocationCacheCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(loc_lock_);
  if (OB_ISNULL(plugin)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(loc_cb_)) {
    ret = OB_OP_NOT_ALLOW;
  } else {
    loc_cb_ = plugin;
  }
  return ret;
}

template<>
int LogPlugins::del_plugin(PalfLocationCacheCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(loc_lock_);
  if (OB_NOT_NULL(loc_cb_)) {
    loc_cb_ = NULL;
  }
  return ret;
}

template<>
int LogPlugins::add_plugin(PalfMonitorCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(palf_monitor_lock_);
  if (OB_ISNULL(plugin)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(palf_monitor_)) {
    ret = OB_OP_NOT_ALLOW;
  } else {
    palf_monitor_ = plugin;
  }
  return ret;
}

template<>
int LogPlugins::del_plugin(PalfMonitorCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(palf_monitor_lock_);
  if (OB_NOT_NULL(palf_monitor_)) {
    palf_monitor_ = NULL;
  }
  return ret;
}

template<>
int LogPlugins::add_plugin(PalfLiteMonitorCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(palf_monitor_lock_);
  if (OB_ISNULL(plugin)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(palflite_monitor_)) {
    ret = OB_OP_NOT_ALLOW;
  } else {
    palflite_monitor_ = plugin;
  }
  return ret;
}

template<>
int LogPlugins::del_plugin(PalfLiteMonitorCb *plugin)
{
  int ret = OB_SUCCESS;
  common::RWLock::WLockGuard guard(palf_monitor_lock_);
  if (OB_NOT_NULL(palflite_monitor_)) {
    palflite_monitor_ = NULL;
  }
  return ret;
}

}; // end namespace palf
}; // end namespace oceanbase
