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

#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_STORAGE_UTIL_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_STORAGE_UTIL_H_
#include "share/ob_ddl_common.h"  // reuses its types(ObMacroDataSeq/ObDatumRow/ObBatchDatumRows/ObStorageSchema/ObWriteMacroParam/ObTableSchema etc.) for forward declarations
namespace oceanbase
{
namespace blocksstable { class ObDatumRowkey; }
namespace storage
{
class ObDDLBatchRows;
class ObDDLMacroBlockWriter;
class ObLobMacroBlockWriter;

class ObDDLStorageUtil
{
public:
  static int ddl_get_tablet(
      ObLS *ls,
      const ObTabletID &tablet_id,
      ObTabletHandle &tablet_handle,
      const ObMDSGetTabletMode mode = ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  static int get_tablet_physical_row_cnt(
      const ObTabletID &tablet_id,
      const bool calc_sstable,
      const bool calc_memtable,
      int64_t &physical_row_count);
  static int is_major_exist(const ObTabletID &tablet_id, bool &is_exist);
  static int set_tablet_autoinc_seq(const ObTabletID &tablet_id, const int64_t seq_value);
  static int report_ddl_checksum_from_major_sstable(
      const ObTabletID &tablet_id,
      const uint64_t table_id,
      const int64_t execution_id,
      const int64_t ddl_task_id,
      const int64_t data_format_version);
  static int report_ddl_sstable_checksum(
      const ObTabletID &tablet_id,
      const uint64_t target_table_id,
      const int64_t execution_id,
      const int64_t ddl_task_id,
      const int64_t data_format_version,
      ObTabletHandle &tablet_handle,
      blocksstable::ObSSTable *first_major_sstable);
  static int init_macro_block_writer(
      const ObWriteMacroParam &param,
      ObIAllocator &allocator,
      ObDDLMacroBlockWriter *&macro_block_writer);
  static int prepare_lob_writer(
      const ObTabletID &tablet_id,
      const int64_t slice_idx,
      const ObWriteMacroParam &param,
      ObLobMacroBlockWriter *&lob_writer);
  static int handle_lob_columns(
      const ObTabletID &tablet_id,
      const int64_t slice_idx,
      ObWriteMacroParam &param,
      ObLobMacroBlockWriter *&lob_writer,
      ObArenaAllocator &allocator,
      blocksstable::ObDatumRow &datum_row);
  static int handle_lob_columns(
      const ObTabletID &tablet_id,
      const int64_t slice_idx,
      ObWriteMacroParam &param,
      ObLobMacroBlockWriter *&lob_writer,
      ObArenaAllocator &allocator,
      blocksstable::ObBatchDatumRows &batch_rows);
  static int convert_to_storage_row(
      const ObTabletID &tablet_id,
      const int64_t slice_idx,
      const ObWriteMacroParam &param,
      ObLobMacroBlockWriter *&lob_writer,
      ObArenaAllocator &row_arena,
      blocksstable::ObDatumRow &current_row);
  static int fill_writer_param(
      const ObTabletID &tablet_id,
      const int64_t slice_idx,
      ObDDLIndependentDag *dag,
      const int64_t max_batch_size,
      ObWriteMacroParam &param);
  static int get_task_ranges(
      const int64_t task_id,
      const ObTabletID &tablet_id,
      const int64_t tablet_size,
      const int64_t hint_parallelism,
      ObArenaAllocator &allocator,
      ObArray<blocksstable::ObDatumRange> &ranges);
  static int init_batch_rows(
      const ObDDLTableSchema &ddl_table_schema,
      const int64_t batch_size,
      ObDDLBatchRows &batch_rows);
  static int check_null_and_length(
      const bool is_index_table,
      const bool has_lob_rowkey,
      const int64_t rowkey_column_cnt,
      const blocksstable::ObDatumRow &row_val);
  static int check_null_and_length(
      const bool is_index_table,
      const bool has_lob_rowkey,
      const int64_t rowkey_column_num,
      blocksstable::ObBatchDatumRows &batch_rows);
  static int init_datum_row_with_snapshot(
      const int64_t request_column_count,
      const int64_t rowkey_column_count,
      const int64_t snapshot_version,
      blocksstable::ObDatumRow &datum_row);
  static int init_macro_block_seq(const int64_t parallel_idx, blocksstable::ObMacroDataSeq &start_seq);
  static int64_t get_parallel_idx(const blocksstable::ObMacroDataSeq &start_seq);
  static int handle_lob_column(
      const ObTabletID &tablet_id,
      const int64_t slice_idx,
      ObWriteMacroParam &param,
      const bool output_invalid_lob_cells, // output all lob cells, include null and nop
      ObIArray<std::pair<char **, uint32_t *>> &lob_cells,
      ObArenaAllocator &allocator,
      const ObColumnSchemaItem &column_schema_item,
      share::ObBatchSelector &selector,
      ObIVector *&vector);
  static int convert_to_storage_schema(
      const share::schema::ObTableSchema *table_schema,
      ObIAllocator &allocator,
      ObStorageSchema *&storage_schema);
  static int extract_index_key(
      const share::schema::ObTableSchema &index_schema,
      const blocksstable::ObDatumRowkey &index_key,
      char *buffer,
      const int64_t buffer_len);
};
}  // namespace storage
}  // namespace oceanbase
#endif
