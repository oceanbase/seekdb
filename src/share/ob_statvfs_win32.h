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
  ULARGE_INTEGER free_bytes_available, total_bytes, total_free_bytes;
  if (!GetDiskFreeSpaceExA(path, &free_bytes_available, &total_bytes, &total_free_bytes)) {
    return -1;
  }
  memset(buf, 0, sizeof(*buf));
  buf->f_bsize = 4096;
  buf->f_frsize = 4096;
  buf->f_blocks = total_bytes.QuadPart / buf->f_frsize;
  buf->f_bfree = total_free_bytes.QuadPart / buf->f_frsize;
  buf->f_bavail = free_bytes_available.QuadPart / buf->f_frsize;
  return 0;
}

#endif /* _WIN32 */

#endif /* OCEANBASE_SHARE_OB_STATVFS_WIN32_H_ */
