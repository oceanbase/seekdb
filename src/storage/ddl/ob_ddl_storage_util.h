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

// storage DDL helper class: contains  storage-clean static methods(A-set member-split cleanup)
#ifndef OCEANBASE_STORAGE_DDL_OB_DDL_STORAGE_UTIL_H_
#define OCEANBASE_STORAGE_DDL_OB_DDL_STORAGE_UTIL_H_
#include "share/ob_ddl_common.h"  // reuses its types(ObMacroDataSeq/ObDatumRow/ObBatchDatumRows/ObStorageSchema/ObWriteMacroParam/ObTableSchema etc.) for forward declarations
namespace oceanbase
{
namespace blocksstable { class ObDatumRowkey; }
namespace storage
{
class ObDDLStorageUtil
{
public:
  static constexpr int64_t MACRO_SEQ_STEP = 1LL << 25;

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
  // --- ObDDLErrorMessageTableOperator::extract_index_key demoted and merged in(storage-bound: ObDatumRowkey/ObStorageDatum) ---
  static int extract_index_key(
      const share::schema::ObTableSchema &index_schema,
      const blocksstable::ObDatumRowkey &index_key,
      char *buffer,
      const int64_t buffer_len);
};
}  // namespace storage
}  // namespace oceanbase
#endif
