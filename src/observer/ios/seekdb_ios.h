// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Engine states for the experimental, single-run-per-process iOS host. */
enum seekdb_ios_state {
  SEEKDB_IOS_IDLE = 0,
  SEEKDB_IOS_STARTING = 1,
  SEEKDB_IOS_RUNNING = 2,
  SEEKDB_IOS_STOPPING = 3,
  SEEKDB_IOS_STOPPED = 4,
  SEEKDB_IOS_FAILED = 5
};

/** Completed cleanup actions for the current process run. */
enum seekdb_ios_cleanup_status {
  SEEKDB_IOS_CLEANUP_NONE = 0,
  SEEKDB_IOS_CLEANUP_SERVER = 1 << 0,
  SEEKDB_IOS_CLEANUP_CURL = 1 << 1,
  SEEKDB_IOS_CLEANUP_WORKING_DIRECTORY = 1 << 2
};

/**
 * Run the engine synchronously on a dedicated background thread until stopped.
 * The absolute directory must be inside the app's writable sandbox. This changes
 * the process working directory for the engine's lifetime and restores it
 * before returning, including after startup failure. A failed run cannot be
 * retried in the same process.
 * Uses a 1 GiB logical memory budget, a 128 MiB vector allocation limit, and
 * 2 GiB redo space, with TCP disabled; clients use
 * the engine's Unix socket. Returns an engine error code, or zero on clean stop.
 * Only one invocation is supported per app process. Never call on the UI thread.
 */
int seekdb_ios_run(const char *absolute_directory);

/** Internal driver entry; parameter storage must remain valid until return. */
int seekdb_ios_run_with_parameters(const char *absolute_directory, const char *const *parameters);

/** Request shutdown; safe from another thread, including during startup. */
void seekdb_ios_request_stop(void);

/** Return the current lifecycle state without blocking. */
enum seekdb_ios_state seekdb_ios_get_state(void);

/** Return completed cleanup actions for the current process run. */
unsigned int seekdb_ios_get_cleanup_status(void);

/** Return the first cleanup error without replacing the primary runtime error. */
int seekdb_ios_get_cleanup_error(void);

/** Return the source revision compiled into the linked runtime archive. */
const char *seekdb_ios_get_build_id(void);

/** Return whether deterministic test hooks were compiled into the runtime. */
const char *seekdb_ios_get_hook_mode(void);

#ifdef __cplusplus
}
#endif
