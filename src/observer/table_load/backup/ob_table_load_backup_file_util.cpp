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

#include "observer/table_load/backup/ob_table_load_backup_file_util.h"
#include "share/backup/ob_backup_io_adapter.h"
#include "share/table/ob_table_load_define.h"

namespace oceanbase
{
namespace observer
{
using namespace common;

int ObTableLoadBackupFileUtil::list_directories(const common::ObString &path, 
                                                const share::ObBackupStorageInfo *storage_info,
                                                ObIArray<ObString> &part_list,
                                                ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  int64_t retry_count = 0;
  ObBackupIoAdapter adapter;
  ObFileListArrayOp op(part_list, allocator);
  while (OB_SUCC(ret)) {
    if (OB_FAIL(adapter.list_directories(path, storage_info, op))) {
      LOG_WARN("fail to list directories", K(ret), K(retry_count));
      if (ret == OB_OBJECT_STORAGE_IO_ERROR) {
        retry_count++;
        if (retry_count <= MAX_RETRY_COUNT) {
          ret = OB_SUCCESS;
          usleep(RETRY_INTERVAL);
        }
      }
    } else {
      break;
    }
  }

  return ret;
}

int ObTableLoadBackupFileUtil::get_file_length(const common::ObString &path, 
                                               const share::ObBackupStorageInfo *storage_info, 
                                               int64_t &file_length)
{
  int ret = OB_SUCCESS;
  int64_t retry_count = 0;
  ObBackupIoAdapter adapter;
  while (OB_SUCC(ret)) {
    if (OB_FAIL(adapter.get_file_length(path, storage_info, file_length))) {
      LOG_WARN("fail to list directories", K(ret), K(retry_count));
      if (ret == OB_OBJECT_STORAGE_IO_ERROR) {
        retry_count++;
        if (retry_count <= MAX_RETRY_COUNT) {
          ret = OB_SUCCESS;
          usleep(RETRY_INTERVAL);
        }
      }
    } else {
      break;
    }
  }

  return ret;
}

int ObTableLoadBackupFileUtil::read_single_file(const common::ObString &path, 
                                                const share::ObBackupStorageInfo *storage_info, 
                                                char *buf, 
                                                const int64_t buf_size,
                                                int64_t &read_size)
{
  int ret = OB_SUCCESS;
  int64_t retry_count = 0;
  ObBackupIoAdapter adapter;
  
  while (OB_SUCC(ret)) {
    if (OB_FAIL(adapter.read_single_file(path, storage_info, buf, buf_size, read_size, ObStorageIdMod(table::OB_STORAGE_ID_DDL, ObStorageUsedMod::STORAGE_USED_DDL)))) {
      LOG_WARN("fail to list directories", K(ret), K(retry_count));
      if (ret == OB_OBJECT_STORAGE_IO_ERROR) {
        retry_count++;
        if (retry_count <= MAX_RETRY_COUNT) {
          ret = OB_SUCCESS;
          usleep(RETRY_INTERVAL);
        }
      }
    } else {
      break;
    }
  }

  return ret;
}

} // namespace observer
} // namespace oceanbase
