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

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include "wasmfs.h"

extern "C" int __wrap___syscall_dup3(int oldfd, int newfd, int flags)
{
  if ((flags & ~O_CLOEXEC) != 0 || oldfd == newfd) return -EINVAL;
  std::shared_ptr<wasmfs::DataFile> replaced;
  {
    auto table = wasmfs::wasmFS.getFileTable().locked();
    const auto source = table.getEntry(oldfd);
    if (!source || newfd < 0 || newfd >= WASMFS_FD_MAX) return -EBADF;
    replaced = table.setEntry(newfd, source);
  }
  if (replaced) (void)replaced->locked().close();
  return newfd;
}
