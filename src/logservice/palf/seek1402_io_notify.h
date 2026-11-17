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

#ifndef OCEANBASE_PALF_SEEK1402_IO_NOTIFY_H_
#define OCEANBASE_PALF_SEEK1402_IO_NOTIFY_H_
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__linux__)
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace oceanbase { namespace palf {
// Diagnostic transport only. A notification never establishes PALF backpressure.
struct Seek1402IONotify {
  bool requested = false;
  int fd = -1;
  std::atomic<uint64_t> attempts{0};
  std::atomic<uint64_t> sent{0};
  std::atomic<uint64_t> lost{0};

  // Called once before worker threads start; socket lifetime ends at writer exit.
  void initialize() {
    const char *endpoint = std::getenv("SEEK1402_IO_NOTIFY_ENDPOINT");
    if (endpoint == nullptr || *endpoint == '\0') return;
    requested = true;
#if defined(__linux__)
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const size_t length = std::strlen(endpoint);
    bool valid = endpoint[0] == '@' && length > 1 && length <= sizeof(address.sun_path);
    for (size_t i = 1; valid && i < length; ++i) {
      const char c = endpoint[i];
      valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
    }
    if (valid) {
      std::memcpy(address.sun_path + 1, endpoint + 1, length - 1);
      fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
      if (fd >= 0 && ::connect(fd, reinterpret_cast<sockaddr *>(&address),
                              offsetof(sockaddr_un, sun_path) + length) != 0) {
        ::close(fd);
        fd = -1;
      }
    }
#endif
    if (fd < 0) lost.fetch_add(1, std::memory_order_relaxed);
  }

  void append_entered(uint64_t slot, int64_t epoch) {
    if (!requested) return;
    const int saved_errno = errno;
    const uint64_t attempt = attempts.fetch_add(1, std::memory_order_relaxed);
    bool delivered = false;
    if (attempt < 8192 && slot < 8192 && epoch >= 0 && fd >= 0) {
#if defined(__linux__)
      const uint64_t frame[3] = {1, slot, static_cast<uint64_t>(epoch)};
      delivered = ::send(fd, frame, sizeof(frame), MSG_DONTWAIT | MSG_NOSIGNAL) == sizeof(frame);
#endif
    }
    if (delivered) sent.fetch_add(1, std::memory_order_relaxed);
    else lost.fetch_add(1, std::memory_order_relaxed);
    errno = saved_errno;
  }
};
} }
#endif
