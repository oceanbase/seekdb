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

#ifndef OCEANBASE_SHARE_OB_INTERNAL_TABLE_CHANGE_NOTIFIER_H_
#define OCEANBASE_SHARE_OB_INTERNAL_TABLE_CHANGE_NOTIFIER_H_

#include "share/ob_module_data_arg.h"
#include "lib/function/ob_function.h"
#include "lib/lock/ob_spin_lock.h"
#include "logservice/ob_log_base_type.h"

namespace oceanbase
{
namespace share
{

class ObInternalTableChangeNotifier : public logservice::ObILocalLogHandler
{
public:
  using ModuleCallback = common::ObFunction<int()>;

  static ObInternalTableChangeNotifier &get_instance();

  int init();
  void destroy();

  int register_module(table::ObModuleDataArg::ObExecModule module,
                      ModuleCallback callback);

  // Schedule refresh for one module. Called by import executor and
  // switch_to_leader. Returns immediately — the actual work is async.
  int notify(table::ObModuleDataArg::ObExecModule module);

  // ObILocalLogHandler — called by ObLocalLogHandlerSet when LS switches role.
  void deactivate() override;
  int activate() override;

private:
  ObInternalTableChangeNotifier();
  ~ObInternalTableChangeNotifier();
  DISALLOW_COPY_AND_ASSIGN(ObInternalTableChangeNotifier);

  static constexpr int MAX_MODULE = static_cast<int>(table::ObModuleDataArg::ObExecModule::MAX_MOD);
  struct ModuleEntry {
    ModuleCallback callback_;
    ModuleEntry() : callback_() {}
  };

  ModuleEntry entries_[MAX_MODULE];
  common::ObSpinLock lock_;  // protects entries_ registration
  bool is_inited_;
};

} // namespace share
} // namespace oceanbase

#endif
