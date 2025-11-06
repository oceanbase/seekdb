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

#define USING_LOG_PREFIX COMMON
#include "ob_log_policy.h"

namespace oceanbase
{
namespace common
{
int ObLogPolicyParser::parse_retry_write_policy(const char *str, ObLogRetryWritePolicy &policy)
{
  int ret = OB_SUCCESS;
  if (0 == STRCMP(str, "normal")) {
    policy = ObLogRetryWritePolicy::NORMAL_WRITE;
  } else if (0 == STRCMP(str, "switch_file")) {
    policy = ObLogRetryWritePolicy::SWITCH_FILE_WRITE;
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(str));
  }
  return ret;
}


int ObLogPolicyParser::parse_log_write_policy(const char *str, ObLogWritePolicy &policy)
{
  int ret = OB_SUCCESS;
  if (0 == STRCMP(str, "append")) {
    policy = ObLogWritePolicy::LOG_APPEND_WRITE;
  } else if (0 == STRCMP(str, "truncate")) {
    policy = ObLogWritePolicy::LOG_TRUNCATE_WRITE;
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(str));
  }
  return ret;
}
} // namespace common
} // namespace oceanbase
