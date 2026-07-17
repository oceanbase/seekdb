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

#define USING_LOG_PREFIX SHARE
#include "share/ob_internal_table_change_notifier.h"
#include "lib/oblog/ob_log_module.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace share
{

ObInternalTableChangeNotifier &ObInternalTableChangeNotifier::get_instance()
{
  static ObInternalTableChangeNotifier instance;
  return instance;
}

ObInternalTableChangeNotifier::ObInternalTableChangeNotifier()
  : lock_(common::ObLatchIds::DEFAULT_SPIN_LOCK),
    is_inited_(false)
{
}

ObInternalTableChangeNotifier::~ObInternalTableChangeNotifier()
{
  destroy();
}

int ObInternalTableChangeNotifier::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    // already inited, no-op
  } else {
    is_inited_ = true;
    LOG_INFO("ObInternalTableChangeNotifier inited");
  }
  return ret;
}

void ObInternalTableChangeNotifier::destroy()
{
  common::ObSpinLockGuard guard(lock_);
  for (int i = 0; i < MAX_MODULE; i++) {
    entries_[i].callback_.reset();
  }
  is_inited_ = false;
  LOG_INFO("ObInternalTableChangeNotifier destroyed");
}

int ObInternalTableChangeNotifier::register_module(
    table::ObModuleDataArg::ObExecModule module,
    ModuleCallback callback)
{
  int ret = OB_SUCCESS;
  int idx = static_cast<int>(module);
  if (idx < 0 || idx >= MAX_MODULE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid module type", K(ret), K(idx));
  } else if (!callback.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid callback", K(ret), K(idx));
  } else {
    common::ObSpinLockGuard guard(lock_);
    entries_[idx].callback_ = callback;
    LOG_INFO("registered module callback", K(idx));
  }
  return ret;
}

int ObInternalTableChangeNotifier::notify(
    table::ObModuleDataArg::ObExecModule module)
{
  int ret = OB_SUCCESS;
  int idx = static_cast<int>(module);
  if (idx < 0 || idx >= MAX_MODULE) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid module type", K(ret), K(idx));
  } else {
    LOG_INFO("[NOTIFIER] notifying module", K(idx));
    int tmp_ret = entries_[idx].callback_();
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN("module callback failed", K(tmp_ret), K(idx));
      ret = tmp_ret;
    }
  }
  return ret;
}

void ObInternalTableChangeNotifier::deactivate()
{
}

int ObInternalTableChangeNotifier::activate()
{
  int ret = OB_SUCCESS;
  
  LOG_INFO("[NOTIFIER] LS promoted to leader, notifying all modules");
  for (int mod = 0; mod < MAX_MODULE; mod++) {
    if (entries_[mod].callback_.is_valid()) {
      int tmp_ret = notify(static_cast<table::ObModuleDataArg::ObExecModule>(mod));
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("module notify failed on leader promotion", K(tmp_ret), K(mod));
        if (OB_SUCCESS == ret) { ret = tmp_ret; }
      }
    }
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
