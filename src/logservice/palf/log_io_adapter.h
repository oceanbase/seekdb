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

#pragma once

#include <optional>
#include "rust/storage_io.h"

namespace oceanbase::palf
{
using LogFile = std::optional<rust::Box<storage_io::NamedFile>>;

class LogIOAdapter
{
public:
  int init(const storage_io::NamedOwnerHandle *log_files);
  void destroy() { log_files_ = nullptr; }
  bool is_valid() const { return log_files_ != nullptr; }
  int open(const char *path, bool read_only, LogFile &file);
  int pwrite(const LogFile &file, const char *buf, int64_t count,
      int64_t offset, int64_t &write_size);
  int pread(const LogFile &file, int64_t count, int64_t offset,
      char *buf, int64_t &read_size);
  int pread_direct(const LogFile &file, int64_t count, int64_t offset,
      char *buf, int64_t &read_size);
  int truncate(const LogFile &file, int64_t length);

private:
  const storage_io::NamedOwnerHandle *log_files_ = nullptr;
};
}
