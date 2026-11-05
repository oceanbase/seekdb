/*
 * Copyright (c) 2026 OceanBase.
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

#include <cstdint>

namespace oceanbase
{
namespace common
{

// Opens `path`, a NUL-terminated UTF-8 encoded file name, and returns a file
// descriptor or -1 with errno set, using the POSIX open(2) flag semantics.
//
// Windows: the name is converted to UTF-16 and passed to the CRT as an
// extended-length path (\\?\ or \\?\UNC\), so paths above MAX_PATH work and
// non-ASCII names are opened as UTF-8 instead of the ANSI code page. The file
// is always opened in binary mode.
int ob_open_utf8(const char *path, int flags, int mode);

// Positioned reads and writes. They behave like POSIX pread(2)/pwrite(2) on
// every platform:
//   - a read that starts at or beyond EOF transfers 0 bytes;
//   - the number of bytes actually transferred is returned, which may be less
//     than `count` at EOF or when a write reaches the end of the medium;
//   - -1 is returned on failure and errno is set from the platform error
//     (Windows: ERROR_HANDLE_EOF is reported as a successful short read,
//     other Win32 errors are mapped to their POSIX errno values).
// Interrupted transfers are not retried here; callers that need it retry on
// EINTR like they would with the POSIX calls.
int64_t ob_pread_fd(int fd, void *buf, int64_t count, int64_t offset);
int64_t ob_pwrite_fd(int fd, const void *buf, int64_t count, int64_t offset);

#ifdef _WIN32
// Maps a Win32 error code (GetLastError()) to the errno value this layer would
// report for it. Exposed for callers that also own a Win32 error path and for
// tests; there is no POSIX counterpart.
int ob_win32_error_to_errno(unsigned long error);
#endif

} // namespace common
} // namespace oceanbase
