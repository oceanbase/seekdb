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

#ifndef OB_RELATIVE_TABLE_H_
#define OB_RELATIVE_TABLE_H_

#include <stdint.h>

#include "lib/container/ob_iarray.h"
#include "storage/meta_mem/ob_tablet_handle.h"
#include "storage/tablet/ob_table_store_util.h"

namespace oceanbase
{
namespace common
{
class ObNewRow;
class ObString;
}
namespace share
{
namespace schema
{
class ObTableDMLParam;
class ObTableSchema;
class ObTableSchemaParam;
struct ObColDesc;
class ColumnMap;
}
}
namespace storage
{
class ObTruncatePartitionFilter;
class ObRelativeTable final
{
public:
  ObTabletTableIterator tablet_iter_;

  ObRelativeTable(): tablet_iter_(), allow_not_ready_(false), schema_param_(NULL), truncate_part_filter_(NULL),
      tablet_id_(), is_inited_(false)
  {}
  ~ObRelativeTable();

  bool is_valid() const;

  void destroy();
  int init(
      const share::schema::ObTableSchemaParam *param,
      const ObTabletID &tablet_id,
      const bool allow_not_ready = false);
  uint64_t get_table_id() const;
  const ObTabletID& get_tablet_id() const;
  const ObTabletHandle *get_tablet_handle() const;
  int get_col_desc(const uint64_t column_id, share::schema::ObColDesc &col_desc) const;
  int get_rowkey_col_id_by_idx(const int64_t idx, uint64_t &col_id) const;
  int get_rowkey_column_ids(common::ObIArray<share::schema::ObColDesc> &column_ids) const;
  int get_rowkey_column_ids(common::ObIArray<uint64_t> &column_ids) const;
  int get_column_data_length(const uint64_t column_id, int32_t &len) const;
  int is_rowkey_column_id(const uint64_t column_id, bool &is_rowkey) const;
  int is_column_nullable_for_write(const uint64_t column_id, bool &is_nullable_for_write) const;
  int is_column_nullable_for_read(const uint64_t column_id, bool &is_nullable_for_read) const;
  int is_nop_default_value(const uint64_t column_id, bool &is_nop) const;
  int has_udf_column(bool &has_udf) const;
  int is_hidden_column(const uint64_t column_id, bool &is_hidden) const;
  int is_gen_column(const uint64_t column_id, bool &is_gen_col) const;
  OB_INLINE bool allow_not_ready() const { return allow_not_ready_; }
  int64_t get_rowkey_column_num() const;
  int64_t get_shadow_rowkey_column_num() const;
  int64_t get_column_count() const;
  int get_index_name(common::ObString &index_name) const;
  int get_primary_key_name(common::ObString &pk_name) const;
  bool is_index_table() const;
  bool is_storage_index_table() const;
  bool is_index_local_storage() const;
  bool can_read_index() const;
  bool is_unique_index() const;
  bool is_fts_index() const;
  bool is_vector_index() const;
  int prepare_truncate_part_filter(common::ObIAllocator &allocator, const int64_t read_snapshot);
  ObTruncatePartitionFilter *get_truncate_part_filter() const { return truncate_part_filter_; }
  const share::schema::ObTableSchemaParam *get_schema_param() const { return schema_param_;}

  DECLARE_TO_STRING;

private:
  int get_rowkey_col_desc_by_idx(const int64_t idx, share::schema::ObColDesc &col_desc) const;
  // must follow index column order
  int set_index_value(const common::ObNewRow &table_row,
                      const share::schema::ColumnMap &col_map,
                      const share::schema::ObColDesc &col_desc,
                      const int64_t rowkey_size,
                      common::ObNewRow &index_row,
                      common::ObIArray<share::schema::ObColDesc> *idx_columns);

private:
  bool allow_not_ready_;
  const share::schema::ObTableSchemaParam *schema_param_;
  ObTruncatePartitionFilter *truncate_part_filter_;
  ObTabletID tablet_id_;
  bool is_inited_;

  DISALLOW_COPY_AND_ASSIGN(ObRelativeTable);
};

}//namespace oceanbase
}//namespace storage

#endif /* OB_RELATIVE_TABLE_H_ */
