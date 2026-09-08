// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#ifndef OCEANBASE_LIB_FILE_WASM_FILE_H_
#define OCEANBASE_LIB_FILE_WASM_FILE_H_

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace oceanbase::common::wasm {
// Equivalent file contents to FALLOC_FL_ZERO_RANGE, without Linux fallocate.
// Preserve bytes outside the range and propagate failed/zero-length writes.
// Flushing remains the caller's responsibility, as with native fallocate.
inline int zero_file_range(int fd, int64_t offset, int64_t length)
{
  if (offset < 0 || length <= 0 || offset > INT64_MAX - length) {
    errno = EINVAL;
    return -1;
  }
  constexpr size_t buffer_size = 64 * 1024;
  void *zeros = calloc(1, buffer_size);
  if (zeros == nullptr) {
    errno = ENOMEM;
    return -1;
  }
  int error = 0;
  while (length > 0 && error == 0) {
    const size_t count = length < buffer_size ? static_cast<size_t>(length) : buffer_size;
    const ssize_t written = pwrite(fd, zeros, count, offset);
    if (written < 0) {
      if (errno != EINTR) error = errno;
    } else if (written == 0) {
      error = EIO;
    } else {
      offset += written;
      length -= written;
    }
  }
  free(zeros);
  if (error != 0) errno = error;
  return error == 0 ? 0 : -1;
}

// Grow an exclusively owned file by writing its missing tail. Unlike
// ftruncate, this exercises the backend's write/allocation error path. A failed
// write may leave a partial tail; retrying preserves it and all existing data.
// This is not a physical-space reservation or a durability boundary. The file
// owner must serialize growth/truncation and perform its normal flush protocol.
inline int extend_file_with_zeros(int fd, int64_t size)
{
  if (size <= 0) {
    errno = EINVAL;
    return -1;
  }
  struct stat st{};
  if (fstat(fd, &st) != 0) return -1;
  if (!S_ISREG(st.st_mode)) {
    errno = EINVAL;
    return -1;
  }
  return st.st_size >= size ? 0 : zero_file_range(fd, st.st_size, size - st.st_size);
}
} // namespace oceanbase::common::wasm
#endif
