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

#define USING_LOG_PREFIX STORAGE
#include "ob_storage_object_rw_info.h"
#include "storage/backup/ob_backup_device_wrapper.h"

namespace oceanbase
{
namespace blocksstable
{

int ObStorageObjectWriteInfo::fill_io_info_for_backup(const blocksstable::MacroBlockId &macro_id, ObIOInfo &io_info) const
{
  int ret = OB_SUCCESS;
  if (!backup::ObBackupDeviceMacroBlockId::is_backup_block_file(macro_id.first_id())) {
    // do nothing
  } else if (!has_backup_device_handle_) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "device handle should not be null", K(ret));
  } else {
    backup::ObBackupWrapperIODevice *device = static_cast<backup::ObBackupWrapperIODevice *>(device_handle_);
    io_info.fd_.fd_id_ = device->simulated_fd_id();
    io_info.fd_.slot_version_ = device->simulated_slot_version();
  }
  return ret;
}

}
}
