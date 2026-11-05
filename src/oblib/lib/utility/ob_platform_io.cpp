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
#include <cstring>
#include <new>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <cwchar>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
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
  return result;
}

} // namespace platform_io_internal

#endif // _WIN32

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

#ifdef _WIN32
int ob_win32_error_to_errno(unsigned long error)
{
  return platform_io_internal::win32_error_to_errno(static_cast<DWORD>(error));
}
#endif

} // namespace common
} // namespace oceanbase
