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

#include "storage/blocksstable/ob_datum_row.h"
#include "storage/direct_load/ob_direct_load_insert_table_ctx.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadInsertLobTabletContext;

class ObDirectLoadLobBuilder
{
public:
  ObDirectLoadLobBuilder();
  ~ObDirectLoadLobBuilder();
  int init(ObDirectLoadInsertTabletContext *insert_tablet_ctx,
           common::ObIAllocator *lob_allocator = nullptr);
  // Include the full row with multiple version columns
  int append_lob(blocksstable::ObDatumRow &datum_row);
  int append_lob(blocksstable::ObBatchDatumRows &datum_rows);
  // Intermediate process data
  int append_lob(ObDirectLoadDatumRow &datum_row,
                 const ObDirectLoadRowFlag &row_flag);
  int append_lob(const ObDirectLoadBatchRows &batch_rows,
                 const uint16_t *selector,
                 const int64_t size);
  int close();

private:
  int init_sstable_slice_ctx();
  int switch_sstable_slice();

  inline int check_can_skip(char *ptr, uint32_t len, bool &can_skip);
  int check_can_skip(const blocksstable::ObDatumRow &datum_row, bool &can_skip);
  int check_can_skip(const blocksstable::ObBatchDatumRows &datum_rows, bool &can_skip);
  int check_can_skip(const ObDirectLoadBatchRows &batch_rows, const uint16_t *selector,
                     const int64_t size, bool &can_skip);

  int append_row(blocksstable::ObDatumRow &datum_row);
  int append_batch(blocksstable::ObBatchDatumRows &datum_rows);

  int fill_into_datum_row(ObDirectLoadDatumRow &datum_row,
                          const ObDirectLoadRowFlag &row_flag);
  int fetch_from_datum_row(ObDirectLoadDatumRow &datum_row,
                           const ObDirectLoadRowFlag &row_flag);

  int fill_into_datum_row(const ObDirectLoadBatchRows &batch_rows,
                          const int64_t row_idx);
  int fetch_from_datum_row(const ObDirectLoadBatchRows &batch_rows,
                           const int64_t row_idx);
private:
  ObDirectLoadInsertTabletContext *insert_tablet_ctx_;
  ObDirectLoadInsertLobTabletContext *insert_lob_tablet_ctx_;
  common::ObIAllocator *lob_allocator_;
  common::ObArenaAllocator inner_lob_allocator_;
  // Does not include multi-version column, lob may be the primary key column
  const ObIArray<int64_t> *lob_column_idxs_;
  int64_t lob_column_cnt_;
  int64_t extra_rowkey_cnt_;
  int64_t lob_inrow_threshold_;
  ObDirectLoadInsertTabletWriteCtx write_ctx_;
  int64_t current_lob_slice_id_;
  blocksstable::ObDatumRow datum_row_;
  ObDirectLoadMgrAgent tmp_ddl_agent_;
  bool is_closed_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadLobBuilder);
};

} // namespace storage
} // namespace oceanbase
