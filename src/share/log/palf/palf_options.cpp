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

#include "share/log/palf/palf_options.h"

namespace oceanbase
{
namespace palf
{
void PalfOptions::reset()
{
  disk_options_.reset();
}

bool PalfOptions::is_valid() const
{
  return disk_options_.is_valid();
}

void PalfDiskOptions::reset()
{
  log_disk_usage_limit_size_ = -1;
}

bool PalfDiskOptions::is_valid() const
{
  return 0 <= log_disk_usage_limit_size_;
}

bool PalfDiskOptions::operator==(const PalfDiskOptions &palf_disk_options) const
{
  return log_disk_usage_limit_size_ == palf_disk_options.log_disk_usage_limit_size_;
}

bool PalfDiskOptions::operator!=(const PalfDiskOptions &palf_disk_options) const
{
  return !this->operator==(palf_disk_options);
}

PalfDiskOptions &PalfDiskOptions::operator=(const PalfDiskOptions &other)
{
  log_disk_usage_limit_size_ = other.log_disk_usage_limit_size_;
  return *this;
}

static const char *access_mode_strs[] = {
  "INVALID_ACCESS_MODE",
  "APPEND"
};

int get_access_mode(const common::ObString &str, AccessMode &mode)
{
  int ret = OB_SUCCESS;
  if (str.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    mode = AccessMode::INVALID_ACCESS_MODE;
    for (int64_t i = 0; i < ARRAYSIZEOF(access_mode_strs); i++) {
      if (0 == str.case_compare(access_mode_strs[i])) {
        mode = static_cast<AccessMode>(i);
        break;
      }
    }
  }
  if (AccessMode::INVALID_ACCESS_MODE == mode) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

}// end of namespace palf
}// end of namespace oceanbase 
