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
#include <cstdint>
#include "lib/ob_errno.h"
namespace oceanbase { namespace common {
// SQL client messages are independent of the runtime logging backend.
enum UserMsgLevel
{
  USER_NONE = 0,
  USER_WARN,
  USER_ERROR,
  USER_NOTE
};
void log_user_message(UserMsgLevel level, int errcode, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void insert_warning_buffer(UserMsgLevel level, int errcode, const char *data, int64_t len);
void log_user_error_line_column(UserMsgLevel level, int line, int column);

const char *ob_error_name(const int oberr);  // defined in src/share/ob_errno.cpp; resolved at link time
} }

#define OB_ERROR__USER_ERROR_MSG "Common error"
#define OB_NOT_SUPPORTED__USER_ERROR_MSG "%s not supported"
#define OB_INVALID_ARGUMENT__USER_ERROR_MSG "Incorrect arguments to %s"
#define OB_INIT_FAIL__USER_ERROR_MSG "Initialize error"

#endif
