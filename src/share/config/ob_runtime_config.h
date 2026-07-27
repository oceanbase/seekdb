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

#ifndef OCEANBASE_RUNTIME_CONFIG_H_
#define OCEANBASE_RUNTIME_CONFIG_H_

#include "lib/function/ob_function.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/string/ob_sql_string.h"
#include "lib/container/ob_array.h"
#include "lib/lock/ob_drw_lock.h"
#include "share/config/ob_reload_config.h"
#include "share/config/ob_config_helper.h"
#include "share/config/ob_config_manager.h"
#include "share/ob_lease_struct.h"
#include "share/rc/ob_context.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase {
namespace obcall
{
class ObRuntimeConfigArg;
}
namespace omt {

class ObRuntimeConfigGuard {
public:
  ObRuntimeConfigGuard();
  ObRuntimeConfigGuard(ObServerConfig *config);
  virtual ~ObRuntimeConfigGuard();

  ObRuntimeConfigGuard(const ObRuntimeConfigGuard &guard) = delete;
  ObRuntimeConfigGuard & operator=(const ObRuntimeConfigGuard &guard) = delete;

  void set_config(ObServerConfig *config);
  bool is_valid() { return nullptr != config_; }
  /*
   * Requires: check is_valid().
   */
  ObServerConfig *operator->() { return config_; }
private:
  ObServerConfig *config_;
};

#define RUNTIME_CONF() &GCONF
#define RUNTIME_CONF_TIL() &GCONF

}
}

#endif
