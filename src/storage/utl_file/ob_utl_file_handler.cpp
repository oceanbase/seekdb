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

#define USING_LOG_PREFIX STORAGE
#include "storage/utl_file/ob_utl_file_handler.h"
#include "common/storage/ob_io_device.h"
#include "share/ob_io_device_helper.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace storage
{
const int ObUtlFileHandler::LINE_TERMINATOR_LEN = STRLEN(ObUtlFileHandler::LINE_TERMINATOR);




int ObUtlFileHandler::get_line_raw(const int64_t &fd, char *buffer, const int64_t len, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  ObIOFd io_fd(&LOCAL_DEVICE_INSTANCE, ObIOFd::NORMAL_FILE_ID, fd);
  int64_t pos = 0;
  if (OB_UNLIKELY(!io_fd.is_normal_file())) {
    ret = OB_UTL_FILE_INVALID_FILEHANDLE;
    LOG_WARN("invalid handle", K(ret), K(io_fd));
  } else if (OB_ISNULL(buffer) || OB_UNLIKELY(len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buffer), K(len));
  } else if (OB_FAIL(read_impl(fd, buffer, len, read_size))) {
    LOG_WARN("fail to get line", K(ret), K(len));
  } else if (0 == read_size) {
    ret = OB_READ_NOTHING;
    LOG_WARN("read size is 0", K(ret), K(fd), K(len));
  }
  return ret;
}










bool ObUtlFileHandler::is_valid_path(const char *path, size_t &path_len)
{
  return OB_NOT_NULL(path) && ((path_len = STRLEN(path)) > 0)
      && (path_len < ObUtlFileConstants::UTL_PATH_SIZE_LIMIT);
}

bool ObUtlFileHandler::is_valid_open_mode(const char *open_mode)
{
  bool b_ret = false;
  static const char *supported_open_mode[] = { "r", "w", "a", "rb", "wb", "ab" };
  for (int i = 0; i < sizeof(supported_open_mode) / sizeof(const char *); ++i) {
    if (0 == STRCASECMP(open_mode, supported_open_mode[i])) {
      b_ret = true;
      break;
    }
  }
  return b_ret;
}

int ObUtlFileHandler::convert_open_mode(const char *open_mode, int &flags)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(open_mode)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(open_mode));
  } else if (0 == STRCASECMP(open_mode, "r")) {
    flags = O_RDONLY;
  } else if (0 == STRCASECMP(open_mode, "w")) {
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  } else if (0 == STRCASECMP(open_mode, "a")) {
    flags = O_WRONLY | O_CREAT | O_APPEND;
  } else if (0 == STRCASECMP(open_mode, "rb")) {
    flags = O_RDONLY;
  } else if (0 == STRCASECMP(open_mode, "wb")) {
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  } else if (0 == STRCASECMP(open_mode, "ab")) {
    flags = O_WRONLY | O_CREAT | O_APPEND;
  } else {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported open mode", K(ret), K(open_mode));
  }
  return ret;
}

int ObUtlFileHandler::format_full_path(char *full_path, size_t len,
    const char *dir, const char *filename)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(dir) || OB_UNLIKELY(0 == STRLEN(dir))
      || OB_ISNULL(filename) || OB_UNLIKELY(0 == STRLEN(filename))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(dir), K(filename));
  } else {
    int p_ret = snprintf(full_path, len, "%s/%s", dir, filename);
    if (p_ret < 0 || p_ret >= len) {
      ret = OB_BUF_NOT_ENOUGH;
      LOG_WARN("file name too long", K(ret), K(dir), K(filename), K(len));
    }
  }
  return ret;
}

int ObUtlFileHandler::put_impl(const int64_t &fd, const char *buffer, const int64_t size,
                               int64_t &write_size, bool autoflush)
{
  int ret = OB_SUCCESS;
  ObIOFd io_fd(&LOCAL_DEVICE_INSTANCE, ObIOFd::NORMAL_FILE_ID, fd);
  if (OB_UNLIKELY(!io_fd.is_normal_file())) {
    ret = OB_UTL_FILE_INVALID_FILEHANDLE;
    LOG_WARN("invalid handle", K(ret), K(io_fd));
  } else if (OB_ISNULL(buffer) || OB_UNLIKELY(size < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buffer), K(size));
  } else if (OB_FAIL(LOCAL_DEVICE_INSTANCE.write(io_fd, buffer, size, write_size))) {
    LOG_WARN("fail to write", K(ret), K(size));
  } else if (autoflush && OB_FAIL(LOCAL_DEVICE_INSTANCE.fsync(io_fd))) {
    LOG_WARN("fail to flush", K(ret), K(io_fd));
  }
  return ret;
}

int ObUtlFileHandler::read_impl(const int64_t &fd, char *buffer, const int64_t len, int64_t &read_size)
{
  int ret = OB_SUCCESS;
  ObIOFd io_fd(&LOCAL_DEVICE_INSTANCE, ObIOFd::NORMAL_FILE_ID, fd);
  int retry_cnt = 0;
  int64_t size = len;
  while (OB_SUCC(ret)
      && size > 0
      && retry_cnt++ < ObUtlFileConstants::DEFAULT_IO_RETRY_CNT) {
    int64_t sz = 0;
    if (OB_FAIL(LOCAL_DEVICE_INSTANCE.read(io_fd, buffer, size, sz))) {
      LOG_WARN("fail to read", K(ret), K(size));
    } else {
      read_size += sz;
      size -= sz;
      buffer += sz;
    }
  }
  return ret;
}

int ObUtlFileHandler::find_single_line(const char *buffer, const int64_t len, int64_t &pos, bool &found)
{
  int ret = OB_SUCCESS;
  const char *p = NULL;
  found = false;
  if (OB_ISNULL(buffer) || OB_UNLIKELY(len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid line", K(buffer), K(len));
  } else if (NULL == (p = STRSTR(buffer, ObUtlFileHandler::LINE_TERMINATOR))) {
    // cannot find line terminator in buffer, so whole buffer is regarded as a single line
    pos = len;
    LOG_INFO("fail to find occurence of line terminator in buffer", K(ret), K(buffer), K(len),
        K(pos), K(found));
  } else {
    pos = p - buffer;
    found = true;
  }
  return ret;
}

int ObUtlFileHandler::find_and_copy_lines(const char *buffer, const int64_t len,
                                          const ObIOFd &dst_io_fd,
                                          const int64_t start_line, const int64_t end_line,
                                          int64_t &line_num)
{
  int ret = OB_SUCCESS;
  const char *write_buffer = buffer;
  int64_t write_buffer_begin = 0;
  int64_t write_buffer_end = 0;
  int64_t write_buffer_size = 0;
  int64_t write_size = 0;

  if (OB_ISNULL(buffer) || OB_UNLIKELY(len < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid line", K(buffer), K(len));
  } else {
    int64_t line_start_pos = 0;
    int64_t line_feed_pos = -1;
    const char *single_line = buffer;
    int64_t buffer_len = len;
    bool found = false;
    while (OB_SUCC(ret) && line_start_pos < len && line_num <= end_line) {
      if (line_num == start_line) {
        write_buffer = single_line;
        write_buffer_begin = line_start_pos;
        write_buffer_end = line_start_pos;
      }

      if (OB_FAIL(find_single_line(single_line, buffer_len, line_feed_pos, found))) {
        LOG_WARN("failed to find single line", K(ret), K(single_line), K(buffer_len));
      } else if (found) {
        if (line_num >= start_line && line_num <= end_line) {
          write_buffer_end += (line_feed_pos + 1);
        }
        single_line += (line_feed_pos + 1);
        line_start_pos += (line_feed_pos + 1);
        buffer_len = len - line_start_pos;
        ++line_num;
      } else {
        // cannot find line feed in buffer
        if (line_num >= start_line && line_num <= end_line) {
          write_buffer_end = len;
        }
        break;
      }
    }
  }

  // write buffer to dst file
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (FALSE_IT(write_buffer_size = write_buffer_end - write_buffer_begin)) {
    // do nothing
  } else if (write_buffer_size > 0
      && OB_FAIL(LOCAL_DEVICE_INSTANCE.write(dst_io_fd, write_buffer, write_buffer_size, write_size))) {
    LOG_WARN("fail to write to dst file", K(ret), K(dst_io_fd), K(write_buffer), K(write_buffer_size));
  }

  return ret;
}

int ObUtlFileHandler::copy_lines(const ObIOFd &src_io_fd, const ObIOFd &dst_io_fd,
                                 const int64_t start_line, const int64_t end_line)
{
  int ret = OB_SUCCESS;
  int64_t line_num = 1;
  int64_t offset = 0;
  int64_t read_size = 0;

  const int64_t size = ObUtlFileConstants::UTF_FILE_WRITE_BUFFER_SIZE;
  SMART_VAR(char[size], data) {
    do {
      if (OB_FAIL(LOCAL_DEVICE_INSTANCE.pread(src_io_fd, offset, size, data, read_size))) {
        LOG_WARN("fail to read", K(ret), K(src_io_fd), K(offset), K(size));
        ret = OB_UTL_FILE_READ_ERROR;
      } else if (0 == read_size) {
        // do nothing
      } else if (OB_FAIL(find_and_copy_lines(data, read_size, dst_io_fd,
          start_line, end_line, line_num))) {
        LOG_WARN("fail to find lines", K(ret), K(read_size), K(dst_io_fd),
            K(start_line), K(end_line), K(line_num));
      } else {
        offset += read_size;
      }
    } while (OB_SUCC(ret) && 0 != read_size && line_num <= end_line);
  }
  if (OB_SUCC(ret) && start_line > line_num) {
    ret = OB_UTL_FILE_INVALID_OFFSET;
    LOG_WARN("invalid offset, start line is bigger than file line count", K(ret));
  }
  return ret;
}

int ObUtlFileHandler::check_buffer(const char *buffer, const int64_t size,
                                   const int64_t max_line_size, int64_t &last_valid_pos)
{
  int ret = OB_SUCCESS;
  int64_t p = 0, q = 0;
  while (p <= q && q < size) {
    if (buffer[q] == '\n') {
      int64_t single_line_size = q - p + 1;
      if (single_line_size > max_line_size) {
        ret = OB_UTL_FILE_WRITE_ERROR;
        LOG_WARN("invalid single line size", K(ret), K(buffer), K(p),
            K(single_line_size), K(max_line_size));
        break;
      } else {
        last_valid_pos = q;
        p = ++q;
      }
    } else {
      ++q;
    }
  }
  // for fflush check buffer size(need_crlf is false)
  if (OB_SUCC(ret) && q == size && '\n' != buffer[size - 1]) {
    int64_t left_size = q - p;
    if (left_size + 1 > max_line_size) {
      // 1 for '\n'
      ret = OB_UTL_FILE_WRITE_ERROR;
      LOG_WARN("invalid single line size", K(ret), K(buffer), K(p), K(q),
          K(left_size), K(max_line_size));
    }
  }
  return ret;
}
} // namespace storage
} // namespace oceanbase
