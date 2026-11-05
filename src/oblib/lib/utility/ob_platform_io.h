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

// Metadata reported by ob_stat_utf8(). The POSIX stat/lstat fields used by the
// database are exposed with explicit widths; fields that Windows does not have
// (device, inode, owner) are zero there.
struct ObPlatformFileStat
{
  int64_t size;
  int64_t atime_sec;
  int64_t mtime_sec;
  int64_t ctime_sec;
  uint64_t dev;
  uint64_t inode;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
};

// stat(2)/lstat(2) of a UTF-8 path. `follow_link` selects stat(2) when true and
// lstat(2) when false. Returns 0 or -1 with errno set.
int ob_stat_utf8(const char *path, ObPlatformFileStat &stat, bool follow_link = true);

enum class ObPathPermission
{
  EXISTS = 0,
  READ = 1,
  WRITE = 2,
};

// access(2) with the matching POSIX errno on every platform. On Windows the
// EXECUTE bit is not modelled, so READ/WRITE are checked through _waccess.
int ob_access_utf8(const char *path, ObPathPermission permission);

// Directory and file name operations on UTF-8 paths. Each returns 0 or -1 with
// errno set. Windows uses the extended-length form internally, so long and
// non-ASCII names work.
int ob_mkdir_utf8(const char *path, int mode);
int ob_rmdir_utf8(const char *path);
int ob_unlink_utf8(const char *path);
int ob_rename_utf8(const char *old_path, const char *new_path);
int ob_symlink_utf8(const char *target_path, const char *link_path);

// Free and total space of the filesystem containing `path`.
int ob_get_disk_space_utf8(const char *path, int64_t &total_space, int64_t &free_space);

// Flushes a directory entry table (fsync(2) on a directory descriptor on POSIX,
// FlushFileBuffers on a directory handle on Windows).
int ob_fsync_dir_utf8(const char *path);

// True when `path` is already absolute in the platform's syntax ("/" on POSIX,
// "C:\", "C:/", "\\server" or "//server" on Windows).
bool ob_is_absolute_path_utf8(const char *path);

// Writes the absolute UTF-8 form of `path` into `buffer`. The buffer must be
// PATH_MAX bytes for POSIX realpath(3); returns 0 or -1 with errno set.
int ob_absolute_path_utf8(const char *path, char *buffer, int64_t buffer_size);

// An open directory stream. The implementation is platform specific and opaque.
class ObPlatformDir;

// Directory iteration on UTF-8 paths. ob_readdir_utf8() returns 1 and copies
// one entry name (including "." and "..") into `name`, 0 at the end of the
// directory, or -1 with errno set.
ObPlatformDir *ob_opendir_utf8(const char *path);
int ob_readdir_utf8(ObPlatformDir *dir, char *name, int64_t name_size);
int ob_closedir_utf8(ObPlatformDir *dir);

#ifdef _WIN32
// Maps a Win32 error code (GetLastError()) to the errno value this layer would
// report for it. Exposed for callers that also own a Win32 error path and for
// tests; there is no POSIX counterpart.
int ob_win32_error_to_errno(unsigned long error);
#endif

} // namespace common
} // namespace oceanbase
