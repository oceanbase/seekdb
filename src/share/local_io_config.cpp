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

#include "share/local_io_config.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <linux/aio_abi.h>
#include <sys/syscall.h>
#endif

namespace oceanbase::share
{
namespace
{
constexpr size_t PROBE_SIZE = 4096;

bool unsupported(const int error)
{
  return error == EINVAL || error == EOPNOTSUPP || error == ENOSYS;
}

int sync_round_trip(const int fd, char *buffer)
{
  std::memset(buffer, 0x5a, PROBE_SIZE);
  // The probe owns this descriptor; seek + read/write also works on Windows.
  if (::lseek(fd, 0, SEEK_SET) < 0) {
    return errno;
  }
  int64_t count;
  do {
    count = ::write(fd, buffer, PROBE_SIZE);
  } while (count < 0 && errno == EINTR);
  if (count < 0) {
    return errno;
  } else if (count != PROBE_SIZE) {
    return EIO;
  }
  std::memset(buffer, 0, PROBE_SIZE);
  if (::lseek(fd, 0, SEEK_SET) < 0) {
    return errno;
  }
  do {
    count = ::read(fd, buffer, PROBE_SIZE);
  } while (count < 0 && errno == EINTR);
  if (count < 0) {
    return errno;
  } else if (count != PROBE_SIZE) {
    return EIO;
  }
  for (size_t i = 0; i < PROBE_SIZE; ++i) {
    if (buffer[i] != 0x5a) {
      return EIO;
    }
  }
  return 0;
}

#if defined(__linux__) && !defined(__ANDROID__)
int native_io(const aio_context_t context, struct iocb &cb,
              const int fd, char *buffer, const bool write)
{
  cb = {};
  cb.aio_fildes = fd;
  cb.aio_lio_opcode = write ? IOCB_CMD_PWRITE : IOCB_CMD_PREAD;
  cb.aio_buf = reinterpret_cast<uintptr_t>(buffer);
  cb.aio_nbytes = PROBE_SIZE;
  struct iocb *cbs[] = {&cb};
  long count;
  do {
    count = ::syscall(SYS_io_submit, context, 1, cbs);
  } while (count < 0 && errno == EINTR);
  if (count < 0) {
    return errno;
  } else if (count != 1) {
    return EAGAIN;
  }
  struct io_event event = {};
  do {
    count = ::syscall(SYS_io_getevents, context, 1, 1, &event, nullptr);
  } while (count < 0 && errno == EINTR);
  if (count < 0) {
    return errno;
  } else if (count != 1 || event.obj != reinterpret_cast<uintptr_t>(&cb) || event.res2 != 0) {
    return EIO;
  } else if (event.res < 0) {
    return -event.res;
  }
  return event.res == PROBE_SIZE ? 0 : EIO;
}
#endif

int probe_native_aio(const int fd, char *buffer, bool &available)
{
  available = false;
  int error = 0;
#if defined(__linux__) && !defined(__ANDROID__)
  aio_context_t context = 0;
  if (::syscall(SYS_io_setup, 1, &context) < 0) {
    // EAGAIN here means the system-wide AIO quota was exhausted.
    error = errno == ENOSYS ? 0 : errno;
  } else {
    struct iocb cb = {};
    std::memset(buffer, 0x36, PROBE_SIZE);
    error = native_io(context, cb, fd, buffer, true);
    if (error == 0) {
      std::memset(buffer, 0, PROBE_SIZE);
      error = native_io(context, cb, fd, buffer, false);
      if (error == 0) {
        for (size_t i = 0; i < PROBE_SIZE; ++i) {
          if (buffer[i] != 0x36) {
            error = EIO;
            break;
          }
        }
      }
    }
    available = error == 0;
    if (unsupported(error)) {
      error = 0;
    }
    // Destroy waits for any submitted I/O before its buffer leaves scope.
    if (::syscall(SYS_io_destroy, context) < 0 && error == 0) {
      error = errno;
    }
  }
#endif
  return error;
}
}

int LocalIOConfig::file_flags(int flags) const
{
#if defined(O_DIRECT) && O_DIRECT != 0
  flags = direct_io ? flags : (flags & ~O_DIRECT);
#endif
  return flags;
}

int probe_local_io(const char *directory, LocalIOConfig &config)
{
  if (directory == nullptr || directory[0] == '\0') {
    return EINVAL;
  }
  std::string path = std::string(directory) + "/.seekdb-io-probe-XXXXXX";
#ifdef _WIN32
  int error = _mktemp_s(path.data(), path.size() + 1);
  int fd = error == 0 ? ::open(path.c_str(), O_CREAT | O_EXCL | O_RDWR | _O_BINARY | _O_TEMPORARY, 0600) : -1;
#else
  int fd = ::mkstemp(path.data());
  int error = 0;
#endif
  if (fd < 0) {
    return error != 0 ? error : errno;
  }
  // Keep the descriptor returned by exclusive creation. Remove its name before
  // any I/O so an interrupted probe does not leave a file in the log directory.
#ifndef _WIN32
  if (::unlink(path.c_str()) < 0) {
    error = errno;
  }
#endif
  LocalIOConfig detected;
#if defined(O_DIRECT) && O_DIRECT != 0
  const int flags = ::fcntl(fd, F_GETFL);
  if (error == 0) {
    if (flags < 0) {
      error = errno;
    } else if (::fcntl(fd, F_SETFL, flags | O_DIRECT) == 0) {
      detected.direct_io = true;
    } else if (!unsupported(errno)) {
      error = errno;
    }
  }
#endif
  alignas(PROBE_SIZE) char buffer[PROBE_SIZE];
  if (error == 0) {
    error = sync_round_trip(fd, buffer);
#if defined(O_DIRECT) && O_DIRECT != 0
    if (error != 0 && detected.direct_io && unsupported(error)) {
      if (::fcntl(fd, F_SETFL, flags & ~O_DIRECT) < 0) {
        error = errno;
      } else {
        detected.direct_io = false;
        error = sync_round_trip(fd, buffer);
      }
    }
#endif
  }
  if (error == 0 && detected.direct_io) {
    bool native_aio = false;
    error = probe_native_aio(fd, buffer, native_aio);
    if (error == 0 && native_aio) {
      detected.backend = LocalIOBackend::NATIVE_AIO;
    }
  }
  if (error == 0) {
#ifdef _WIN32
    error = ::_commit(fd) == 0 ? 0 : errno;
#else
    error = ::fsync(fd) == 0 ? 0 : errno;
#endif
  }
  if (::close(fd) < 0 && error == 0) {
    error = errno;
  }
  if (error == 0) {
    config = detected;
  }
  return error;
}
}
