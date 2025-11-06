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
#include "share/backup/ob_backup_struct.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace observer
{

class ObTableLoadBackupFileUtil
{
public:
  static const int64_t MAX_RETRY_COUNT = 12; // retry 1 minute
  static const int64_t RETRY_INTERVAL = 5 * 1000 * 1000; // 5s
  ObTableLoadBackupFileUtil() {}
  ~ObTableLoadBackupFileUtil() {}
  static int list_directories(const common::ObString &path, 
                              const share::ObBackupStorageInfo *storage_info,
                              ObIArray<ObString> &part_list_,
                              ObIAllocator &allocator);
  static int get_file_length(const common::ObString &path, 
                             const share::ObBackupStorageInfo *storage_info, 
                             int64_t &file_length);
  static int read_single_file(const common::ObString &path, 
                              const share::ObBackupStorageInfo *storage_info, 
                              char *buf, 
                              const int64_t buf_size,
                              int64_t &read_size);
};

} // namespace observer
} // namespace oceanbase
