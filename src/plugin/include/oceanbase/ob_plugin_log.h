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

#pragma once

#include "oceanbase/ob_plugin_base.h"
#include "oceanbase/ob_plugin_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup ObPlugin
 * @{
 */

/**
 * Log Level
 */
enum ObPluginLogLevel
{
  OBP_LOG_LEVEL_TRACE,
  OBP_LOG_LEVEL_INFO,
  OBP_LOG_LEVEL_WARN,
};

/**
 * test if we should output the log with specific level
 * @note you shouldn't call this routine
 * @return
 *   - OBP_SUCCESS enabled
 *   - others not enabled
 */
OBP_PUBLIC_API int obp_log_enabled(int32_t level);

/**
 * logging
 * @note you should use OBP_LOG_xxx instead
 */
OBP_PUBLIC_API void obp_log_format(int32_t level,
                                   const char *filename,
                                   int32_t lineno,
                                   const char *location_string,
                                   int64_t location_string_size,
                                   const char *function,
                                   const char *format, ...) __attribute__((format(printf, 7, 8)));

/**
 * logging macro
 */
#define OBP_LOG(level, fmt, args...)                                              \
  do {                                                                            \
    if (OBP_SUCCESS == obp_log_enabled(level)) {                                  \
      (void)obp_log_format(level,                                                 \
                           __FILE__,                                              \
                           __LINE__,                                              \
                           __FILE__ ":" OBP_STRINGIZE(__LINE__),                  \
                           sizeof(__FILE__ ":" OBP_STRINGIZE(__LINE__)),          \
                           __FUNCTION__,                                          \
                           fmt,                                                   \
                           ##args);                                               \
    }                                                                             \
  } while (0)

#define OBP_LOG_TRACE(fmt, args...)  OBP_LOG(OBP_LOG_LEVEL_TRACE, fmt, ##args)
#define OBP_LOG_INFO(fmt, args...)   OBP_LOG(OBP_LOG_LEVEL_INFO,  fmt, ##args)
#define OBP_LOG_WARN(fmt, args...)   OBP_LOG(OBP_LOG_LEVEL_WARN,  fmt, ##args)

/** @} */

#ifdef __cplusplus
} // extern "C"
#endif
