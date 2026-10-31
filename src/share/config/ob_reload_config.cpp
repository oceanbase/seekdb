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

#include "share/config/ob_reload_config.h"
#include "config_bridge.h"

namespace oceanbase
{
namespace common
{
int ObReloadConfig::reload_ob_logger_set()
{
  int ret = OB_SUCCESS;
  {
    rust::String level = config::syslog_level();
    if (OB_FAIL(OB_LOGGER.parse_set(level.c_str(),
                                    static_cast<int32_t>(level.size()),
                                    0))) {
    } else if (OB_FAIL(OB_LOGGER.set_max_file_index(
        static_cast<int32_t>(config::max_syslog_file_count())))) {
    } else if (OB_FAIL(OB_LOGGER.set_record_old_log_file())) {
    } else {
      OB_LOGGER.set_enable_async_log(config::enable_async_syslog());
    }
  }
  return ret;
}

}//end of common
}//end of oceanbase
