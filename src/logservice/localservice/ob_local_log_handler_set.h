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

#ifndef OCEANBASE_LOGSERVICE_OB_LOCAL_LOG_HANDLER_SET_
#define OCEANBASE_LOGSERVICE_OB_LOCAL_LOG_HANDLER_SET_
#include "lib/container/ob_fixed_array.h"
#include "lib/container/ob_se_array.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/utility/ob_print_utils.h"
#include "share/log/ob_log_base_header.h"
#include "share/log/ob_log_base_type.h"
#include "share/ob_errno.h"
namespace oceanbase
{
namespace logservice
{
class ObLocalLogHandlerSet {
public:
  ObLocalLogHandlerSet();
  ~ObLocalLogHandlerSet();
  void reset();
  int register_handler(const ObLogBaseType &type, ObILocalLogHandler *handler);
  void unregister_handler(const ObLogBaseType &type);

  void deactivate();
  int activate();
  int activate_except(const ObLogBaseType excluded_type);
  int activate_handler(const ObLogBaseType type);
private:
  int activate_(const ObLogBaseType excluded_type);
  ObSpinLock lock_;
  ObILocalLogHandler* local_log_handlers_[ObLogBaseType::MAX_LOG_BASE_TYPE];
  bool local_log_handler_active_[ObLogBaseType::MAX_LOG_BASE_TYPE];
};
}
}
#endif // OCEANBASE_LOGSERVICE_OB_LOCAL_LOG_HANDLER_SET_
