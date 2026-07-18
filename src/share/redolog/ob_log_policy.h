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

#ifndef OCEANBASE_COMMON_OB_LOG_POLICY_H_
#define OCEANBASE_COMMON_OB_LOG_POLICY_H_

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{
enum ObLogRetryWritePolicy
{
  INVALID_RETRY_WRITE,
  NORMAL_WRITE,
  SWITCH_FILE_WRITE // append only
};

enum ObLogCreatePolicy
{
  INVALID_CREATE,
  NORMAL_CREATE,
  PRE_CREATE
};

enum ObLogWritePolicy
{
  INVALID_WRITE,
  LOG_APPEND_WRITE,
  LOG_TRUNCATE_WRITE
};

struct ObLogPolicyCollection
{
  ObLogRetryWritePolicy retry_write_policy_;
  ObLogCreatePolicy log_create_policy_;
  ObLogWritePolicy log_write_policy_;

  TO_STRING_KV(K_(retry_write_policy), K_(log_create_policy), K_(log_write_policy));
};

class ObLogPolicyParser
{
public:
  static int parse_retry_write_policy(const char *str, ObLogRetryWritePolicy &policy);
  static int parse_log_write_policy(const char *str, ObLogWritePolicy &policy);
};
} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_LOG_POLICY_H_
