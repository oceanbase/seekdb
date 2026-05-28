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

#ifndef OCEANBASE_STORAGE_DIFF_OB_DIFF_TABLET_SCANNER_H_
#define OCEANBASE_STORAGE_DIFF_OB_DIFF_TABLET_SCANNER_H_

#include "lib/allocator/page_arena.h"
#include "lib/container/ob_se_array.h"
#include "lib/hash/ob_hashmap.h"
#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_store_row_iterator.h"
#include "storage/blocksstable/ob_datum_rowkey.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/blocksstable/ob_datum_range.h"

namespace oceanbase
{
namespace blocksstable { class ObSSTable; }
namespace storage
{
class ObTablet;
class ObTabletHandle;
class ObStorageSchema;
class ObRowkeyReadInfo;
class ObSSTableRowWholeScanner;
class ObITabletMemtable;

// Output unit of ObDiffTabletScanner. Owns its memory in the iter allocator.
struct ObDiffMaterializedRow
{
  blocksstable::ObDatumRow row_;   // contains pk + val columns (deep copy)
  bool is_delete_;
  int64_t trans_version_;
  TO_STRING_KV(K_(is_delete), K_(trans_version), K_(row));
};

// Scans a single tablet and emits distinct rows (latest version per PK) in
// PK-ascending order.
//
//   fork_snapshot_version = 0  → full scan (fallback): include every SSTable
//                                and every memtable.
//   fork_snapshot_version > 0  → delta scan (incremental): include only
//                                SSTables with start_scn > fork_snapshot AND
//                                memtables with start_scn > fork_snapshot.
//
// Tombstones surface as is_delete=true rows so callers can distinguish
// "row was deleted post-fork" from "row never existed". For each PK, the
// row with the largest trans_version wins (current state at the read SCN).
//
// All emitted rows are deep-copied into the caller-supplied allocator and
// remain valid until that allocator resets.
class ObDiffTabletScanner
{
public:
  ObDiffTabletScanner();
  ~ObDiffTabletScanner();

  int init(uint64_t tenant_id,
           share::ObLSID ls_id,
           common::ObTabletID tablet_id,
           uint64_t table_id,
           int64_t fork_snapshot_version,
           int64_t read_snapshot_us,
           common::ObIAllocator &alloc);

  // Output is sorted by PK ascending. Returns OB_ITER_END at end.
  int get_next_row(const ObDiffMaterializedRow *&row);

  // Storage rowkey column count (matches storage_datums_[0..rowkey_cnt) layout).
  int64_t get_rowkey_cnt() const { return rowkey_cnt_; }
  // Datum compare utils for PK columns — owned by rowkey_read_info_.
  const blocksstable::ObStorageDatumUtils *get_datum_utils() const;

  void reset();

private:
  int collect_all_();
  int collect_from_sstable_(blocksstable::ObSSTable &sstable);
  int collect_from_memtable_(ObITabletMemtable &memtable);
  int absorb_row_(const blocksstable::ObDatumRow &row);
  int finalize_sort_();

private:
  bool is_inited_;
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  uint64_t table_id_;
  int64_t fork_snapshot_version_;
  int64_t read_snapshot_us_;
  common::ObIAllocator *alloc_;

  common::ObArenaAllocator local_alloc_;
  ObStoreCtx store_ctx_;
  ObTableAccessContext access_ctx_;
  ObTableAccessParam access_param_;
  ObRowkeyReadInfo *rowkey_read_info_;
  ObStorageSchema *storage_schema_;

  // PK column count and projected output column count, captured from schema.
  int64_t rowkey_cnt_;
  int64_t output_col_cnt_;
  int64_t trans_idx_;   // index of the trans_version pseudo-column in multi-version row

  // Materialised rows, deduped by PK (latest trans_version wins).
  common::ObArray<ObDiffMaterializedRow *> rows_;

  // Iteration cursor over sorted rows_.
  int64_t cur_idx_;

  DISALLOW_COPY_AND_ASSIGN(ObDiffTabletScanner);
};

} // namespace storage
} // namespace oceanbase
#endif
