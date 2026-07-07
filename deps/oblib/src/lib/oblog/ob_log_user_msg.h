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
// oblib log(LOG_USER_ERROR/LOG_DBA)for lib-level error infrastructure.
#ifndef OCEANBASE_LIB_OBLOG_OB_LOG_USER_MSG_H_
#define OCEANBASE_LIB_OBLOG_OB_LOG_USER_MSG_H_
#include "lib/ob_errno.h"
namespace oceanbase { namespace common {
const char *ob_error_name(const int oberr);  // defined in src/share/ob_errno.cpp; resolved at link time
} }

#define OB_ERROR__USER_ERROR_MSG "Common error"
#define OB_NOT_SUPPORTED__USER_ERROR_MSG "%s not supported"
#define OB_INVALID_ARGUMENT__USER_ERROR_MSG "Incorrect arguments to %s"
#define OB_INIT_FAIL__USER_ERROR_MSG "Initialize error"

#endif
