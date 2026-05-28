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

#ifndef OCEANBASE_STORAGE_DIFF_OB_TABLET_POINT_GETTER_H_
#define OCEANBASE_STORAGE_DIFF_OB_TABLET_POINT_GETTER_H_

#include "lib/allocator/page_arena.h"
#include "share/ob_ls_id.h"
#include "share/scn.h"
#include "common/ob_tablet_id.h"
#include "share/schema/ob_table_dml_param.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_single_merge.h"
#include "storage/ob_relative_table.h"
#include "storage/tx/ob_trans_define.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/blocksstable/ob_datum_rowkey.h"

namespace oceanbase
{
namespace storage
{

// Single-row point getter spanning the full table history (no fork/SCN filter).
//
// Wraps ObSingleMerge — the same code path used by SQL TABLE GET — so it sees
// every SSTable + memtable that the tablet currently exposes for reads.
//
// Tombstones surface as OB_ENTRY_NOT_EXIST (the row does not exist at the
// read snapshot). All other failures are propagated.
//
// Typical use by DIFF: after enumerating candidate PKs from the delta scanner,
// look each one up on both sides to determine current state.
class ObTabletPointGetter
{
public:
  ObTabletPointGetter();
  ~ObTabletPointGetter();

  int init(uint64_t tenant_id,
           share::ObLSID ls_id,
           common::ObTabletID tablet_id,
           uint64_t table_id,
           share::SCN read_snapshot);

  // Returns:
  //   OB_SUCCESS           — row_out points to the current row (caller must
  //                          consume before next call; storage is reused).
  //   OB_ENTRY_NOT_EXIST   — no live row for this PK at read_snapshot.
  //   other                — fatal.
  int get(const blocksstable::ObDatumRowkey &pk,
          const blocksstable::ObDatumRow *&row_out);

  // Number of stored columns (PK + value, multi-version pseudo cols excluded).
  // Matches the layout of rows returned by get().
  int64_t get_store_col_count() const { return store_col_count_; }
  int64_t get_rowkey_col_count() const { return rowkey_col_count_; }

  // Storage position of a given column_id in the row layout (storage_datums_).
  // Returns -1 if not found. Virtual generated columns are excluded.
  int64_t get_col_pos(uint64_t col_id) const;

  void reset();

private:
  bool is_inited_;
  uint64_t tenant_id_;
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
  uint64_t table_id_;

  common::ObArenaAllocator allocator_;
  common::ObArenaAllocator stmt_allocator_;
  ObTabletHandle tablet_handle_;
  share::schema::ObTableSchemaParam schema_param_;
  ObRelativeTable relative_table_;
  ObTableAccessParam access_param_;
  ObStoreCtx store_ctx_;
  ObTableAccessContext access_ctx_;
  ObGetTableParam get_table_param_;
  ObSingleMerge single_merge_;
  common::ObArray<int32_t> out_cols_project_;
  int64_t store_col_count_;
  int64_t rowkey_col_count_;

  DISALLOW_COPY_AND_ASSIGN(ObTabletPointGetter);
};

} // namespace storage
} // namespace oceanbase
#endif
