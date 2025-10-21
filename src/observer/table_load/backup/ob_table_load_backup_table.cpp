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

#define USING_LOG_PREFIX SERVER

#include "ob_table_load_backup_table.h"
#include "observer/table_load/backup/v_1_4/ob_table_load_backup_table_v_1_4.h"

namespace oceanbase
{
namespace observer
{

int ObTableLoadBackupTable::get_table(ObTableLoadBackupVersion version, 
                                      ObTableLoadBackupTable *&table, 
                                      ObIAllocator &allocator) 
{ 
  int ret = OB_SUCCESS;
  table = nullptr;
  if (OB_UNLIKELY(version <= ObTableLoadBackupVersion::INVALID || 
                  version >= ObTableLoadBackupVersion::MAX_VERSION)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(version));
  } else {
    switch (version) {
      case ObTableLoadBackupVersion::V_1_4: {
        if (OB_ISNULL(table = OB_NEWx(ObTableLoadBackupTable_V_1_4, &allocator))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("fail to alloc memory", KR(ret));
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("not support version", KR(ret), K(version));
        break;
      }
    }
  }

  return ret;
}

} // namespace observer
} // namespace oceanbase
