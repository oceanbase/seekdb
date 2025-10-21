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
#include "deps/oblib/src/lib/allocator/ob_slice_alloc.h"


namespace oceanbase
{
namespace common
{
ObPLogItem::ObPLogItem()
  : ObIBaseLogItem(), fd_type_(MAX_FD_FILE),
    log_level_(OB_LOG_LEVEL_NONE), tl_type_(common::OB_INVALID_INDEX), is_force_allow_(false),
    is_size_overflow_(false), timestamp_(0), header_pos_(0), buf_size_(0), pos_(0)
{
}

ObPLogFileStruct::ObPLogFileStruct()
  : fd_(STDERR_FILENO), write_count_(0), write_size_(0),
    file_size_(0)
{
  filename_[0] = '\0';
  MEMSET(&stat_, 0, sizeof(stat_));
}

int ObPLogFileStruct::open(const char *file_name, const bool redirect_flag)
{
  int ret = OB_SUCCESS;
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
  return ret;
}


int ObPLogFileStruct::reopen(const bool redirect_flag)
{
  int ret = OB_SUCCESS;
  int32_t tmp_fd = -1;
  if (OB_UNLIKELY(strlen(filename_) <= 0)) {
    LOG_STDERR("invalid argument log_file = %p\n", filename_);
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY((tmp_fd = ::open(filename_, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, LOG_FILE_MODE)) < 0)) {
    LOG_STDERR("open file = %s errno = %d error = %m\n", filename_, errno);
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(0 != fstat(tmp_fd, &stat_))) {
    LOG_STDERR("fstat file = %s error\n", filename_);
    ret = OB_ERR_UNEXPECTED;
    (void)close(tmp_fd);
    tmp_fd = -1;
  } else {
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
    file_size_ = stat_.st_size;
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
