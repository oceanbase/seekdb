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

#include "ob_async_log_struct.h"
#include "lib/allocator/ob_slice_alloc.h"
#ifdef _WIN32
#include <fcntl.h>
#include "lib/file/windows_file_path.h"
#include "lib/allocator/ob_malloc.h"
#endif

#if defined(_WIN32) && !defined(O_CLOEXEC)
#define O_CLOEXEC 0
#endif


namespace oceanbase
{
namespace common
{
ObPLogItem::ObPLogItem()
  : ObIBaseLogItem(), ring_offset_(0),
    fd_type_(MAX_FD_FILE),
    log_level_(OB_LOG_LEVEL_NONE), tl_type_(common::OB_INVALID_INDEX), is_force_allow_(false),
    is_size_overflow_(false), timestamp_(0), header_pos_(0), buf_size_(0), pos_(0)
{
}

ObPLogFileStruct::ObPLogFileStruct()
  : fd_(STDERR_FILENO), write_count_(0), write_size_(0),
    file_size_(0)
{
#ifdef _WIN32
  filename_ = "";
#else
  filename_[0] = '\0';
#endif
  MEMSET(&stat_, 0, sizeof(stat_));
}

ObPLogFileStruct::~ObPLogFileStruct()
{
  close_all();
#ifdef _WIN32
  if (filename_[0] != '\0') {
    ob_free(const_cast<char *>(filename_));
  }
#endif
}

#ifdef _WIN32
int ObPLogFileStruct::needs_reopen(bool &changed) const
{
  int ret = OB_SUCCESS;
  changed = false;
  ObMalloc allocator("WindowsPath");
  WindowsFilePath path(allocator);
  if (OB_FAIL(path.assign(filename_))) {
    LOG_STDERR("invalid log identity path ret=%d win32=%lu\n", ret, path.win32_error());
  } else {
    HANDLE named = CreateFileW(path.wide(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (INVALID_HANDLE_VALUE == named) {
      const DWORD error = GetLastError();
      if (ERROR_FILE_NOT_FOUND == error || ERROR_PATH_NOT_FOUND == error) {
        changed = true;
      } else {
        ret = OB_IO_ERROR;
        LOG_STDERR("open log identity failed win32=%lu\n", error);
      }
    } else {
      BY_HANDLE_FILE_INFORMATION current_info = {};
      BY_HANDLE_FILE_INFORMATION named_info = {};
      // The CRT owns this handle; only the temporary query handle is closed here.
      const HANDLE current = reinterpret_cast<HANDLE>(_get_osfhandle(fd_));
      if (!GetFileInformationByHandle(current, &current_info)
          || !GetFileInformationByHandle(named, &named_info)) {
        const DWORD error = GetLastError();
        ret = OB_IO_ERROR;
        LOG_STDERR("query log identity failed win32=%lu\n", error);
      } else {
        changed = current_info.dwVolumeSerialNumber != named_info.dwVolumeSerialNumber
            || current_info.nFileIndexHigh != named_info.nFileIndexHigh
            || current_info.nFileIndexLow != named_info.nFileIndexLow;
      }
      CloseHandle(named);
    }
  }
  return ret;
}
#endif

int ObPLogFileStruct::open(const char *file_name, const bool redirect_flag)
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  ObMalloc path_allocator("WindowsPath");
  WindowsFilePath path(path_allocator);
  if (OB_FAIL(path.assign(file_name))) {
    LOG_STDERR("invalid log path ret=%d win32=%lu\n", ret, path.win32_error());
  } else {
    const size_t bytes = strlen(path.utf8()) + 1;
    char *candidate = static_cast<char *>(path_allocator.alloc(bytes));
    if (nullptr == candidate) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(candidate, path.utf8(), bytes);
      const char *previous = filename_;
      filename_ = candidate;
      if (OB_FAIL(reopen(redirect_flag))) {
        filename_ = previous;
        ob_free(candidate);
      } else if (previous[0] != '\0') {
        ob_free(const_cast<char *>(previous));
      }
    }
  }
#else
  size_t fname_len = 0;
  if (OB_ISNULL(file_name)) {
    LOG_STDERR("invalid argument log_file = %p\n", file_name);
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY((fname_len = strlen(file_name)) > MAX_LOG_FILE_NAME_SIZE - 5)) {
    LOG_STDERR("fname' size is overflow, log_file = %p\n", file_name);
    ret = OB_SIZE_OVERFLOW;
  } else {
    if (OB_UNLIKELY(is_opened())) {
      LOG_STDOUT("old log_file need close, old = %s new = %s\n", filename_, file_name);
    }
    MEMCPY(filename_, file_name, fname_len);
    filename_[fname_len] = '\0';
    if (OB_FAIL(reopen(redirect_flag))) {
      LOG_STDERR("reopen error, ret= %d\n", ret);
    }
  }
#endif
  return ret;
}


int ObPLogFileStruct::reopen(const bool redirect_flag)
{
  int ret = OB_SUCCESS;
  int32_t tmp_fd = -1;
  struct stat next_stat = {};
  if (OB_UNLIKELY(strlen(filename_) <= 0)) {
    LOG_STDERR("invalid argument log_file = %p\n", filename_);
    ret = OB_INVALID_ARGUMENT;
  }
#ifdef _WIN32
  else {
    ObMalloc path_allocator("WindowsPath");
    WindowsFilePath path(path_allocator);
    if (OB_FAIL(path.assign(filename_))) {
      LOG_STDERR("invalid log file=%s ret=%d win32=%lu\n",
                 filename_, ret, path.win32_error());
    } else {
      // Rotation renames the active file before replacing its descriptor.
      HANDLE handle = CreateFileW(path.wide(), GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (INVALID_HANDLE_VALUE == handle) {
        const DWORD error = GetLastError();
        ret = OB_IO_ERROR;
        LOG_STDERR("open log file=%s ret=%d win32=%lu\n", filename_, ret, error);
      } else if ((tmp_fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle),
                     _O_WRONLY | _O_APPEND | _O_BINARY | _O_NOINHERIT)) < 0) {
        const int error = errno;
        CloseHandle(handle); // Ownership transfers only on successful conversion.
        ret = OB_IO_ERROR;
        LOG_STDERR("adopt log handle file=%s errno=%d\n", filename_, error);
      }
    }
  }
  if (OB_FAIL(ret)) {
  }
#else
  else if (OB_UNLIKELY((tmp_fd = ::open(filename_, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC
          , LOG_FILE_MODE)) < 0)) {
    LOG_STDERR("open file = %s errno = %d error = %m\n", filename_, errno);
    ret = OB_ERR_UNEXPECTED;
  }
#endif
  else if (OB_UNLIKELY(0 != fstat(tmp_fd, &next_stat))) {
    LOG_STDERR("fstat file = %s error\n", filename_);
    ret = OB_ERR_UNEXPECTED;
    (void)close(tmp_fd);
    tmp_fd = -1;
  } else {
#ifdef _WIN32
    if ((redirect_flag && (dup2(tmp_fd, STDERR_FILENO) < 0 ||
                           dup2(tmp_fd, STDOUT_FILENO) < 0)) ||
        (fd_ > STDERR_FILENO && dup2(tmp_fd, fd_) < 0)) {
      ret = OB_IO_ERROR;
      LOG_STDERR("duplicate log descriptor file=%s errno=%d\n", filename_, errno);
      (void)close(tmp_fd);
    } else if (fd_ > STDERR_FILENO) {
      (void)close(tmp_fd);
    } else {
      fd_ = tmp_fd;
    }
#else
    if (redirect_flag) {
      (void)dup2(tmp_fd, STDERR_FILENO);
      (void)dup2(tmp_fd, STDOUT_FILENO);

      if (fd_ > STDERR_FILENO) {
        (void)dup2(tmp_fd, fd_);
        (void)close(tmp_fd);
      } else {
        fd_ = tmp_fd;
      }
    } else {
      if (fd_ > STDERR_FILENO) {
        (void)dup2(tmp_fd, fd_);
        (void)close(tmp_fd);
      } else {
        fd_ = tmp_fd;
      }
    }
#endif
    if (OB_SUCC(ret)) {
      stat_ = next_stat;
      file_size_ = stat_.st_size;
    }
  }
  return ret;
}

int ObPLogFileStruct::close_all()
{
  int ret = OB_SUCCESS;
  if (fd_ > STDERR_FILENO) {
    (void)close(fd_);
    fd_ = STDERR_FILENO;
  }
  return ret;
}

} // end common
} // end oceanbase
