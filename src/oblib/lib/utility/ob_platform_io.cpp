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

#include "lib/utility/ob_platform_io.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <new>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <sys/stat.h>
#include <windows.h>
#include <cwchar>
// S_IFLNK is not part of the UCRT stat constants; the value matches the POSIX
// convention used by the repository. Callers compare through S_ISLNK(st_mode).
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace oceanbase
{
namespace common
{

#ifdef _WIN32

namespace platform_io_internal
{

// ReadFile/WriteFile take a DWORD byte count; transfer bigger requests in
// bounded chunks instead of truncating the count.
const int64_t MAX_IO_CHUNK = 1LL << 30;

int name_too_long_errno()
{
#ifdef ENAMETOOLONG
  return ENAMETOOLONG;
#else
  return EINVAL;
#endif
}

int win32_error_to_errno(DWORD error)
{
  int ret = EIO;
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_DRIVE:
      ret = ENOENT;
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_WRITE_PROTECT:
      ret = EACCES;
      break;
    case ERROR_ALREADY_EXISTS:
    case ERROR_FILE_EXISTS:
      ret = EEXIST;
      break;
    case ERROR_TOO_MANY_OPEN_FILES:
      ret = EMFILE;
      break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
      ret = ENOSPC;
      break;
    case ERROR_INVALID_HANDLE:
      ret = EBADF;
      break;
    case ERROR_OUTOFMEMORY:
    case ERROR_NOT_ENOUGH_MEMORY:
      ret = ENOMEM;
      break;
    case ERROR_OPERATION_ABORTED:
      ret = EINTR;
      break;
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY:
      ret = EBUSY;
      break;
    case ERROR_DIR_NOT_EMPTY:
      ret = ENOTEMPTY;
      break;
    case ERROR_FILENAME_EXCED_RANGE:
      ret = name_too_long_errno();
      break;
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_ACCESS:
    case ERROR_NOT_SUPPORTED:
      ret = EINVAL;
      break;
    case ERROR_BROKEN_PIPE:
    case ERROR_PIPE_NOT_CONNECTED:
      ret = EPIPE;
      break;
    default:
      ret = EIO;
      break;
  }
  return ret;
}

// Converts a NUL-terminated UTF-8 name to UTF-16 and allocates the result with
// new[]. Returns 0 on success; on failure returns -1 with errno set.
int utf8_to_wide(const char *path, wchar_t *&wide)
{
  int ret = -1;
  wide = NULL;
  const int wide_len = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
  if (wide_len <= 0) {
    errno = EINVAL;
  } else if (NULL == (wide = new (std::nothrow) wchar_t[wide_len])) {
    errno = ENOMEM;
  } else if (0 == ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, wide_len)) {
    delete[] wide;
    wide = NULL;
    errno = EINVAL;
  } else {
    ret = 0;
  }
  return ret;
}

bool is_absolute_path(const wchar_t *path)
{
  const size_t len = ::wcslen(path);
  const bool leading_slash = len >= 1 && (L'/' == path[0] || L'\\' == path[0]);
  const bool second_slash = len >= 2 && (L'/' == path[1] || L'\\' == path[1]);
  bool ret = leading_slash && second_slash;
  if (!ret && len >= 3 && (L'/' == path[2] || L'\\' == path[2]) && L':' == path[1]) {
    ret = (path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z');
  }
  return ret;
}

// True when the path contains a "." or ".." component, which the verbatim path
// form would interpret literally instead of resolving.
bool has_dot_component(const wchar_t *path)
{
  bool ret = false;
  const size_t len = ::wcslen(path);
  for (size_t i = 0; i < len && !ret; ++i) {
    if ((i == 0 || L'\\' == path[i - 1] || L'/' == path[i - 1]) && L'.' == path[i]) {
      const size_t rest = len - i;
      ret = (rest == 1 || L'\\' == path[i + 1] || L'/' == path[i + 1])
            || (rest >= 2 && L'.' == path[i + 1]
                && (rest == 2 || L'\\' == path[i + 2] || L'/' == path[i + 2]));
    }
  }
  return ret;
}

// Returns a newly allocated extended-length path (\\?\ or \\?\UNC\).
// Paths that already carry an extended or device prefix are returned unchanged.
// Absolute paths are prefixed directly, which keeps paths beyond MAX_PATH
// working without going through GetFullPathNameW. Relative paths, and absolute
// paths that need "." / ".." resolution or separator normalization, are made
// absolute by GetFullPathNameW first. Returns NULL with errno set on failure.
wchar_t *make_extended_path(const wchar_t *path)
{
  wchar_t *result = NULL;
  const size_t path_len = ::wcslen(path);
  const bool has_prefix = path_len >= 4 && L'\\' == path[0] && L'\\' == path[1]
                          && (L'?' == path[2] || L'.' == path[2]) && L'\\' == path[3];
  if (has_prefix) {
    if (NULL == (result = new (std::nothrow) wchar_t[path_len + 1])) {
      errno = ENOMEM;
    } else {
      ::wmemcpy(result, path, path_len + 1);
    }
  } else {
    wchar_t *absolute = NULL;
    if (is_absolute_path(path) && !has_dot_component(path)) {
      absolute = new (std::nothrow) wchar_t[path_len + 1];
      if (NULL == absolute) {
        errno = ENOMEM;
      } else {
        ::wmemcpy(absolute, path, path_len + 1);
      }
    } else {
      const DWORD need = ::GetFullPathNameW(path, 0, NULL, NULL);
      if (0 == need) {
        errno = win32_error_to_errno(::GetLastError());
      } else if (NULL == (absolute = new (std::nothrow) wchar_t[need])) {
        errno = ENOMEM;
      } else {
        const DWORD written = ::GetFullPathNameW(path, need, absolute, NULL);
        if (0 == written) {
          errno = win32_error_to_errno(::GetLastError());
          delete[] absolute;
          absolute = NULL;
        } else if (written >= need) {
          errno = name_too_long_errno();
          delete[] absolute;
          absolute = NULL;
        }
      }
    }
    if (NULL != absolute) {
      const size_t absolute_len = ::wcslen(absolute);
      for (size_t i = 0; i < absolute_len; ++i) {
        if (L'/' == absolute[i]) {
          absolute[i] = L'\\';
        }
      }
      // GetFullPathNameW already returns an extended-length path when the
      // working directory is one (ob_chdir_utf8 keeps the verbatim form so
      // paths beyond MAX_PATH work). Never wrap that in "\\?\" again.
      const bool absolute_has_prefix = absolute_len >= 4
                                       && L'\\' == absolute[0] && L'\\' == absolute[1]
                                       && (L'?' == absolute[2] || L'.' == absolute[2])
                                       && L'\\' == absolute[3];
      if (absolute_has_prefix) {
        result = absolute;
      } else {
        // A UNC path (\\server\share) replaces its leading "\\" with "\\?\UNC\".
        const bool is_unc = absolute_len >= 2 && L'\\' == absolute[0] && L'\\' == absolute[1];
        const size_t prefix_len = is_unc ? 8 : 4;
        result = new (std::nothrow) wchar_t[absolute_len + prefix_len + 1];
        if (NULL == result) {
          errno = ENOMEM;
        } else if (is_unc) {
          ::wmemcpy(result, L"\\\\?\\UNC\\", 8);
          ::wmemcpy(result + 8, absolute + 2, absolute_len - 2 + 1);
        } else {
          ::wmemcpy(result, L"\\\\?\\", 4);
          ::wmemcpy(result + 4, absolute, absolute_len + 1);
        }
        delete[] absolute;
      }
    }
  }
  return result;
}

// Converts a UTF-8 name to its extended-length (\\?\) UTF-16 form. Returns a
// new[]-allocated string or NULL with errno set.
wchar_t *utf8_to_extended_path(const char *path)
{
  wchar_t *wide = NULL;
  wchar_t *extended = NULL;
  if (NULL == path || '\0' == path[0]) {
    errno = EINVAL;
  } else if (0 == utf8_to_wide(path, wide)) {
    extended = make_extended_path(wide);
    delete[] wide;
  }
  return extended;
}

// Converts a NUL-terminated UTF-16 string to UTF-8. Returns 0 or -1 with errno
// set when the name does not fit into `buffer_size`.
int wide_to_utf8(const wchar_t *wide, char *buffer, int64_t buffer_size)
{
  int ret = -1;
  if (NULL == wide || NULL == buffer || buffer_size <= 0
      || buffer_size > static_cast<int64_t>(INT_MAX)) {
    errno = EINVAL;
  } else {
    const int utf8_len = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) {
      errno = EINVAL;
    } else if (utf8_len > buffer_size) {
      errno = name_too_long_errno();
    } else if (0 == ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer,
                                          static_cast<int>(buffer_size), NULL, NULL)) {
      errno = EINVAL;
    } else {
      ret = 0;
    }
  }
  return ret;
}

int64_t filetime_to_unix_seconds(const FILETIME &filetime)
{
  ULARGE_INTEGER value;
  value.LowPart = filetime.dwLowDateTime;
  value.HighPart = filetime.dwHighDateTime;
  return static_cast<int64_t>(value.QuadPart / 10000000ULL) - 11644473600LL;
}

uint32_t attributes_to_mode(DWORD attributes)
{
  uint32_t mode = 0;
  if (0 != (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
    mode |= _S_IFDIR;
  } else {
    mode |= _S_IFREG;
  }
  if (0 != (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
    mode |= S_IFLNK;
  }
  mode |= (0 != (attributes & FILE_ATTRIBUTE_READONLY)) ? _S_IREAD : (_S_IREAD | _S_IWRITE);
  return mode;
}

int fill_stat_from_attributes(const wchar_t *extended_path, DWORD attributes,
                              ObPlatformFileStat &stat)
{
  int ret = 0;
  WIN32_FILE_ATTRIBUTE_DATA data;
  ::memset(&data, 0, sizeof(data));
  if (FALSE == ::GetFileAttributesExW(extended_path, GetFileExInfoStandard, &data)) {
    errno = win32_error_to_errno(::GetLastError());
    ret = -1;
  } else {
    stat.size = (static_cast<int64_t>(data.nFileSizeHigh) << 32)
                | static_cast<int64_t>(data.nFileSizeLow);
    stat.atime_sec = filetime_to_unix_seconds(data.ftLastAccessTime);
    stat.mtime_sec = filetime_to_unix_seconds(data.ftLastWriteTime);
    stat.ctime_sec = filetime_to_unix_seconds(data.ftCreationTime);
    stat.dev = 0;
    stat.inode = 0;
    stat.rdev = 0;
    stat.block_cnt = 0;
    stat.block_size = 4096;
    stat.nlink = 1;
    stat.uid = 0;
    stat.gid = 0;
    stat.mode = attributes_to_mode(attributes);
  }
  return ret;
}

// Converts a NUL-terminated UTF-16 string to a new[]-allocated UTF-8 string.
// Returns NULL with errno set on failure.
char *wide_to_utf8_alloc(const wchar_t *wide)
{
  char *result = NULL;
  const int utf8_size = ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, NULL, 0, NULL, NULL);
  if (utf8_size <= 0) {
    errno = EINVAL;
  } else if (NULL == (result = new (std::nothrow) char[utf8_size])) {
    errno = ENOMEM;
  } else if (0 == ::WideCharToMultiByte(CP_UTF8, 0, wide, -1, result, utf8_size, NULL, NULL)) {
    delete[] result;
    result = NULL;
    errno = EINVAL;
  }
  return result;
}

// Returns the plain absolute UTF-8 name of the object behind `handle`.
// GetFinalPathNameByHandleW reports the verbatim form ("\\?\C:\..." or
// "\\?\UNC\server\..."); the path helpers add the extended-length prefix
// themselves, so the prefix is stripped here.
char *handle_to_utf8(const HANDLE handle)
{
  char *result = NULL;
  const DWORD need = ::GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
  if (0 == need) {
    errno = win32_error_to_errno(::GetLastError());
  } else {
    wchar_t *normalized = new (std::nothrow) wchar_t[need];
    if (NULL == normalized) {
      errno = ENOMEM;
    } else {
      const DWORD written = ::GetFinalPathNameByHandleW(handle, normalized, need,
                                                        FILE_NAME_NORMALIZED);
      if (0 == written || written >= need) {
        errno = (0 == written) ? win32_error_to_errno(::GetLastError())
                               : name_too_long_errno();
      } else {
        wchar_t *clean = new (std::nothrow) wchar_t[written + 1];
        if (NULL == clean) {
          errno = ENOMEM;
        } else {
          size_t clean_len = 0;
          if (written > 8 && 0 == ::wcsncmp(normalized, L"\\\\?\\UNC\\", 8)) {
            // "\\?\UNC\server\share" -> "\\server\share".
            clean[0] = L'\\';
            clean[1] = L'\\';
            ::wmemcpy(clean + 2, normalized + 8, written - 8 + 1);
            clean_len = written - 6;
          } else if (written > 4 && 0 == ::wcsncmp(normalized, L"\\\\?\\", 4)) {
            ::wmemcpy(clean, normalized + 4, written - 4 + 1);
            clean_len = written - 4;
          } else {
            ::wmemcpy(clean, normalized, written + 1);
            clean_len = written;
          }
          clean[clean_len] = L'\0';
          result = wide_to_utf8_alloc(clean);
          delete[] clean;
        }
      }
      delete[] normalized;
    }
  }
  return result;
}

// Resolves the path `rel_path` relative to the directory behind `dir_fd` into a
// new[]-allocated absolute UTF-8 name that the path-based helpers accept. An
// absolute `rel_path` is returned unchanged, as openat(2) ignores `dir_fd` then.
char *resolve_at_path(int dir_fd, const char *rel_path)
{
  char *result = NULL;
  if (NULL == rel_path || '\0' == rel_path[0]) {
    errno = EINVAL;
  } else if (dir_fd < 0) {
    // See ob_pread_fd: the CRT terminates the process on invalid descriptors,
    // so reject them before _get_osfhandle.
    errno = EBADF;
  } else if (ob_is_absolute_path_utf8(rel_path)) {
    const size_t rel_len = ::strlen(rel_path);
    if (NULL == (result = new (std::nothrow) char[rel_len + 1])) {
      errno = ENOMEM;
    } else {
      ::memcpy(result, rel_path, rel_len + 1);
    }
  } else {
    const HANDLE handle = reinterpret_cast<HANDLE>(::_get_osfhandle(dir_fd));
    char *dir_path = NULL;
    if (INVALID_HANDLE_VALUE == handle) {
      errno = EBADF;
    } else if (NULL != (dir_path = handle_to_utf8(handle))) {
      size_t dir_len = ::strlen(dir_path);
      while (dir_len > 0 && ('\\' == dir_path[dir_len - 1] || '/' == dir_path[dir_len - 1])) {
        --dir_len;
      }
      const size_t rel_len = ::strlen(rel_path);
      if (NULL == (result = new (std::nothrow) char[dir_len + 1 + rel_len + 1])) {
        errno = ENOMEM;
      } else {
        ::memcpy(result, dir_path, dir_len);
        result[dir_len] = '\\';
        ::memcpy(result + dir_len + 1, rel_path, rel_len + 1);
      }
      delete[] dir_path;
    }
  }
  return result;
}

} // namespace platform_io_internal

#endif // _WIN32

#ifndef _WIN32
namespace platform_io_internal
{

int name_too_long_errno()
{
  return ENAMETOOLONG;
}

void fill_stat(const struct stat &src, ObPlatformFileStat &stat)
{
  stat.size = static_cast<int64_t>(src.st_size);
  stat.dev = static_cast<uint64_t>(src.st_dev);
  stat.inode = static_cast<uint64_t>(src.st_ino);
  stat.rdev = static_cast<uint64_t>(src.st_rdev);
  stat.block_cnt = static_cast<uint64_t>(src.st_blocks);
  stat.block_size = static_cast<uint64_t>(src.st_blksize);
  stat.nlink = static_cast<uint32_t>(src.st_nlink);
  stat.uid = static_cast<uint32_t>(src.st_uid);
  stat.gid = static_cast<uint32_t>(src.st_gid);
  stat.mode = static_cast<uint32_t>(src.st_mode);
#if defined(__APPLE__)
  stat.atime_sec = static_cast<int64_t>(src.st_atimespec.tv_sec);
  stat.mtime_sec = static_cast<int64_t>(src.st_mtimespec.tv_sec);
  stat.ctime_sec = static_cast<int64_t>(src.st_ctimespec.tv_sec);
#else
  stat.atime_sec = static_cast<int64_t>(src.st_atim.tv_sec);
  stat.mtime_sec = static_cast<int64_t>(src.st_mtim.tv_sec);
  stat.ctime_sec = static_cast<int64_t>(src.st_ctim.tv_sec);
#endif
}

} // namespace platform_io_internal
#endif // !_WIN32

int ob_open_utf8(const char *path, int flags, int mode)
{
  int fd = -1;
#ifdef _WIN32
  wchar_t *wide_path = NULL;
  if (NULL == path || '\0' == path[0]) {
    errno = EINVAL;
  } else if (0 == platform_io_internal::utf8_to_wide(path, wide_path)) {
    wchar_t *extended_path = platform_io_internal::make_extended_path(wide_path);
    if (NULL != extended_path) {
      fd = ::_wopen(extended_path, flags | _O_BINARY, mode);
      delete[] extended_path;
    }
    delete[] wide_path;
  }
#else
  if (NULL == path || '\0' == path[0]) {
    errno = EINVAL;
  } else {
    fd = ::open(path, flags, static_cast<mode_t>(mode));
  }
#endif
  return fd;
}

int ob_open_dir_utf8(const char *path)
{
  int fd = -1;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL != extended_path) {
    HANDLE handle = ::CreateFileW(extended_path, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (INVALID_HANDLE_VALUE == handle) {
      // A directory the process may only read still has to be scannable.
      const DWORD error = ::GetLastError();
      handle = ::CreateFileW(extended_path, GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
      if (INVALID_HANDLE_VALUE == handle) {
        errno = platform_io_internal::win32_error_to_errno(error);
      }
    }
    if (INVALID_HANDLE_VALUE != handle) {
      fd = ::_open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY);
      if (-1 == fd) {
        errno = EMFILE;
        ::CloseHandle(handle);
      }
    }
    delete[] extended_path;
  }
#else
  if (NULL == path || '\0' == path[0]) {
    errno = EINVAL;
  } else {
    fd = ::open(path, O_RDONLY | O_DIRECTORY);
  }
#endif
  return fd;
}

int64_t ob_pread_fd(int fd, void *buf, int64_t count, int64_t offset)
{
  if (count < 0 || offset < 0 || (NULL == buf && count > 0)) {
    errno = EINVAL;
    return -1;
  }
#ifdef _WIN32
  int64_t total = 0;
  if (0 > fd) {
    // The CRT validates negative descriptors by failing the process, so reject
    // them before _get_osfhandle and report the POSIX answer instead.
    errno = EBADF;
    total = -1;
  } else if (INVALID_HANDLE_VALUE
             == (reinterpret_cast<HANDLE>(::_get_osfhandle(fd)))) {
    errno = EBADF;
    total = -1;
  } else {
    const HANDLE handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    while (total < count) {
      const DWORD chunk = static_cast<DWORD>(
          (count - total) < platform_io_internal::MAX_IO_CHUNK
              ? (count - total) : platform_io_internal::MAX_IO_CHUNK);
      const uint64_t position = static_cast<uint64_t>(offset) + static_cast<uint64_t>(total);
      OVERLAPPED overlapped;
      ::memset(&overlapped, 0, sizeof(overlapped));
      overlapped.Offset = static_cast<DWORD>(position & 0xFFFFFFFF);
      overlapped.OffsetHigh = static_cast<DWORD>(position >> 32);
      DWORD read_bytes = 0;
      if (FALSE == ::ReadFile(handle, static_cast<char *>(buf) + total, chunk, &read_bytes,
                              &overlapped)) {
        const DWORD error = ::GetLastError();
        if (ERROR_HANDLE_EOF == error) {
          // A read starting at or past EOF is a successful short read.
          break;
        }
        if (0 == total) {
          errno = platform_io_internal::win32_error_to_errno(error);
          total = -1;
        }
        break;
      }
      total += static_cast<int64_t>(read_bytes);
      if (read_bytes < chunk) {
        // End of file or a short transfer: this is the successful result.
        break;
      }
    }
  }
  return total;
#else
  return static_cast<int64_t>(::pread(fd, buf, static_cast<size_t>(count),
                                       static_cast<off_t>(offset)));
#endif
}

int64_t ob_pwrite_fd(int fd, const void *buf, int64_t count, int64_t offset)
{
  if (count < 0 || offset < 0 || (NULL == buf && count > 0)) {
    errno = EINVAL;
    return -1;
  }
#ifdef _WIN32
  int64_t total = 0;
  if (0 > fd) {
    // See ob_pread_fd: reject negative descriptors before the CRT validates them.
    errno = EBADF;
    total = -1;
  } else if (INVALID_HANDLE_VALUE
             == (reinterpret_cast<HANDLE>(::_get_osfhandle(fd)))) {
    errno = EBADF;
    total = -1;
  } else {
    const HANDLE handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    while (total < count) {
      const DWORD chunk = static_cast<DWORD>(
          (count - total) < platform_io_internal::MAX_IO_CHUNK
              ? (count - total) : platform_io_internal::MAX_IO_CHUNK);
      const uint64_t position = static_cast<uint64_t>(offset) + static_cast<uint64_t>(total);
      OVERLAPPED overlapped;
      ::memset(&overlapped, 0, sizeof(overlapped));
      overlapped.Offset = static_cast<DWORD>(position & 0xFFFFFFFF);
      overlapped.OffsetHigh = static_cast<DWORD>(position >> 32);
      DWORD written_bytes = 0;
      if (FALSE == ::WriteFile(handle, static_cast<const char *>(buf) + total, chunk,
                               &written_bytes, &overlapped)) {
        if (0 == total) {
          errno = platform_io_internal::win32_error_to_errno(::GetLastError());
          total = -1;
        }
        break;
      }
      total += static_cast<int64_t>(written_bytes);
      if (written_bytes < chunk) {
        // Short write: report the transferred prefix, as pwrite(2) does.
        break;
      }
    }
  }
  return total;
#else
  return static_cast<int64_t>(::pwrite(fd, buf, static_cast<size_t>(count),
                                        static_cast<off_t>(offset)));
#endif
}

int ob_fsync_fd(int fd)
{
  int ret = 0;
#ifdef _WIN32
  if (0 > fd) {
    // See ob_pread_fd: the CRT terminates the process on invalid descriptors.
    errno = EBADF;
    ret = -1;
  } else if (0 == ::_commit(fd)) {
    ret = 0;
  } else {
    const HANDLE handle = reinterpret_cast<HANDLE>(::_get_osfhandle(fd));
    BY_HANDLE_FILE_INFORMATION info;
    bool is_directory = false;
    if (INVALID_HANDLE_VALUE == handle) {
      errno = EBADF;
      ret = -1;
    } else if (FALSE != ::GetFileInformationByHandle(handle, &info)) {
      is_directory = 0 != (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    }
    if (-1 != ret) {
      if (FALSE != ::FlushFileBuffers(handle)) {
        ret = 0;
      } else if (is_directory) {
        // NTFS journals directory metadata; a directory handle cannot always be
        // flushed, and PALF retries a failure forever. Report success, which is
        // what the Windows shim this replaces did.
        ret = 0;
      } else {
        errno = platform_io_internal::win32_error_to_errno(::GetLastError());
        ret = -1;
      }
    }
  }
#else
  ret = ::fsync(fd);
#endif
  return ret;
}

int ob_ftruncate_fd(int fd, int64_t length)
{
  int ret = 0;
#ifdef _WIN32
  ret = (0 == ::_chsize_s(fd, length)) ? 0 : -1;
#else
  ret = ::ftruncate(fd, static_cast<off_t>(length));
#endif
  return ret;
}

int ob_fallocate_fd(int fd, int64_t length)
{
  int ret = 0;
#ifdef _WIN32
  ret = (0 == ::_chsize_s(fd, length)) ? 0 : -1;
#elif defined(__APPLE__) || defined(__EMSCRIPTEN__)
  ret = ::ftruncate(fd, static_cast<off_t>(length));
#else
  ret = ::fallocate(fd, 0, 0, static_cast<off_t>(length));
#endif
  return ret;
}

class ObPlatformDir
{
public:
#ifdef _WIN32
  HANDLE handle_;
  WIN32_FIND_DATAW find_data_;
  bool first_pending_;
#else
  DIR *dir_;
#endif
};

int ob_stat_utf8(const char *path, ObPlatformFileStat &stat, bool follow_link)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    ret = -1;
  } else if (follow_link) {
    struct _stat64 os_stat;
    if (0 != ::_wstat64(extended_path, &os_stat)) {
      ret = -1;
    } else {
      stat.size = os_stat.st_size;
      stat.atime_sec = os_stat.st_atime;
      stat.mtime_sec = os_stat.st_mtime;
      stat.ctime_sec = os_stat.st_ctime;
      stat.dev = os_stat.st_dev;
      stat.inode = os_stat.st_ino;
      stat.rdev = os_stat.st_dev;
      stat.block_cnt = 0;
      stat.block_size = 4096;
      stat.mode = static_cast<uint32_t>(os_stat.st_mode);
      stat.nlink = static_cast<uint32_t>(os_stat.st_nlink);
      stat.uid = static_cast<uint32_t>(os_stat.st_uid);
      stat.gid = static_cast<uint32_t>(os_stat.st_gid);
    }
  } else {
    // _wstat64 follows reparse points; the caller asked for the link itself.
    const DWORD attributes = ::GetFileAttributesW(extended_path);
    if (INVALID_FILE_ATTRIBUTES == attributes) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    } else {
      ret = platform_io_internal::fill_stat_from_attributes(extended_path, attributes, stat);
    }
  }
  delete[] extended_path;
#else
  struct stat os_stat;
  if (0 != (follow_link ? ::stat(path, &os_stat) : ::lstat(path, &os_stat))) {
    ret = -1;
  } else {
    platform_io_internal::fill_stat(os_stat, stat);
  }
#endif
  return ret;
}

bool ob_stat_is_file(uint32_t mode)
{
#ifdef _WIN32
  return (mode & _S_IFMT) == _S_IFREG;
#else
  return S_ISREG(static_cast<mode_t>(mode));
#endif
}

bool ob_stat_is_dir(uint32_t mode)
{
#ifdef _WIN32
  return (mode & _S_IFMT) == _S_IFDIR;
#else
  return S_ISDIR(static_cast<mode_t>(mode));
#endif
}

bool ob_stat_is_link(uint32_t mode)
{
#ifdef _WIN32
  return (mode & _S_IFMT) == S_IFLNK;
#else
  return S_ISLNK(static_cast<mode_t>(mode));
#endif
}

int ob_access_utf8(const char *path, ObPathPermission permission)
{
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    return -1;
  }
  int os_mode = 0;
  switch (permission) {
    case ObPathPermission::EXISTS:
      os_mode = 0;
      break;
    case ObPathPermission::READ:
      os_mode = 4;
      break;
    case ObPathPermission::WRITE:
      os_mode = 2;
      break;
    default:
      break;
  }
  const int ret = ::_waccess(extended_path, os_mode);
  delete[] extended_path;
  return ret;
#else
  int os_mode = F_OK;
  switch (permission) {
    case ObPathPermission::EXISTS:
      os_mode = F_OK;
      break;
    case ObPathPermission::READ:
      os_mode = R_OK;
      break;
    case ObPathPermission::WRITE:
      os_mode = W_OK;
      break;
    default:
      break;
  }
  return ::access(path, os_mode);
#endif
}

int ob_mkdir_utf8(const char *path, int mode)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    ret = -1;
  } else {
    ret = ::_wmkdir(extended_path);
    delete[] extended_path;
  }
#else
  ret = ::mkdir(path, static_cast<mode_t>(mode));
#endif
  return ret;
}

int ob_rmdir_utf8(const char *path)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    ret = -1;
  } else {
    ret = ::_wrmdir(extended_path);
    delete[] extended_path;
  }
#else
  ret = ::rmdir(path);
#endif
  return ret;
}

int ob_unlink_utf8(const char *path)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    ret = -1;
  } else {
    ret = ::_wunlink(extended_path);
    delete[] extended_path;
  }
#else
  ret = ::unlink(path);
#endif
  return ret;
}

int ob_rename_utf8(const char *old_path, const char *new_path)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_old = platform_io_internal::utf8_to_extended_path(old_path);
  wchar_t *extended_new = NULL;
  if (NULL == extended_old) {
    ret = -1;
  } else if (NULL == (extended_new = platform_io_internal::utf8_to_extended_path(new_path))) {
    ret = -1;
  } else {
    ret = ::_wrename(extended_old, extended_new);
  }
  delete[] extended_old;
  delete[] extended_new;
#else
  ret = ::rename(old_path, new_path);
#endif
  return ret;
}

int ob_symlink_utf8(const char *target_path, const char *link_path)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_target = platform_io_internal::utf8_to_extended_path(target_path);
  wchar_t *extended_link = NULL;
  if (NULL == extended_target) {
    ret = -1;
  } else if (NULL == (extended_link = platform_io_internal::utf8_to_extended_path(link_path))) {
    ret = -1;
  } else if (FALSE == ::CreateSymbolicLinkW(extended_link, extended_target, 0)) {
    errno = platform_io_internal::win32_error_to_errno(::GetLastError());
    ret = -1;
  }
  delete[] extended_target;
  delete[] extended_link;
#else
  ret = ::symlink(target_path, link_path);
#endif
  return ret;
}

int ob_truncate_utf8(const char *path, int64_t length)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    ret = -1;
  } else {
    const int fd = ::_wopen(extended_path, O_RDWR | _O_BINARY, 0);
    if (-1 == fd) {
      ret = -1;
    } else {
      if (0 != ::_chsize_s(fd, length)) {
        ret = -1;
      }
      ::_close(fd);
    }
    delete[] extended_path;
  }
#else
  ret = ::truncate(path, static_cast<off_t>(length));
#endif
  return ret;
}

int ob_openat_utf8(int dir_fd, const char *path, int flags, int mode)
{
  int fd = -1;
#ifdef _WIN32
  char *resolved = platform_io_internal::resolve_at_path(dir_fd, path);
  if (NULL != resolved) {
    fd = ob_open_utf8(resolved, flags, mode);
    delete[] resolved;
  }
#else
  fd = ::openat(dir_fd, path, flags, static_cast<mode_t>(mode));
#endif
  return fd;
}

int ob_statat_utf8(int dir_fd, const char *path, ObPlatformFileStat &stat, bool follow_link)
{
  int ret = 0;
#ifdef _WIN32
  char *resolved = platform_io_internal::resolve_at_path(dir_fd, path);
  if (NULL == resolved) {
    ret = -1;
  } else {
    ret = ob_stat_utf8(resolved, stat, follow_link);
    delete[] resolved;
  }
#else
  struct stat os_stat;
  const int flags = follow_link ? 0 : AT_SYMLINK_NOFOLLOW;
  if (0 != ::fstatat(dir_fd, path, &os_stat, flags)) {
    ret = -1;
  } else {
    platform_io_internal::fill_stat(os_stat, stat);
  }
#endif
  return ret;
}

int ob_renameat_utf8(int old_dir_fd, const char *old_path,
                     int new_dir_fd, const char *new_path)
{
  int ret = 0;
#ifdef _WIN32
  char *resolved_old = platform_io_internal::resolve_at_path(old_dir_fd, old_path);
  char *resolved_new = NULL;
  if (NULL == resolved_old) {
    ret = -1;
  } else if (NULL == (resolved_new = platform_io_internal::resolve_at_path(new_dir_fd,
                                                                          new_path))) {
    ret = -1;
  } else {
    ret = ob_rename_utf8(resolved_old, resolved_new);
  }
  delete[] resolved_old;
  delete[] resolved_new;
#else
  ret = ::renameat(old_dir_fd, old_path, new_dir_fd, new_path);
#endif
  return ret;
}

int ob_unlinkat_utf8(int dir_fd, const char *path, bool is_dir)
{
  int ret = 0;
#ifdef _WIN32
  char *resolved = platform_io_internal::resolve_at_path(dir_fd, path);
  if (NULL == resolved) {
    ret = -1;
  } else {
    ret = is_dir ? ob_rmdir_utf8(resolved) : ob_unlink_utf8(resolved);
    delete[] resolved;
  }
#else
  ret = ::unlinkat(dir_fd, path, is_dir ? AT_REMOVEDIR : 0);
#endif
  return ret;
}

#ifdef _WIN32
// GetVolumePathNameW resolves mounted directories and junctions as well as drive roots.
static wchar_t *make_volume_root(const wchar_t *wide_path)
{
  const DWORD capacity = 32768;
  wchar_t *root = new (std::nothrow) wchar_t[capacity];
  if (NULL == root) {
    errno = ENOMEM;
  } else if (FALSE == ::GetVolumePathNameW(wide_path, root, capacity)) {
    errno = platform_io_internal::win32_error_to_errno(::GetLastError());
    delete[] root;
    root = NULL;
  }
  return root;
}
#endif

int ob_get_disk_space_utf8(const char *path, int64_t &total_space, int64_t &free_space)
{
  int ret = 0;
  total_space = 0;
  free_space = 0;
#ifdef _WIN32
  wchar_t *wide_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == wide_path) {
    ret = -1;
  } else {
    ULARGE_INTEGER free_bytes_available;
    ULARGE_INTEGER total_number_of_bytes;
    ULARGE_INTEGER total_number_of_free_bytes;
    if (FALSE == ::GetDiskFreeSpaceExW(wide_path, &free_bytes_available, &total_number_of_bytes,
                                      &total_number_of_free_bytes)) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    } else {
      total_space = static_cast<int64_t>(total_number_of_bytes.QuadPart);
      free_space = static_cast<int64_t>(free_bytes_available.QuadPart);
    }
    delete[] wide_path;
  }
#else
  struct statvfs svfs;
  if (NULL == path) {
    errno = EINVAL;
    ret = -1;
  } else if (0 != ::statvfs(path, &svfs)) {
    ret = -1;
  } else {
    total_space = static_cast<int64_t>((svfs.f_blocks + svfs.f_bavail - svfs.f_bfree) * svfs.f_bsize);
    free_space = static_cast<int64_t>(svfs.f_bavail * svfs.f_bsize);
  }
#endif
  return ret;
}

int ob_get_filesystem_id_utf8(const char *path, int64_t &filesystem_id)
{
  int ret = 0;
  filesystem_id = 0;
#ifdef _WIN32
  wchar_t *wide_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == wide_path) {
    ret = -1;
  } else {
    wchar_t *root = make_volume_root(wide_path);
    if (NULL == root) {
      ret = -1;
    } else {
      DWORD volume_serial = 0;
      if (FALSE == ::GetVolumeInformationW(root, NULL, 0, &volume_serial, NULL, NULL, NULL, 0)) {
        errno = platform_io_internal::win32_error_to_errno(::GetLastError());
        ret = -1;
      } else {
        filesystem_id = static_cast<int64_t>(volume_serial);
      }
      delete[] root;
    }
    delete[] wide_path;
  }
#else
  struct statvfs svfs;
  if (NULL == path) {
    errno = EINVAL;
    ret = -1;
  } else if (0 != ::statvfs(path, &svfs)) {
    ret = -1;
  } else {
    filesystem_id = static_cast<int64_t>(svfs.f_fsid);
  }
#endif
  return ret;
}

int ob_fsync_dir_utf8(const char *path)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
    ret = -1;
  } else {
    const HANDLE handle = ::CreateFileW(extended_path, GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (INVALID_HANDLE_VALUE == handle) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    } else {
      // Directories are journaled; a failed flush is not fatal, matching the
      // previous Windows behaviour.
      ::FlushFileBuffers(handle);
      ::CloseHandle(handle);
    }
    delete[] extended_path;
  }
#else
  const int fd = ::open(path, O_RDONLY | O_DIRECTORY);
  if (-1 == fd) {
    ret = -1;
  } else {
    if (0 != ::fsync(fd)) {
      ret = -1;
    }
    ::close(fd);
  }
#endif
  return ret;
}

bool ob_is_absolute_path_utf8(const char *path)
{
  bool ret = false;
  if (NULL != path) {
#ifdef _WIN32
    const size_t len = ::strlen(path);
    if (len >= 2 && ('\\' == path[0] || '/' == path[0])
        && ('\\' == path[1] || '/' == path[1])) {
      ret = true;
    } else if (len >= 3 && ':' == path[1]
               && ('\\' == path[2] || '/' == path[2])) {
      ret = ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'));
    }
#else
    ret = ('/' == path[0]);
#endif
  }
  return ret;
}

int ob_absolute_path_utf8(const char *path, char *buffer, int64_t buffer_size)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *wide_path = NULL;
  if (NULL == path || NULL == buffer || buffer_size <= 0) {
    errno = EINVAL;
    ret = -1;
  } else if (0 != platform_io_internal::utf8_to_wide(path, wide_path)) {
    ret = -1;
  } else {
    const DWORD need = ::GetFullPathNameW(wide_path, 0, NULL, NULL);
    if (0 == need) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    } else {
      wchar_t *absolute = new (std::nothrow) wchar_t[need];
      if (NULL == absolute) {
        errno = ENOMEM;
        ret = -1;
      } else {
        const DWORD written = ::GetFullPathNameW(wide_path, need, absolute, NULL);
        if (0 == written || written >= need) {
          errno = (0 == written) ? platform_io_internal::win32_error_to_errno(::GetLastError())
                                 : platform_io_internal::name_too_long_errno();
          ret = -1;
        } else {
          ret = platform_io_internal::wide_to_utf8(absolute, buffer, buffer_size);
        }
        delete[] absolute;
      }
    }
    delete[] wide_path;
  }
#else
  if (NULL == path || NULL == buffer || buffer_size <= 0) {
    errno = EINVAL;
    ret = -1;
  } else {
    char *resolved = ::realpath(path, NULL);
    if (NULL == resolved) {
      ret = -1;
    } else {
      const size_t resolved_size = ::strlen(resolved) + 1;
      if (resolved_size > static_cast<size_t>(buffer_size)) {
        errno = platform_io_internal::name_too_long_errno();
        ret = -1;
      } else {
        ::memcpy(buffer, resolved, resolved_size);
      }
      ::free(resolved);
    }
  }
#endif
  return ret;
}

int ob_chdir_utf8(const char *path)
{
  int ret = 0;
#ifdef _WIN32
  wchar_t *wide_path = NULL;
  wchar_t *absolute_path = NULL;
  if (0 != platform_io_internal::utf8_to_wide(path, wide_path)) {
    ret = -1;
  } else {
    const DWORD need = ::GetFullPathNameW(wide_path, 0, NULL, NULL);
    if (0 == need) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    } else if (NULL == (absolute_path = new (std::nothrow) wchar_t[need])) {
      errno = ENOMEM;
      ret = -1;
    } else {
      const DWORD written = ::GetFullPathNameW(wide_path, need, absolute_path, NULL);
      if (0 == written) {
        errno = platform_io_internal::win32_error_to_errno(::GetLastError());
        ret = -1;
      } else if (written >= need) {
        errno = platform_io_internal::name_too_long_errno();
        ret = -1;
      } else if (written + 1 <= MAX_PATH) {
        // Prefer the plain form: the process working directory is then
        // ordinary "C:\..." for the legacy relative-path call sites that do
        // not go through this layer.
        if (FALSE == ::SetCurrentDirectoryW(absolute_path)) {
          errno = platform_io_internal::win32_error_to_errno(::GetLastError());
          ret = -1;
        }
      } else {
        // Win32 only accepts paths beyond MAX_PATH in the verbatim form.
        wchar_t *extended_path = platform_io_internal::make_extended_path(absolute_path);
        if (NULL == extended_path) {
          ret = -1;
        } else {
          if (FALSE == ::SetCurrentDirectoryW(extended_path)) {
            errno = platform_io_internal::win32_error_to_errno(::GetLastError());
            ret = -1;
          }
          delete[] extended_path;
        }
      }
    }
  }
  delete[] wide_path;
  delete[] absolute_path;
#else
  ret = ::chdir(path);
#endif
  return ret;
}

int ob_getcwd_utf8(char *buffer, int64_t buffer_size)
{
  int ret = 0;
  if (NULL == buffer || buffer_size <= 0) {
    errno = EINVAL;
    ret = -1;
#ifdef _WIN32
  } else {
    const DWORD need = ::GetCurrentDirectoryW(0, NULL);
    if (0 == need) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    } else {
      wchar_t *wide_cwd = new (std::nothrow) wchar_t[need + 1];
      if (NULL == wide_cwd) {
        errno = ENOMEM;
        ret = -1;
      } else if (0 == ::GetCurrentDirectoryW(need + 1, wide_cwd)) {
        errno = platform_io_internal::win32_error_to_errno(::GetLastError());
        ret = -1;
      } else {
        // ob_chdir_utf8 keeps the verbatim form, GetCurrentDirectoryW reports
        // it back; callers expect an ordinary "C:\..." path.
        size_t wide_cwd_len = ::wcslen(wide_cwd);
        if (wide_cwd_len >= 8 && 0 == ::wcsncmp(wide_cwd, L"\\\\?\\UNC\\", 8)) {
          ::wmemmove(wide_cwd + 2, wide_cwd + 8, wide_cwd_len - 8 + 1);
          wide_cwd[0] = L'\\';
          wide_cwd[1] = L'\\';
        } else if (wide_cwd_len >= 4 && L'\\' == wide_cwd[0] && L'\\' == wide_cwd[1]
                   && L'?' == wide_cwd[2] && L'\\' == wide_cwd[3]) {
          ::wmemmove(wide_cwd, wide_cwd + 4, wide_cwd_len - 4 + 1);
        }
        ret = platform_io_internal::wide_to_utf8(wide_cwd, buffer, buffer_size);
      }
      delete[] wide_cwd;
    }
  }
#else
  } else if (NULL == ::getcwd(buffer, static_cast<size_t>(buffer_size))) {
    ret = -1;
  }
#endif
  return ret;
}

ObPlatformDir *ob_opendir_utf8(const char *path)
{
  ObPlatformDir *dir = NULL;
#ifdef _WIN32
  wchar_t *extended_path = platform_io_internal::utf8_to_extended_path(path);
  if (NULL == extended_path) {
  } else {
    const size_t extended_len = ::wcslen(extended_path);
    wchar_t *pattern = new (std::nothrow) wchar_t[extended_len + 3];
    if (NULL == pattern) {
      errno = ENOMEM;
    } else {
      ::wmemcpy(pattern, extended_path, extended_len);
      ::wmemcpy(pattern + extended_len, L"\\*", 3);
      dir = new (std::nothrow) ObPlatformDir();
      if (NULL == dir) {
        errno = ENOMEM;
      } else {
        ::memset(&dir->find_data_, 0, sizeof(dir->find_data_));
        dir->first_pending_ = true;
        dir->handle_ = ::FindFirstFileW(pattern, &dir->find_data_);
        if (INVALID_HANDLE_VALUE == dir->handle_) {
          errno = platform_io_internal::win32_error_to_errno(::GetLastError());
          delete dir;
          dir = NULL;
        }
      }
      delete[] pattern;
    }
    delete[] extended_path;
  }
#else
  DIR *os_dir = ::opendir(path);
  if (NULL != os_dir) {
    dir = new (std::nothrow) ObPlatformDir();
    if (NULL == dir) {
      ::closedir(os_dir);
      errno = ENOMEM;
    } else {
      dir->dir_ = os_dir;
    }
  }
#endif
  return dir;
}

int ob_readdir_entry_utf8(ObPlatformDir *dir, ObPlatformDirEntry &entry)
{
  int ret = -1;
  if (NULL == dir) {
    errno = EINVAL;
#ifdef _WIN32
  } else {
    ret = 0;
    for (;;) {
      if (dir->first_pending_) {
        dir->first_pending_ = false;
      } else if (FALSE == ::FindNextFileW(dir->handle_, &dir->find_data_)) {
        const DWORD error = ::GetLastError();
        if (ERROR_NO_MORE_FILES == error) {
          ret = 0;
        } else {
          errno = platform_io_internal::win32_error_to_errno(error);
          ret = -1;
        }
        break;
      }
      if (0 == platform_io_internal::wide_to_utf8(dir->find_data_.cFileName, entry.name,
                                                  sizeof(entry.name))) {
        const DWORD attributes = dir->find_data_.dwFileAttributes;
        if (0 != (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
          entry.type = OB_DIR_ENTRY_DIR;
        } else if (0 != (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
          entry.type = OB_DIR_ENTRY_OTHER;
        } else {
          entry.type = OB_DIR_ENTRY_FILE;
        }
        ret = 1;
        break;
      }
    }
  }
#else
  } else {
    const struct dirent *os_entry = ::readdir(dir->dir_);
    if (NULL == os_entry) {
      ret = 0;
    } else if (::strlen(os_entry->d_name) + 1 > sizeof(entry.name)) {
      errno = platform_io_internal::name_too_long_errno();
      ret = -1;
    } else {
      ::snprintf(entry.name, sizeof(entry.name), "%s", os_entry->d_name);
#ifdef _DIRENT_HAVE_D_TYPE
      switch (os_entry->d_type) {
        case DT_REG:
          entry.type = OB_DIR_ENTRY_FILE;
          break;
        case DT_DIR:
          entry.type = OB_DIR_ENTRY_DIR;
          break;
        case DT_UNKNOWN:
          entry.type = OB_DIR_ENTRY_UNKNOWN;
          break;
        default:
          entry.type = OB_DIR_ENTRY_OTHER;
          break;
      }
#else
      entry.type = OB_DIR_ENTRY_UNKNOWN;
#endif
      ret = 1;
    }
  }
#endif
  return ret;
}

int ob_readdir_utf8(ObPlatformDir *dir, char *name, int64_t name_size)
{
  int ret = -1;
  if (NULL == dir || NULL == name || name_size <= 0) {
    errno = EINVAL;
  } else {
    ObPlatformDirEntry entry;
    const int read_ret = ob_readdir_entry_utf8(dir, entry);
    if (read_ret <= 0) {
      ret = read_ret;
    } else if (::strlen(entry.name) + 1 > static_cast<size_t>(name_size)) {
      errno = platform_io_internal::name_too_long_errno();
    } else {
      ::snprintf(name, name_size, "%s", entry.name);
      ret = 1;
    }
  }
  return ret;
}

int ob_get_executable_path_utf8(char *buffer, int64_t buffer_size)
{
  int ret = -1;
  if (NULL == buffer || buffer_size <= 0) {
    errno = EINVAL;
#ifdef _WIN32
  } else {
    wchar_t wide_path[32768];
    const DWORD len = ::GetModuleFileNameW(NULL, wide_path,
                                           static_cast<DWORD>(sizeof(wide_path) / sizeof(wchar_t)));
    if (0 == len) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
    } else if (len >= sizeof(wide_path) / sizeof(wchar_t)) {
      errno = platform_io_internal::name_too_long_errno();
    } else if (0 == platform_io_internal::wide_to_utf8(wide_path, buffer, buffer_size)) {
      ret = 0;
    }
  }
#elif defined(__APPLE__)
  } else {
    char raw_path[PATH_MAX];
    uint32_t raw_size = sizeof(raw_path);
    char resolved_path[PATH_MAX];
    if (0 != _NSGetExecutablePath(raw_path, &raw_size)) {
      errno = EIO;
    } else if (NULL == ::realpath(raw_path, resolved_path)) {
      // realpath sets errno
    } else if (::strlen(resolved_path) + 1 > static_cast<size_t>(buffer_size)) {
      errno = platform_io_internal::name_too_long_errno();
    } else {
      ::snprintf(buffer, buffer_size, "%s", resolved_path);
      ret = 0;
    }
  }
#else
  } else {
    const ssize_t len = ::readlink("/proc/self/exe", buffer, buffer_size - 1);
    if (len < 0) {
      // readlink sets errno
    } else if (len >= buffer_size - 1) {
      errno = platform_io_internal::name_too_long_errno();
    } else {
      buffer[len] = '\0';
      ret = 0;
    }
  }
#endif
  return ret;
}

int ob_create_executable_alias_utf8(const char *alias_path)
{
  int ret = 0;
  if (NULL == alias_path || '\0' == alias_path[0]) {
    errno = EINVAL;
    ret = -1;
#ifdef _WIN32
  } else {
    char exe_path[4096];
    char link_path[4096];
    wchar_t *wide_exe = NULL;
    wchar_t *wide_link = NULL;
    if (0 != ob_get_executable_path_utf8(exe_path, sizeof(exe_path))) {
      ret = -1;
    } else if (::snprintf(link_path, sizeof(link_path), "%s.exe", alias_path) <= 0) {
      errno = platform_io_internal::name_too_long_errno();
      ret = -1;
    } else if (0 != platform_io_internal::utf8_to_wide(exe_path, wide_exe)) {
      ret = -1;
    } else if (0 != platform_io_internal::utf8_to_wide(link_path, wide_link)) {
      ret = -1;
    } else {
      ::DeleteFileW(wide_link);
      if (FALSE == ::CreateHardLinkW(wide_link, wide_exe, NULL)
          && FALSE == ::CopyFileW(wide_exe, wide_link, FALSE)) {
        errno = platform_io_internal::win32_error_to_errno(::GetLastError());
        ret = -1;
      }
    }
    delete[] wide_exe;
    delete[] wide_link;
  }
#else
  } else {
    char exe_path[4096];
    if (0 != ob_get_executable_path_utf8(exe_path, sizeof(exe_path))) {
      ret = -1;
    } else {
      // Drop the previous alias; a missing one is not an error.
      if (0 != ::unlink(alias_path) && ENOENT != errno) {
        ret = -1;
      } else if (0 != ::symlink(exe_path, alias_path)) {
        ret = -1;
      }
    }
  }
#endif
  return ret;
}

int ob_closedir_utf8(ObPlatformDir *dir)
{
  int ret = 0;
  if (NULL == dir) {
    errno = EINVAL;
    ret = -1;
#ifdef _WIN32
  } else {
    if (FALSE == ::FindClose(dir->handle_)) {
      errno = platform_io_internal::win32_error_to_errno(::GetLastError());
      ret = -1;
    }
    delete dir;
  }
#else
  } else {
    if (0 != ::closedir(dir->dir_)) {
      ret = -1;
    }
    delete dir;
  }
#endif
  return ret;
}

#ifdef _WIN32
int ob_win32_error_to_errno(unsigned long error)
{
  return platform_io_internal::win32_error_to_errno(static_cast<DWORD>(error));
}
#endif

} // namespace common
} // namespace oceanbase
