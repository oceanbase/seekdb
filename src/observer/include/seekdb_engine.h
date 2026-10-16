/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SeekdbEngineImpl *SeekdbEngineHandle;

/* Start the engine and wait for startup to complete. argv contains only
 * options in seekdb command-line syntax, without a program name or path.
 * Pass argc = 0 and argv = NULL to use defaults. Arguments are
 * copied and may be released after this call. The engine never daemonizes,
 * installs process signal handlers, or redirects stdout/stderr.
 *
 * Returns NULL on failure. Optional error_code receives zero or an OceanBase
 * error code. Optional error_message receives NULL on success or a borrowed,
 * read-only diagnostic on failure, including the startup stage and available
 * system error details. Do not modify or free this string. It remains valid
 * until the next open call on the same thread, thread exit, or library unload.
 * Copy it to retain it longer. Calls on other threads do not overwrite it.
 * Server-internal details may also require inspecting the engine log.
 * Either output parameter may be NULL.
 * Help/version are invalid library arguments (they never exit).
 * One engine may be active per process. The current core supports one engine
 * lifetime per process; once core initialization has been attempted, another
 * open is rejected.
 * Argument-validation failures may be retried.
 *
 * The current core requires the process working directory to remain at
 * --base-dir until close. It is restored on return from close or failed open.
 * The host must not change cwd or rely on relative paths during this lifetime.
 * Sessions continue to use the normal MySQL socket/TCP protocol.
 */
SeekdbEngineHandle seekdb_engine_open(int argc, const char *const argv[], int *error_code,
                                     const char **error_message);

/* Stop, join and destroy the engine synchronously. Close client connections
 * before calling. Returns zero on success. The handle is consumed even when
 * cleanup reports an error; do not reuse it. Passing NULL is a no-op.
 * Do not call from a signal handler or from an engine callback.
 */
int seekdb_engine_close(SeekdbEngineHandle engine);

#ifdef __cplusplus
}
#endif
