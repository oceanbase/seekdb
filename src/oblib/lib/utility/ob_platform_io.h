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

// Opens the directory `path` and returns a descriptor, with the POSIX
// open(path, O_RDONLY | O_DIRECTORY) contract. The Windows CRT cannot open a
// directory through _wopen, so this uses CreateFileW with
// FILE_FLAG_BACKUP_SEMANTICS and wraps the handle as a descriptor; write access
// is requested first so descriptors that are flushed keep working, and a
// directory without write permission is opened read-only.
int ob_open_dir_utf8(const char *path);

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

// Flushes an open descriptor. POSIX: fsync(2). Windows: _commit, then
// FlushFileBuffers on the underlying handle. A directory descriptor cannot be
// flushed by the CRT; because NTFS journals directory metadata, the flush of a
// directory is reported as success, which is the contract the PALF Windows shim
// this replaces had. File descriptors report a failed flush as -1 with errno.
int ob_fsync_fd(int fd);

// Sets the size of an open file. POSIX: ftruncate(2); Windows: _chsize_s.
int ob_ftruncate_fd(int fd, int64_t length);

// Reserves `length` bytes for an open file, growing it when it is shorter.
// Linux: fallocate(fd, 0, 0, length); macOS: ftruncate (the new bytes read as
// zero); Windows: _chsize_s. PALF calls this on the empty file it just created
// with O_EXCL, so growing the file and Linux's allocate-without-shrinking
// contract coincide.
int ob_fallocate_fd(int fd, int64_t length);

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
  uint64_t rdev;
  uint64_t block_cnt;
  uint64_t block_size;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
};

// stat(2)/lstat(2) of a UTF-8 path. `follow_link` selects stat(2) when true and
// lstat(2) when false. Returns 0 or -1 with errno set.
int ob_stat_utf8(const char *path, ObPlatformFileStat &stat, bool follow_link = true);

// Predicates for ObPlatformFileStat::mode. They spell out the POSIX S_IS*
// macros so call sites do not depend on platform stat headers.
bool ob_stat_is_file(uint32_t mode);
bool ob_stat_is_dir(uint32_t mode);
bool ob_stat_is_link(uint32_t mode);

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
int ob_truncate_utf8(const char *path, int64_t length);

// Directory-descriptor relative forms of the operations above, with the
// openat(2)/fstatat(2)/renameat(2)/unlinkat(2) contract: `path` is relative to
// `dir_fd`, and an absolute path ignores the descriptor. On POSIX these are the
// native calls; on Windows the descriptor is resolved to the UTF-8 absolute
// name of its directory, and the extended-length path handling consequently
// stays in this file. Return values are the same as the path-based helpers.
int ob_openat_utf8(int dir_fd, const char *path, int flags, int mode);
int ob_statat_utf8(int dir_fd, const char *path, ObPlatformFileStat &stat,
                   bool follow_link = true);
int ob_renameat_utf8(int old_dir_fd, const char *old_path,
                     int new_dir_fd, const char *new_path);

// Removes `path` under `dir_fd`. `is_dir` selects rmdir(2) over unlink(2).
int ob_unlinkat_utf8(int dir_fd, const char *path, bool is_dir);

// Free and total space of the filesystem containing `path`.
int ob_get_disk_space_utf8(const char *path, int64_t &total_space, int64_t &free_space);

// Identity of the filesystem containing `path`: the volume serial number on
// Windows, statvfs(3)::f_fsid on POSIX. Two paths report the same id when they
// live on the same filesystem.
int ob_get_filesystem_id_utf8(const char *path, int64_t &filesystem_id);

// Flushes a directory entry table (fsync(2) on a directory descriptor on POSIX,
// FlushFileBuffers on a directory handle on Windows).
int ob_fsync_dir_utf8(const char *path);

// True when `path` is already absolute in the platform's syntax ("/" on POSIX,
// "C:\", "C:/", "\\server" or "//server" on Windows).
bool ob_is_absolute_path_utf8(const char *path);

// Writes the absolute UTF-8 form of `path` into `buffer`; returns 0 or -1 with
// errno set (ENAMETOOLONG when the resolved name does not fit).
int ob_absolute_path_utf8(const char *path, char *buffer, int64_t buffer_size);

// Working-directory access on UTF-8 paths. Windows converts the name to UTF-16
// (SetCurrentDirectoryW / GetCurrentDirectoryW), POSIX maps to chdir(2)/getcwd(2).
int ob_chdir_utf8(const char *path);
int ob_getcwd_utf8(char *buffer, int64_t buffer_size);

// An open directory stream. The implementation is platform specific and opaque.
class ObPlatformDir;

// Portable kind of one directory entry. POSIX reports the readdir(3) d_type
// where the filesystem provides it; when it is DT_UNKNOWN the caller may stat
// the entry itself. Windows derives the kind from the find data attributes.
enum ObPlatformDirEntryType
{
  OB_DIR_ENTRY_UNKNOWN = 0,
  OB_DIR_ENTRY_FILE = 1,
  OB_DIR_ENTRY_DIR = 2,
  OB_DIR_ENTRY_OTHER = 3,  // symlink, device, socket, ...
};

static const int64_t OB_PLATFORM_DIR_ENTRY_NAME_SIZE = 1024;

struct ObPlatformDirEntry
{
  ObPlatformDirEntry() : name(), type(OB_DIR_ENTRY_UNKNOWN) { name[0] = '\0'; }
  char name[OB_PLATFORM_DIR_ENTRY_NAME_SIZE];
  ObPlatformDirEntryType type;
};

// Directory iteration on UTF-8 paths. ob_readdir_entry_utf8() returns 1 and
// fills one entry (including "." and ".."), 0 at the end of the directory, or
// -1 with errno set. ob_readdir_utf8() is the name-only form used by callers
// that do not need the entry kind; it reports ENAMETOOLONG when the caller
// buffer cannot hold the name.
ObPlatformDir *ob_opendir_utf8(const char *path);
int ob_readdir_entry_utf8(ObPlatformDir *dir, ObPlatformDirEntry &entry);
int ob_readdir_utf8(ObPlatformDir *dir, char *name, int64_t name_size);
int ob_closedir_utf8(ObPlatformDir *dir);

// Absolute UTF-8 path of the running executable (without a verbatim prefix).
int ob_get_executable_path_utf8(char *buffer, int64_t buffer_size);

// Creates/refreshes `alias_path` as an alias of the running executable:
// a symbolic link on POSIX/macOS, and on Windows a hard link (copy fallback)
// named `<alias_path>.exe`. The previous alias is removed first.
int ob_create_executable_alias_utf8(const char *alias_path);

#ifdef _WIN32
// Maps a Win32 error code (GetLastError()) to the errno value this layer would
// report for it. Exposed for callers that also own a Win32 error path and for
// tests; there is no POSIX counterpart.
int ob_win32_error_to_errno(unsigned long error);
#endif

} // namespace common
} // namespace oceanbase
