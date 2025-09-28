/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "common/log/ob_log_data_writer.h"
#include "lib/file/ob_file.h"
#include "common/log/ob_log_dir_scanner.h"
#include "common/log/ob_log_generator.h"

namespace oceanbase
{
namespace common
{
ObLogDataWriter::AppendBuffer::AppendBuffer()
    : file_pos_(-1),
      buf_(NULL),
      buf_end_(0),
      buf_limit_(DEFAULT_BUF_SIZE)
{}

ObLogDataWriter::AppendBuffer::~AppendBuffer()
{
  if (NULL != buf_) {
    free(buf_);
    buf_ = NULL;
  }
}


int ObLogDataWriter::AppendBuffer::flush(int fd)
{
  int ret = OB_SUCCESS;
  if (fd < 0) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(ERROR, "invalid argument", K(fd), K(ret));
  } else if (buf_end_ <= 0) {
    // do nothing
  } else if (unintr_pwrite(fd, buf_, buf_end_, file_pos_) != buf_end_) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "uniter_pwrite error", K(fd), KP(buf_), K(buf_end_),
              K(file_pos_), KERRMSG, K(ret));
  } else {
    file_pos_ = -1;
    buf_end_ = 0;
  }
  return ret;
}


ObLogDataWriter::ObLogDataWriter():
    write_buffer_(),
    log_dir_(NULL),
    file_size_(0),
    end_cursor_(),
    log_sync_type_(OB_LOG_SYNC),
    fd_(-1), cur_file_id_(-1),
    num_file_to_add_(-1), min_file_id_(0),
    min_avail_file_id_(-1),
    min_avail_file_id_getter_(NULL)
{
}

ObLogDataWriter::~ObLogDataWriter()
{
  int ret = OB_SUCCESS;
  if (NULL != log_dir_) {
    free((void *)log_dir_);
    log_dir_ = NULL;
  }
  if (fd_ > 0) {
    if (OB_LOG_NOSYNC == log_sync_type_ && OB_FAIL(write_buffer_.flush(fd_))) {
      SHARE_LOG(ERROR, "write_buffer_ flush error", K(fd_), K(ret));
    }
    if (0 != close(fd_)) {
      SHARE_LOG(ERROR, "close error", K(fd_), KERRMSG);
    }
  }
}




int myfallocate_by_append(int fd, int mode, off_t offset, off_t len)
{
  int ret = 0;
  static char buf[1 << 20] __attribute__((aligned(ObLogConstants::LOG_FILE_ALIGN_SIZE)));
  int64_t count = 0;
  UNUSED(mode);
  if (fd < 0) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(ERROR, "invalid argument", K(fd), K(ret));
  } else if (offset < 0 || len <= 0) {
    errno = -EINVAL;
    ret = -1;
  }
  for (int64_t pos = offset; 0 == ret && pos < offset + len; pos += count) {
    count = min(offset + len - pos, static_cast<int64_t>(sizeof(buf)));
    if (unintr_pwrite(fd, buf, count, pos) != count) {
      ret = -1;
      SHARE_LOG(ERROR, "uniter_pwrite fail", K(pos), K(count), K(ret));
      break;
    }
  }
  return ret;
}
# if __linux && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 10))
int myfallocate(int fd, int mode, off_t offset, off_t len)
{
  int ret = 0;
  static bool syscall_supported = true;
  if (syscall_supported && 0 != (ret = fallocate(fd, mode, offset, len))) {
    syscall_supported = false;
    SHARE_LOG(WARN, "glibc support fallocate(), but fallocate still fail, "
              "fallback to call myfallocate_by_append()",
              KERRMSG);
  }
  if (!syscall_supported) {
    ret = myfallocate_by_append(fd, mode, offset, len);
  }
  return ret;
}
#else
int myfallocate(int fd, int mode, off_t offset, off_t len)
{
  return myfallocate_by_append(fd, mode, offset, len);
}
#endif
int file_expand_by_fallocate(const int fd, const int64_t file_size)
{
  int ret = OB_SUCCESS;
  struct stat st;
  if (fd < 0 || file_size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(ERROR, "invalid argument", K(fd), K(file_size), K(ret));
  } else if (0 != fstat(fd, &st)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "fstat failed", K(fd), KERRMSG);
  } else if (0 != (st.st_size & ObLogConstants::LOG_FILE_ALIGN_MASK)
             || 0 != (file_size & ObLogConstants::LOG_FILE_ALIGN_MASK)) {
    ret = OB_LOG_NOT_ALIGN;
    _SHARE_LOG(ERROR, "file_size[%ld] or file_size[%ld] not align by %lx",
               st.st_size, file_size,
               ObLogConstants::LOG_FILE_ALIGN_MASK);
  } else if (st.st_size >= file_size) {
    //do nothing
  } else if (0 != myfallocate(fd, 0, st.st_size, file_size - st.st_size)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "fallocate error", K(fd), KERRMSG);
  } else if (0 != fsync(fd)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "fsync error", K(fd), KERRMSG);
  } else {}
  return ret;
}

// The original size of the file needs to be 512-byte aligned



int ObLogDataWriter::reuse(const char *pool_file, const char *fname)
{
  int ret = OB_SUCCESS;
  char tmp_pool_file[OB_MAX_FILE_NAME_LENGTH];
  int64_t len = 0;
  int fd = -1;
  // Open and rename are allowed to fail, but pwrite() is not allowed to fail.
  if (OB_ISNULL(pool_file) || OB_ISNULL(fname)) {
    ret = OB_INVALID_ARGUMENT;
    SHARE_LOG(WARN, "invalid argument", KP(pool_file), KP(fname), K(ret));
  } else if ((len = snprintf(tmp_pool_file,
                             sizeof(tmp_pool_file),
                             "%s.tmp", pool_file)) < 0
             || len >= (int64_t)sizeof(tmp_pool_file)) {
    ret = OB_BUF_NOT_ENOUGH;
  } else if (0 != rename(pool_file, tmp_pool_file)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(WARN, "rename error", KCSTRING(pool_file), KCSTRING(tmp_pool_file), KERRMSG);
  } else if ((fd = open(tmp_pool_file, OPEN_FLAG, OPEN_MODE)) < 0) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "open file failed", KCSTRING(tmp_pool_file), KERRMSG);
  } else if (unintr_pwrite(fd, ObLogGenerator::eof_flag_buf_,
                           sizeof(ObLogGenerator::eof_flag_buf_),
                           0) != sizeof(ObLogGenerator::eof_flag_buf_)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "write_eof_flag fail", KCSTRING(tmp_pool_file), KERRMSG);
  } else if (0 != fsync(fd)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "fdatasync error", KCSTRING(tmp_pool_file), KERRMSG);
  } else if (0 != rename(tmp_pool_file, fname)) {
    ret = OB_IO_ERROR;
    SHARE_LOG(ERROR, "rename error", KCSTRING(pool_file), KCSTRING(fname), KERRMSG);
  }
  if (OB_SUCCESS != ret && fd > 0) {
    if (0 != close(fd)) {
      SHARE_LOG(ERROR, "close error", K(fd), KERRMSG);
    } else {
      fd = -1;
    }
  }
  return fd;
}

const char *ObLogDataWriter::select_pool_file(char *buf, const int64_t buf_len)
{
  char *result = NULL;
  int64_t len = 0;
  if (OB_ISNULL(buf) || buf_len < 0) {
    SHARE_LOG_RET(WARN, OB_INVALID_ARGUMENT, "invalid argument", KP(buf), K(buf_len));
  } else if (OB_ISNULL(min_avail_file_id_getter_)) {
    // do nothing
  } else if (0 == min_file_id_) {
    num_file_to_add_--;
    SHARE_LOG(INFO, "min_file_id has not inited, can not reuse file, "
              "will create new file num_file_to_add", K(num_file_to_add_));
  } else if (min_file_id_ < 0) {
    SHARE_LOG_RET(ERROR, OB_ERROR, "min_file_id < 0", K(min_file_id_));
  } else if (num_file_to_add_ > 0) {
    num_file_to_add_--;
    SHARE_LOG(INFO, "num_file_to_add >= 0 will create new file.", K(num_file_to_add_));
  } else if (min_file_id_ > min_avail_file_id_
             && min_file_id_ > (min_avail_file_id_ = min_avail_file_id_getter_->get())) {
    SHARE_LOG_RET(WARN, OB_ERROR, "can not select pool_file", K(min_file_id_), K(min_avail_file_id_));
  } else if ((len = snprintf(buf, buf_len, "%s/%ld", log_dir_, min_file_id_)) < 0
             || len >= buf_len) {
    SHARE_LOG_RET(ERROR, OB_ERROR, "gen fname fail", K(buf_len), KCSTRING(log_dir_), K(min_file_id_));
  } else {
    result = buf;
    min_file_id_++;
  }
  SHARE_LOG(INFO, "select_pool_file", K(num_file_to_add_), K(min_file_id_),
            K(min_avail_file_id_), KCSTRING(result));
  return result;
}




}; // end namespace common
}; // end namespace oceanbase
