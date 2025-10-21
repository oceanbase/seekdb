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
#include "observer/table_load/backup/ob_table_load_backup_table.h"

namespace oceanbase
{
namespace observer
{

class ObTableLoadBackupTable_V_1_4 : public ObTableLoadBackupTable
{
public:
  ObTableLoadBackupTable_V_1_4() 
    : allocator_("TLD_BT_V_1_4"),
      is_inited_(false)
  {
    allocator_.set_tenant_id(MTL_ID());
    column_ids_.set_tenant_id(MTL_ID());
    part_list_.set_tenant_id(MTL_ID());
  }
  virtual ~ObTableLoadBackupTable_V_1_4() {}
  int init(const share::ObBackupStorageInfo *storage_info, const ObString &path) override;
  int scan(int64_t part_idx, ObNewRowIterator *&iter, ObIAllocator &allocator,
           int64_t subpart_count = 1, int64_t subpart_idx = 0) override;
  bool is_valid() const override;
  int64_t get_column_count() const override { return column_ids_.count(); }
  int64_t get_partition_count() const override { return part_list_.count(); }
  TO_STRING_KV(K(table_id_), K(data_path_), K(meta_path_), K(column_ids_), K(part_list_));
private:
  int parse_path(const ObString &path);
  int get_column_ids();
  int get_partitions();
private:
  ObArenaAllocator allocator_;
  share::ObBackupStorageInfo storage_info_;
  ObString table_id_;
  ObString data_path_;
  ObString meta_path_;
  ObArray<int64_t> column_ids_;
  ObArray<ObString> part_list_;
  bool is_inited_;
};

} // namespace observer
} // namespace oceanbase
