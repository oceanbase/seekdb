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
#include "rust/file_io.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>

namespace oceanbase::share
{
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
  const auto result = file_io::probe_directory(
      {reinterpret_cast<const uint8_t *>(directory), std::strlen(directory)});
  if (result.error != 0) {
    return result.error;
  }
  LocalIOConfig detected;
  switch (result.capability) {
    case file_io::DirectoryCapabilities::BufferedWorker:
      break;
    case file_io::DirectoryCapabilities::DirectWorker:
      detected.direct_io = true;
      break;
    case file_io::DirectoryCapabilities::DirectNativeAio:
      detected.direct_io = true;
      detected.backend = LocalIOBackend::NATIVE_AIO;
      break;
    default:
      return EIO;
  }
  config = detected;
  return 0;
}
}
