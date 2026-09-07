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
// Sub-step profiler for the server storage-meta replay window
// (SERVER_STORAGE_META_SERVICE.start()). The observer startup timeline in
// ob_server.cpp only brackets this whole window as one coarse stage, and the
// embedded log level (WARN by default) drops the per-step INFO logs inside it.
// These marks split the window so a warm-start cost can be attributed to:
// slogger start / checkpoint read / slog replay / log start / runtime rebuild
// / first device mark / LS finish+gc / LS online.
//
// inline + function-local state gives one shared delta chain across all
// translation units that call it (ODR). Startup runs single-threaded under
// the open lock, so no synchronization is needed. Output mirrors the
// "[STARTUP_TIMELINE] <name> cost_us=..." format on the SeekdbStartup logcat
// tag on Android (visible at WARN); it is a no-op on other platforms.
inline void storage_meta_replay_timeline_mark(const char *name)
{
  static int64_t last_us = -1;
  const int64_t now_us = oceanbase::common::ObTimeUtility::current_time();
  const int64_t cost_us = (last_us < 0) ? 0 : (now_us - last_us);
  last_us = now_us;
#ifdef __ANDROID__
  char buf[192];
  snprintf(buf, sizeof(buf), "[STARTUP_TIMELINE] sms_%-28s cost_us=%lld",
           name, (long long)cost_us);
  __android_log_print(ANDROID_LOG_WARN, "SeekdbStartup", "%s", buf);
#else
  (void)name;
  (void)cost_us;
#endif
}
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_STORAGE_META_REPLAY_TIMELINE_H_
