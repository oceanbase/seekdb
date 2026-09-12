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

#ifdef __linux__
#include <linux/falloc.h> // FALLOC_FL_ZERO_RANGE for linux kernel 3.15
#endif
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <basetsd.h>
#include <windows.h>
#include "lib/file/windows_file_path.h"
#include "lib/allocator/page_arena.h"
typedef SSIZE_T ssize_t;
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#ifndef O_NOATIME
#define O_NOATIME 0
#endif
#ifndef FALLOC_FL_ZERO_RANGE
#define FALLOC_FL_ZERO_RANGE 0
#endif
#define stat64 _stat64
static bool ob_resolve_path_at(int dir_fd, const char *relative,
    oceanbase::common::WindowsFilePath &path)
{
  if (dir_fd < 0) { errno = EBADF; return false; }
  const int ret = path.assign_at(reinterpret_cast<HANDLE>(_get_osfhandle(dir_fd)), relative);
  if (ret != 0) {
    const int path_errno = path.error_to_errno(ret);
    fprintf(stderr, "seekdb: resolve directory-relative path failed: code=%d win32=%lu path=%s\n",
        ret, path.win32_error(), relative == nullptr ? "(null)" : relative);
    errno = path_errno;
  }
  return ret == 0;
}
static int ob_fstatat64(int dir_fd, const char *name, struct _stat64 *buf, int) {
  oceanbase::common::ObArenaAllocator allocator;
  oceanbase::common::WindowsFilePath path(allocator);
  return ob_resolve_path_at(dir_fd, name, path) ? _wstat64(path.wide(), buf) : -1;
}
#define fstatat64 ob_fstatat64
static int ob_openat(int dir_fd, const char *name, int flags, ...) {
  const int mode = (flags & _O_CREAT) ? _S_IREAD | _S_IWRITE : 0;
  oceanbase::common::ObArenaAllocator allocator;
  oceanbase::common::WindowsFilePath path(allocator);
  return ob_resolve_path_at(dir_fd, name, path)
      ? _wopen(path.wide(), (flags & ~(O_DIRECT | O_NOATIME)) | _O_BINARY, mode) : -1;
}
#define openat ob_openat
static int ob_renameat(int src_fd, const char *src, int dst_fd, const char *dst) {
  oceanbase::common::ObArenaAllocator allocator;
  oceanbase::common::WindowsFilePath source(allocator), destination(allocator);
  return ob_resolve_path_at(src_fd, src, source) && ob_resolve_path_at(dst_fd, dst, destination)
      ? _wrename(source.wide(), destination.wide()) : -1;
}
#define renameat ob_renameat
static int ob_fsync(int fd) {
  if (fd < 0) { errno = EBADF; return -1; }
  const HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  if (FlushFileBuffers(handle)) { return 0; }
  const DWORD error = GetLastError();
  fprintf(stderr, "seekdb: flush failed: fd=%d win32=%lu\n", fd, error);
  switch (error) {
    case ERROR_INVALID_HANDLE: errno = EBADF; break;
    case ERROR_ACCESS_DENIED: errno = EACCES; break;
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_PARAMETER: errno = EINVAL; break;
    default: errno = EIO; break;
  }
  return -1;
}
#define fsync ob_fsync
// MSVCRT typedefs `off_t` as 32-bit `long`. Use int64_t explicitly so PALF
// log file offsets and sizes can exceed 2 GiB on Windows.
static int ob_fallocate(int fd, int, int64_t, int64_t len) {
  return _chsize_s(fd, len) == 0 ? 0 : -1;
}
#define fallocate ob_fallocate
static int ob_ftruncate(int fd, int64_t len) {
  return _chsize_s(fd, len) == 0 ? 0 : -1;
}
static ssize_t ob_pwrite(int fd, const void *buf, size_t count, int64_t offset) {
  long long prev = _lseeki64(fd, 0, SEEK_CUR);
  _lseeki64(fd, offset, SEEK_SET);
  int written = _write(fd, buf, (unsigned)count);
  _lseeki64(fd, prev, SEEK_SET);
  return written;
}
static ssize_t ob_pread(int fd, void *buf, size_t count, int64_t offset) {
  long long prev = _lseeki64(fd, 0, SEEK_CUR);
  _lseeki64(fd, offset, SEEK_SET);
  int nread = _read(fd, buf, (unsigned)count);
  _lseeki64(fd, prev, SEEK_SET);
  return nread;
}
#else
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <fcntl.h> // For fcntl, F_PREALLOCATE on macOS
#include <string.h> // For memset
#include <stdlib.h> // For calloc, free
// macOS doesn't have stat64/fstatat64, use stat/fstatat instead
#define stat64 stat
#define fstatat64 fstatat
#endif
#include "log_io_utils.h"
#include "share/ob_errno.h"
#include "logservice/ob_server_log_block_mgr.h"

namespace oceanbase
{
namespace palf
{

const int64_t RETRY_INTERVAL = 10*1000;

int open_directory(const char *dir_path)
{
#ifdef _WIN32
  if (NULL == dir_path) {
    errno = EINVAL;
    return -1;
  }
  common::ObArenaAllocator allocator;
  common::WindowsFilePath path(allocator);
  const int path_ret = path.assign(dir_path);
  if (path_ret != OB_SUCCESS) { errno = path.error_to_errno(path_ret); return -1; }
  HANDLE h = CreateFileW(
      path.wide(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      NULL,
      OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS,
      NULL);
  if (h == INVALID_HANDLE_VALUE) {
    h = CreateFileW(
        path.wide(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        NULL);
  }
  if (h == INVALID_HANDLE_VALUE) {
    errno = EACCES;
    return -1;
  }
  int fd = _open_osfhandle((intptr_t)h, _O_RDONLY);
  if (fd == -1) {
    CloseHandle(h);
    return -1;
  }
  return fd;
#else
  return ::open(dir_path, O_DIRECTORY | O_RDONLY);
#endif
}

int openat_with_retry(const int dir_fd, 
                      const char *block_path,
                      const int flag,
                      const int mode,
                      int &fd)
{
  int ret = OB_SUCCESS;
  if (-1 == dir_fd || NULL == block_path || -1 == flag || -1 == mode) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid argument", K(dir_fd), KP(block_path), K(flag), K(mode));
  } else {
    do {
      if (-1 == (fd = ::openat(dir_fd, block_path, flag, mode))) {
#ifdef _WIN32
        const int operation_errno = errno;
#endif
        ret = convert_sys_errno();
#ifdef _WIN32
        if (operation_errno == EINVAL || operation_errno == ENAMETOOLONG ||
            operation_errno == EBADF || operation_errno == ENOTDIR) { break; }
#endif
        PALF_LOG(ERROR, "open block failed", K(ret), K(errno), K(block_path), K(dir_fd));
        ob_usleep(RETRY_INTERVAL);
      } else {
        ret = OB_SUCCESS;
        break;
      }
    } while (OB_FAIL(ret));
  }
  return ret;
}
int close_with_ret(const int fd)
{
  int ret = OB_SUCCESS;
  if (-1 == fd) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(ERROR, "invalid argument", K(fd));
  } else if (-1 == (::close(fd))) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "close block failed", K(ret), K(errno), K(fd));
  } else {
  }
  return ret;
}

int check_file_exist(const char *file_name,
                     bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
#ifdef __APPLE__
  struct stat file_info;
#else
  struct stat64 file_info;
#endif
  if (OB_ISNULL(file_name) || OB_UNLIKELY(strlen(file_name) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments.", KCSTRING(file_name), K(ret));
  } else {
#ifdef _WIN32
    ObArenaAllocator allocator("PalfPath");
    WindowsFilePath path(allocator);
    if (OB_FAIL(path.assign(file_name))) {
      PALF_LOG(WARN, "invalid file path", K(ret), K(file_name), "win32", path.win32_error());
    } else if (0 == ::_wstat64(path.wide(), &file_info)) {
      exist = true;
    } else if (errno != ENOENT) {
      ret = convert_sys_errno();
    }
#else
    exist = (0 == ::stat64(file_name, &file_info));
#endif
  }
  return ret;
}

int check_file_exist(const int dir_fd,
                     const char *file_name,
                     bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
#ifdef __APPLE__
  struct stat file_info;
#else
  struct stat64 file_info;
#endif
  const int64_t flag = 0;
  if (OB_ISNULL(file_name) || OB_UNLIKELY(strlen(file_name) == 0)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid arguments.", KCSTRING(file_name), K(ret));
  } else {
    exist = (0 == ::fstatat64(dir_fd, file_name, &file_info, flag));
#ifdef _WIN32
    if (!exist && errno != ENOENT) { ret = convert_sys_errno(); }
#endif
  }
  return ret;
}

bool check_rename_success(const char *src_name,
                          const char *dest_name)
{
  bool bool_ret = false;
  bool src_exist = false;
  bool dest_exist = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_file_exist(src_name, src_exist))) {
  } else if (!src_exist && OB_FAIL(check_file_exist(dest_name, dest_exist))) {
    PALF_LOG(WARN, "check_file_exist failed", KR(ret), K(src_name), K(dest_name));
  } else if (!src_exist && dest_exist) {
    bool_ret = true;
    PALF_LOG(INFO, "check_rename_success return true",
             KR(ret), K(src_name), K(dest_name), K(src_exist), K(dest_exist));
  } else {
    bool_ret = false;
    LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg", "rename file failed, unexpected error",
                  KR(ret), K(errno), K(src_name), K(dest_name), K(src_exist), K(dest_exist));
  }
  return bool_ret;
}

bool check_renameat_success(const int src_dir_fd,
                            const char *src_name,
                            const int dest_dir_fd,
                            const char *dest_name)
{
  bool bool_ret = false;
  bool src_exist = false;
  bool dest_exist = false;
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_file_exist(src_dir_fd, src_name, src_exist))) {
  } else if (!src_exist && OB_FAIL(check_file_exist(dest_dir_fd, dest_name, dest_exist))) {
    PALF_LOG(WARN, "check_file_exist failed", KR(ret), K(src_name), K(dest_name));
  } else if (!src_exist && dest_exist) {
    bool_ret = true;
    PALF_LOG(INFO, "check_renameat_success return true",
             KR(ret), K(src_name), K(dest_name), K(src_dir_fd), K(dest_dir_fd), K(src_exist), K(dest_exist));
  } else {
    bool_ret = false;
    LOG_DBA_ERROR(OB_ERR_UNEXPECTED, "msg", "renameat file failed, unexpected error",
                  KR(ret), K(errno), K(src_name), K(dest_name), K(src_exist), K(dest_exist));
  }
  return bool_ret;
}

int rename_with_retry(const char *src_name,
                      const char *dest_name)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src_name) || OB_ISNULL(dest_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KP(src_name), KP(dest_name));
  } else {
#ifdef _WIN32
    ObArenaAllocator allocator("PalfPath");
    WindowsFilePath source(allocator);
    WindowsFilePath destination(allocator);
    if (OB_FAIL(source.assign(src_name)) || OB_FAIL(destination.assign(dest_name))) {
      PALF_LOG(WARN, "invalid rename path", K(ret), K(src_name), K(dest_name));
      return ret;
    }
#endif
    do {
      ret = OB_SUCCESS;
#ifdef _WIN32
      const int result = ::_wrename(source.wide(), destination.wide());
      const int saved_errno = errno;
      unsigned long native_error = 0;
      if (result == -1) { _get_doserrno(&native_error); }
#else
      const int result = ::rename(src_name, dest_name);
#endif
      if (-1 == result) {
        ret  = convert_sys_errno();
        LOG_DBA_WARN(OB_IO_ERROR, "msg", "rename file failed",
                     KR(ret), K(errno), K(src_name), K(dest_name));
        // for xfs, source file not exist and dest file exist after rename return ENOSPC, therefore, next rename will return
        // OB_NO_SUCH_FILE_OR_DIRECTORY, however, for some reason, we can not return OB_SUCCESS when rename return OB_NO_SUCH_FILE_OR_DIRECTORY.
        // consider that, if file names with 'src_name' has been delted by human and file names with 'dest_name' not exist.
        if (OB_NO_SUCH_FILE_OR_DIRECTORY == ret && check_rename_success(src_name, dest_name)) {
          ret = OB_SUCCESS;
          break;
        }
#ifdef _WIN32
        if (saved_errno == EINVAL || saved_errno == ENAMETOOLONG
            || saved_errno == ENOTDIR
            || (saved_errno == EACCES && native_error != ERROR_SHARING_VIOLATION
                && native_error != ERROR_LOCK_VIOLATION)
            || saved_errno == EEXIST || saved_errno == ENOENT) {
          break;
        }
#endif
        usleep(RETRY_INTERVAL);
      }
    } while(OB_FAIL(ret));
  }
  return ret;
}

int renameat_with_retry(const int src_dir_fd,
                        const char *src_name,
                        const int dest_dir_fd,
                        const char *dest_name)
{
  int ret = OB_SUCCESS;
  if (src_dir_fd < 0 || OB_ISNULL(src_name)
      || dest_dir_fd < 0 || OB_ISNULL(dest_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", KP(src_name), KP(dest_name));
  } else {
    do {
      ret = OB_SUCCESS;
      if (-1 == ::renameat(src_dir_fd, src_name, dest_dir_fd, dest_name)) {
#ifdef _WIN32
        const int operation_errno = errno;
        unsigned long native_error = 0;
        _get_doserrno(&native_error);
#endif
        ret  = convert_sys_errno();
#ifdef _WIN32
        if (operation_errno == EINVAL || operation_errno == ENAMETOOLONG ||
            operation_errno == EBADF || operation_errno == ENOTDIR) { break; }
#endif
        LOG_DBA_WARN(OB_IO_ERROR, "msg", "renameat file failed",
                     KR(ret), K(errno), K(src_name), K(dest_name), K(src_dir_fd), K(dest_dir_fd));
        // for xfs, source file not exist and dest file exist after renameat return ENOSPC, therefore, next renameat will return
        // OB_NO_SUCH_FILE_OR_DIRECTORY, however, for some reason, we can not return OB_SUCCESS when renameat return OB_NO_SUCH_FILE_OR_DIRECTORY.
        // consider that, if file names with 'src_name' has been delted by human and file names with 'dest_name' not exist.
        if (OB_NO_SUCH_FILE_OR_DIRECTORY == ret && check_renameat_success(src_dir_fd, src_name, dest_dir_fd, dest_name)) {
          ret = OB_SUCCESS;
          break;
        }
#ifdef _WIN32
        if (operation_errno == ENOENT || operation_errno == EEXIST
            || (operation_errno == EACCES && native_error != ERROR_SHARING_VIOLATION
                && native_error != ERROR_LOCK_VIOLATION)) { break; }
#endif
        ob_usleep(RETRY_INTERVAL);
      }
    } while(OB_FAIL(ret));
  }
  return ret;
}

int fsync_with_retry(const int dir_fd)
{
  int ret = OB_SUCCESS;
  do {
    if (-1 == ::fsync(dir_fd)) {
#ifdef _WIN32
      const int operation_errno = errno;
#endif
      ret = convert_sys_errno();
      CLOG_LOG(ERROR, "fsync dest dir failed", K(ret), K(dir_fd));
#ifdef _WIN32
      if (operation_errno == EBADF || operation_errno == EACCES || operation_errno == EINVAL) { break; }
#endif
      ob_usleep(RETRY_INTERVAL);
    } else {
      ret = OB_SUCCESS;
      break;
    }
  } while (OB_FAIL(ret));
  return ret;

}

int scan_dir(const char *dir_name, ObBaseDirFunctor &functor)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ObArenaAllocator allocator;
  WindowsFilePath directory(allocator), child(allocator);
  WindowsDirectoryIterator iterator(allocator);
  if (OB_FAIL(directory.assign(dir_name))) {
    PALF_LOG(WARN, "invalid scan directory", K(ret), K(dir_name));
  } else if (OB_FAIL(iterator.open(directory))) {
    PALF_LOG(WARN, "open scan directory failed", K(ret), K(dir_name),
        "win32_error", iterator.win32_error());
  } else {
    DWORD attributes = 0;
    while (OB_SUCC(ret)) {
      ret = iterator.next(child, attributes);
      if (OB_ITER_END == ret) { ret = OB_SUCCESS; break; }
      if (OB_FAIL(ret)) { break; }
      const char *name = child.utf8();
      for (const char *cursor = name; *cursor != '\0'; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') { name = cursor + 1; }
      }
      struct dirent entry = {};
      const size_t length = strlen(name);
      if (length >= sizeof(entry.d_name)) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        MEMCPY(entry.d_name, name, length + 1);
        ret = functor.func(&entry);
      }
    }
    if (OB_FAIL(ret)) {
      PALF_LOG(WARN, "scan directory failed", K(ret), K(dir_name),
          "win32_error", iterator.win32_error());
    }
  }
#else
  DIR *open_dir = NULL;
  struct dirent *result = NULL;

  if (OB_ISNULL(dir_name)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid argument", K(ret), K(dir_name));
  } else if (OB_ISNULL(open_dir = ::opendir(dir_name))) {
    if (ENOENT != errno) {
      ret = OB_FILE_NOT_OPENED;
      PALF_LOG(WARN, "Fail to open dir, ", K(ret), K(dir_name));
    } else {
      ret = OB_NO_SUCH_FILE_OR_DIRECTORY;
      PALF_LOG(WARN, "dir does not exist", K(ret), K(dir_name));
    }
  } else {
    while ((NULL != (result = ::readdir(open_dir))) && OB_SUCC(ret)) {
      if (0 != STRCMP(result->d_name, ".") && 0 != STRCMP(result->d_name, "..")
          && OB_FAIL((functor.func)(result))) {
        PALF_LOG(WARN, "fail to operate dir entry", K(ret), K(dir_name));
      }
    }
  }
  // close dir
  if (NULL != open_dir) {
    ::closedir(open_dir);
  }
#endif
  return ret;
}

int GetBlockCountFunctor::func(const dirent *entry)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(entry)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid args", K(ret), KP(entry));
  } else {
    const char *entry_name = entry->d_name;
    if (false == is_number(entry_name)) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(WARN, "this is block is not used for palf!!!", K(ret), K(entry_name));
    } else {
      count_ ++;
    }
  }
  return ret;
}

int TrimLogDirectoryFunctor::func(const dirent *entry)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(entry)) {
    ret = OB_INVALID_ARGUMENT;
    PALF_LOG(WARN, "invalid args", K(ret), KP(entry));
  } else {
    const char *entry_name = entry->d_name;
    if (false == is_number(entry_name)) {
      ret = OB_ERR_UNEXPECTED;
      PALF_LOG(WARN, "this is block is not used for palf!!!", K(ret), K(entry_name));
    } else {
      uint32_t block_id = static_cast<uint32_t>(strtol(entry->d_name, nullptr, 10));
      if (LOG_INVALID_BLOCK_ID == min_block_id_ || block_id < min_block_id_) {
        min_block_id_ = block_id;
      }
      if (LOG_INVALID_BLOCK_ID == max_block_id_ || block_id > max_block_id_) {
        max_block_id_ = block_id;
      }
    }
  }
  return ret;
}

int reuse_block_at(const int dir_fd, const char *block_path)
{
  int ret = OB_SUCCESS;
  int fd = -1;
  if (-1 == (fd = ::openat(dir_fd, block_path, LOG_WRITE_FLAG))) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "::openat failed", K(ret), K(block_path));
#ifdef __APPLE__
  } else if (-1 == ftruncate(fd, PALF_PHY_BLOCK_SIZE)) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "::ftruncate failed (macOS fallocate replacement)", K(ret), K(block_path));
  } else {
    // Zero out the file by writing zeros
    char *zero_buf = static_cast<char *>(calloc(1, 64 * 1024)); // 64KB buffer
    if (NULL == zero_buf) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      PALF_LOG(ERROR, "failed to allocate zero buffer", K(ret));
    } else {
      int64_t written = 0;
      int64_t remain = PALF_PHY_BLOCK_SIZE;
      while (OB_SUCC(ret) && remain > 0) {
        int64_t to_write = (remain > 64 * 1024) ? 64 * 1024 : remain;
        ssize_t n = ob_pwrite(fd, zero_buf, to_write, written);
        if (n != to_write) {
          ret = convert_sys_errno();
          PALF_LOG(ERROR, "pwrite failed", K(ret), K(block_path));
          break;
        }
        written += n;
        remain -= n;
      }
      free(zero_buf);
      if (OB_SUCC(ret)) {
        PALF_LOG(INFO, "reuse_block_at success", K(ret), K(block_path));
      }
    }
#else
  } else if (-1 == ::fallocate(fd, FALLOC_FL_ZERO_RANGE, 0, PALF_PHY_BLOCK_SIZE)) {
    ret = convert_sys_errno();
    PALF_LOG(ERROR, "::fallocate failed", K(ret), K(block_path));
  } else {
    PALF_LOG(INFO, "reuse_block_at success", K(ret), K(block_path));
#endif
  }

  if (-1 != fd) {
    ::close(fd);
  }
  return ret;
}

} // end namespace palf
} // end namespace oceanbase
