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
  // --- ObCODDLUtil column-store DDL helpers merged in ---
  static int get_column_checksums(
      const storage::ObCOSSTableV2 *co_sstable,
      const storage::ObStorageSchema *storage_schema,
      ObIArray<int64_t> &column_checksums);
  static int is_rowkey_based_co_sstable(
      const storage::ObCOSSTableV2 *co_sstable,
      const storage::ObStorageSchema *storage_schema,
      bool &is_rowkey_based);
  static int get_co_column_checksums_if_need(
      const ObTabletHandle &tablet_handle,
      const blocksstable::ObSSTable *sstable,
      ObIArray<int64_t> &column_checksum_array);
  static int get_base_cg_idx(
      const storage::ObStorageSchema *storage_schema,
      int64_t &base_cg_idx);
  static int need_column_group_store(const storage::ObStorageSchema &table_schema, bool &need_column_group);
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
