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
#include "common/row/ob_row_iterator.h"
#include "share/backup/ob_backup_struct.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace observer
{

enum class ObTableLoadBackupVersion
{
  INVALID = 0,
  V_1_4,
  V_3_X,      // FARM COMPAT WHITELIST, rename, add whitelist
  V_2_X_LOG,
  V_2_X_PHY,
  MAX_VERSION
};

class ObTableLoadBackupTable
{
public:
  static int get_table(ObTableLoadBackupVersion version, 
                       ObTableLoadBackupTable *&table, 
                       ObIAllocator &allocator);
  ObTableLoadBackupTable() {}
  virtual ~ObTableLoadBackupTable() {}
  virtual int init(const share::ObBackupStorageInfo *storage_info, const ObString &path) = 0;
  virtual int scan(int64_t part_idx, common::ObNewRowIterator *&iter, common::ObIAllocator &allocator,
                   int64_t subpart_count = 1, int64_t subpart_idx = 0) = 0;
  virtual bool is_valid() const = 0;
  virtual int64_t get_column_count() const = 0;
  virtual int64_t get_partition_count() const = 0;
};

} // namespace observer
} // namespace oceanbase
