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

#ifndef OCEANBASE_SHARE_OB_STATVFS_WIN32_H_
#define OCEANBASE_SHARE_OB_STATVFS_WIN32_H_

#ifdef _WIN32

#include <windows.h>
#include <stdint.h>
#include <cstring>
#include <errno.h>
#include "lib/allocator/page_arena.h"
#include "lib/file/windows_file_path.h"
#include "lib/ob_errno.h"

struct statvfs {
  unsigned long f_bsize;
  unsigned long f_frsize;
  uint64_t f_blocks;
  uint64_t f_bfree;
  uint64_t f_bavail;
  uint64_t f_files;
  uint64_t f_ffree;
  uint64_t f_favail;
  unsigned long f_fsid;
  unsigned long f_flag;
  unsigned long f_namemax;
};

static inline int statvfs(const char *path, struct statvfs *buf)
{
  oceanbase::common::ObArenaAllocator allocator;
  oceanbase::common::WindowsFilePath wide_path(allocator);
  if (buf == nullptr) {
    errno = EINVAL;
    return -1;
  }
  const int ret = wide_path.assign(path);
  if (ret != oceanbase::common::OB_SUCCESS) {
    errno = ret == oceanbase::common::OB_SIZE_OVERFLOW ? ENAMETOOLONG
          : ret == oceanbase::common::OB_ALLOCATE_MEMORY_FAILED ? ENOMEM : EINVAL;
    return -1;
  }
  ULARGE_INTEGER free_bytes_available, total_bytes, total_free_bytes;
  if (!GetDiskFreeSpaceExW(wide_path.wide(), &free_bytes_available, &total_bytes, &total_free_bytes)) {
    const DWORD error = GetLastError();
    errno = error == ERROR_ACCESS_DENIED ? EACCES
          : (error == ERROR_PATH_NOT_FOUND || error == ERROR_FILE_NOT_FOUND) ? ENOENT : EIO;
    SetLastError(error);
    return -1;
  }
  // Query the actual directory's volume, including mounted volumes, without
  // truncating its path or treating two failed lookups as the same filesystem.
  HANDLE directory = CreateFileW(wide_path.wide(), 0,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  BY_HANDLE_FILE_INFORMATION info = {};
  DWORD error = ERROR_SUCCESS;
  if (directory == INVALID_HANDLE_VALUE) {
    error = GetLastError();
  } else {
    if (!GetFileInformationByHandle(directory, &info)) {
      error = GetLastError();
    }
    CloseHandle(directory);
  }
  if (error != ERROR_SUCCESS) {
    errno = error == ERROR_ACCESS_DENIED ? EACCES
          : (error == ERROR_PATH_NOT_FOUND || error == ERROR_FILE_NOT_FOUND) ? ENOENT : EIO;
    SetLastError(error);
    return -1;
  }
  memset(buf, 0, sizeof(*buf));
  buf->f_fsid = info.dwVolumeSerialNumber;
  buf->f_bsize = 4096;
  buf->f_frsize = 4096;
  buf->f_blocks = total_bytes.QuadPart / buf->f_frsize;
  buf->f_bfree = total_free_bytes.QuadPart / buf->f_frsize;
  buf->f_bavail = free_bytes_available.QuadPart / buf->f_frsize;
  return 0;
}

#endif /* _WIN32 */

#endif /* OCEANBASE_SHARE_OB_STATVFS_WIN32_H_ */
