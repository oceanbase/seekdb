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

#include "storage/slog/ob_storage_log_struct.h"

namespace oceanbase
{
namespace storage
{
ObStorageLogParam::ObStorageLogParam()
  : cmd_(0),
    data_(nullptr),
    disk_addr_()
{
}

ObStorageLogParam::ObStorageLogParam(const int32_t cmd, ObIBaseStorageLogEntry *data)
  : cmd_(cmd),
    data_(data),
    disk_addr_()
{
}

void ObStorageLogParam::reset()
{
  cmd_ = 0;
  data_ = nullptr;
  disk_addr_.reset();
}

bool ObStorageLogParam::is_valid() const
{
  return cmd_ > 0
      && nullptr != data_
      && data_->is_valid();
}
}//end namespace blocksstable
}//end namespace oceanbase
