/**
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

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace
{

bool fcntl_cmd_requires_arg(const int cmd)
{
  switch (cmd) {
    case F_GETFD:
    case F_GETFL:
#ifdef F_GETOWN
    case F_GETOWN:
#endif
#ifdef F_GETSIG
    case F_GETSIG:
#endif
#ifdef F_GETLEASE
    case F_GETLEASE:
#endif
#ifdef F_GETPIPE_SZ
    case F_GETPIPE_SZ:
#endif
#ifdef F_GET_SEALS
    case F_GET_SEALS:
#endif
#ifdef F_GET_RW_HINT
    case F_GET_RW_HINT:
#endif
#ifdef F_GET_FILE_RW_HINT
    case F_GET_FILE_RW_HINT:
#endif
      return false;
    default:
      return true;
  }
}

int read_entropy_from_urandom(char *buf, size_t len)
{
  int ret = -1;
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
  } else {
    size_t pos = 0;
    while (pos < len) {
      ssize_t read_len = read(fd, buf + pos, len - pos);
      if (read_len > 0) {
        pos += static_cast<size_t>(read_len);
      } else if (0 == read_len) {
        errno = EIO;
        break;
      } else if (EINTR != errno) {
        break;
      }
    }
    if (pos == len) {
      ret = 0;
    }
    close(fd);
  }
  return ret;
}

} // namespace

extern "C" int getentropy(void *buffer, size_t length)
{
  int ret = 0;
  char *buf = static_cast<char *>(buffer);
  if (nullptr == buffer) {
    errno = EFAULT;
    ret = -1;
  }
#ifdef SYS_getrandom
  else {
    size_t pos = 0;
    while (pos < length) {
      long read_len = syscall(SYS_getrandom, buf + pos, length - pos, 0);
      if (read_len > 0) {
        pos += static_cast<size_t>(read_len);
      } else if (-1 == read_len && EINTR != errno) {
        if (ENOSYS == errno) {
          ret = read_entropy_from_urandom(buf, length);
        } else {
          ret = -1;
        }
        break;
      }
    }
  }
#else
  else {
    ret = read_entropy_from_urandom(buf, length);
  }
#endif
  return ret;
}

extern "C" int fcntl64(int fd, int cmd, ...)
{
  int ret = -1;
  va_list ap;
  va_start(ap, cmd);
  if (fcntl_cmd_requires_arg(cmd)) {
    long arg = va_arg(ap, long);
    ret = fcntl(fd, cmd, arg);
  } else {
    ret = fcntl(fd, cmd);
  }
  va_end(ap);
  return ret;
}
