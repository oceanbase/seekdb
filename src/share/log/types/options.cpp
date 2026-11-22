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

#include "share/log/types/options.h"

namespace oceanbase
{
namespace wal
{
void Options::reset()
{
  disk_options_.reset();
}

bool Options::is_valid() const
{
  return disk_options_.is_valid();
}

void DiskOptions::reset()
{
  log_disk_usage_limit_size_ = -1;
}

bool DiskOptions::is_valid() const
{
  return 0 <= log_disk_usage_limit_size_;
}

bool DiskOptions::operator==(const DiskOptions &wal_disk_options) const
{
  return log_disk_usage_limit_size_ == wal_disk_options.log_disk_usage_limit_size_;
}

bool DiskOptions::operator!=(const DiskOptions &wal_disk_options) const
{
  return !this->operator==(wal_disk_options);
}

DiskOptions &DiskOptions::operator=(const DiskOptions &other)
{
  log_disk_usage_limit_size_ = other.log_disk_usage_limit_size_;
  return *this;
}

}// end of namespace wal
}// end of namespace oceanbase
