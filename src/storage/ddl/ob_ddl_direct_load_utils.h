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

#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_DIRECT_LOAD_UTILS_H
#define OCEANBASE_STORAGE_DDL_OB_DDL_DIRECT_LOAD_UTILS_H

#include "common/ob_tablet_id.h"
#include "storage/ddl/ob_direct_load_struct.h"
namespace oceanbase
{
namespace storage
{
class ObInsertMonitor;
class ObDDLDirectLoadUtil
{
public:
static bool need_process_vec_index(const ObIndexType & index_type)
{
  return schema::is_local_vec_ivf_centroid_index(index_type)
      || schema::is_vec_ivfsq8_meta_index(index_type)
      || schema::is_vec_ivfpq_pq_centroid_index(index_type)
      || schema::is_vec_index_snapshot_data_type(index_type);
  }
  static int get_tablet_handle(const ObTabletID &tablet_id, ObTabletHandle &tablet_handle);
  static ObDirectLoadType ddl_get_direct_load_type();
  static int generate_merge_param(const ObTabletDDLCompleteArg &arg, ObDDLTableMergeDagParam &merge_param);
  static int generate_merge_param(const ObTabletDDLCompleteMdsUserData &data, ObTablet &tablet, ObDDLTableMergeDagParam &merge_param);
  static int is_ddl_need_major_merge(const ObTablet &tablet, bool &ddl_need_merging);
  static int prepare_schema_item_for_vec_idx_data(ObSchemaGetterGuard &schema_guard,
                                                  const ObTableSchema *table_schema,
                                                  const ObTableSchema *&data_table_schema,
                                                  ObIAllocator &allocator,
                                                  ObTableSchemaItem &schema_item);
};
} // namespace storage
} // namespace oceanbaes

#endif // OCEANBASE_STORAGE_DDL_OB_DDL_DIRECT_LOAD_UTILS_H
