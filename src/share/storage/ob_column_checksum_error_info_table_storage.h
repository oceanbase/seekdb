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

#ifndef OCEANBASE_SHARE_STORAGE_OB_COLUMN_CHECKSUM_ERROR_INFO_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_COLUMN_CHECKSUM_ERROR_INFO_TABLE_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_column_checksum_error_operator.h"

namespace oceanbase
{
namespace share
{

class ObColumnChecksumErrorInfo;
class ObColumnChecksumErrorInfoTableStorage
{
public:
  ObColumnChecksumErrorInfoTableStorage();
  virtual ~ObColumnChecksumErrorInfoTableStorage();

  int init(ObSQLiteConnectionPool *pool);
  bool is_inited() const { return nullptr != pool_; }

  // Insert column checksum error info
  int insert(const ObColumnChecksumErrorInfo &error_info);

  // Batch insert column checksum error infos
  int insert_all(const ObIArray<ObColumnChecksumErrorInfo> &error_infos);

  // Get column checksum error infos
  int get(const uint64_t tenant_id,
          const SCN &frozen_scn,
          const bool is_global_index,
          const int64_t data_table_id,
          const int64_t index_table_id,
          const common::ObTabletID &data_tablet_id,
          const common::ObTabletID &index_tablet_id,
          ObIArray<ObColumnChecksumErrorInfo> &error_infos);

  // Delete expired error infos
  int delete_expired(const uint64_t tenant_id, const SCN &frozen_scn_before, int64_t limit);

private:
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObColumnChecksumErrorInfoTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_COLUMN_CHECKSUM_ERROR_INFO_TABLE_STORAGE_H_
