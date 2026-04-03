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

#ifndef OCEANBASE_SHARE_STORAGE_OB_ZONE_MERGE_INFO_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_ZONE_MERGE_INFO_TABLE_STORAGE_H_

#include "share/storage/ob_sqlite_connection_pool.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_zone_merge_info.h"

namespace oceanbase
{
namespace share
{

class ObZoneMergeInfoTableStorage
{
public:
  ObZoneMergeInfoTableStorage();
  virtual ~ObZoneMergeInfoTableStorage();

  int init(ObSQLiteConnectionPool *pool);
  bool is_inited() const { return nullptr != pool_; }

  // Insert or update zone merge info
  int insert_or_update(const ObZoneMergeInfo &zone_merge_info);

  // Get zone merge info
  int get(const uint64_t tenant_id, ObZoneMergeInfo &zone_merge_info);

  // Get all zone merge infos for a tenant
  int get_all(const uint64_t tenant_id, ObIArray<ObZoneMergeInfo> &zone_merge_infos);

  // Remove zone merge info by tenant_id
  int remove(const uint64_t tenant_id);

private:
  int create_table_if_not_exists();

  ObSQLiteConnectionPool *pool_;
  DISALLOW_COPY_AND_ASSIGN(ObZoneMergeInfoTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_ZONE_MERGE_INFO_TABLE_STORAGE_H_
