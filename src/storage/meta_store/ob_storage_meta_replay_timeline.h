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
#ifndef OCEANBASE_STORAGE_OB_STORAGE_META_REPLAY_TIMELINE_H_
#define OCEANBASE_STORAGE_OB_STORAGE_META_REPLAY_TIMELINE_H_

#include <stdio.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{
namespace storage
{
// Sub-step profilers for the embedded cold/warm-start path. Each mark reports
// the microseconds elapsed since the previous mark of the SAME chain, so marks
// inserted anywhere in the single-threaded startup flow split a bracketed
// window into exact sub-costs without touching the surrounding code.
//
// All marks below share ONE delta chain: ob_startup_substep_mark() holds a
// function-local static, and because it is inline, ODR guarantees every
// translation unit calling any of these functions contributes to the same
// state. Startup runs single-threaded under the open lock, so no
// synchronization is needed. Output mirrors the
// "[STARTUP_TIMELINE] <name> cost_us=..." format on the SeekdbStartup logcat
// tag on Android (visible at WARN); it is a no-op on other platforms.
//
// Existing groups (see docs/seekdb-android/measure/parse_startup.py):
//   sms_*   server storage-meta replay (ob_server_storage_meta_replayer.cpp)
//   mb_*    runtime module-tree rebuild (ObServerRuntime::create_modules)
//   lson_*  LS online chain (ObLS::online_without_lock_)
inline void ob_startup_substep_mark(const char *printed_name)
{
  static int64_t last_us = -1;
  const int64_t now_us = oceanbase::common::ObTimeUtility::current_time();
  const int64_t cost_us = (last_us < 0) ? 0 : (now_us - last_us);
  last_us = now_us;
#ifdef __ANDROID__
  char buf[192];
  snprintf(buf, sizeof(buf), "[STARTUP_TIMELINE] %-30s cost_us=%lld",
           printed_name, (long long)cost_us);
  __android_log_print(ANDROID_LOG_WARN, "SeekdbStartup", "%s", buf);
#else
  (void)printed_name;
  (void)cost_us;
#endif
}

inline void storage_meta_replay_timeline_mark(const char *name)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "sms_%s", name);
  ob_startup_substep_mark(buf);
}

// Generic sub-step mark; the caller passes a fully-qualified name such as
// "mb_init" or "lson_log_handler". Emit AFTER the bracketed step so the
// printed cost is the segment that just ran (same convention as sms_*).
inline void startup_substep_timeline_mark(const char *name)
{
  ob_startup_substep_mark(name);
}
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_STORAGE_META_REPLAY_TIMELINE_H_
