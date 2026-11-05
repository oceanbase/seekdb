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

#include "storage/blocksstable/ob_datum_range.h"
#include "storage/direct_load/ob_direct_load_datum_row.h"
#include "storage/direct_load/ob_direct_load_dml_row_handler.h"
#include "storage/direct_load/ob_direct_load_multiple_datum_range.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable_scan_merge.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadMultipleSSTable;
class ObDirectLoadOriginTable;
class ObDirectLoadOriginTableScanner;

class ObDirectLoadLobIdConflictHandler : public ObDirectLoadDMLRowHandler
{
public:
  ObDirectLoadLobIdConflictHandler() = default;
  virtual ~ObDirectLoadLobIdConflictHandler() = default;

  /**
   * handle rows direct insert into sstable
   */
  int handle_insert_row(const ObTabletID &tablet_id,
                        const ObDirectLoadDatumRow &datum_row) override
  {
    return OB_ERR_UNEXPECTED;
  }
  int handle_delete_row(const ObTabletID &tablet_id,
                        const ObDirectLoadDatumRow &datum_row) override
  {
    return OB_ERR_UNEXPECTED;
  }
  int handle_insert_row(const ObTabletID &tablet_id,
                        const blocksstable::ObDatumRow &datum_row) override
  {
    return OB_ERR_UNEXPECTED;
  }
  int handle_insert_batch(const ObTabletID &tablet_id,
                          const blocksstable::ObBatchDatumRows &datum_rows) override
  {
    return OB_ERR_UNEXPECTED;
  }

  /**
   * handle rows with the same primary key in the imported data
   */
  int handle_update_row(const ObTabletID &tablet_id,
                        const ObDirectLoadDatumRow &datum_row) override
  {
    return OB_ERR_UNEXPECTED;
  }
  int handle_update_row(const ObTabletID &tablet_id,
                        common::ObArray<const ObDirectLoadExternalRow *> &rows,
                        const ObDirectLoadExternalRow *&row) override
  {
    return OB_ERR_UNEXPECTED;
  }
  int handle_update_row(common::ObArray<const ObDirectLoadMultipleDatumRow *> &rows,
                        const ObDirectLoadMultipleDatumRow *&row) override
  {
    return OB_ERR_UNEXPECTED;
  }

  /**
   * handle rows with the same primary key between the imported data and the original data
   */
  int handle_update_row(const ObTabletID &tablet_id,
                        const ObDirectLoadDatumRow &old_row,
                        const ObDirectLoadDatumRow &new_row,
                        const ObDirectLoadDatumRow *&result_row) override
  {
    return OB_ERR_UNEXPECTED;
  }

  /**
   * handle insert row conflict with delete row
   */
  int handle_insert_delete_conflict(const ObTabletID &tablet_id,
                                   const ObDirectLoadDatumRow &datum_row) override
  {
    return OB_ERR_UNEXPECTED;
  }

  TO_STRING_EMPTY();
};

struct ObDirectLoadLobMetaIterParam
{
public:
  ObDirectLoadLobMetaIterParam();
  ~ObDirectLoadLobMetaIterParam();

public:
  ObTabletID tablet_id_;
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  const common::ObIArray<share::schema::ObColDesc> *col_descs_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
};

class ObDirectLoadLobMetaRowIter final : public ObDirectLoadIStoreRowIterator
{
public:
  ObDirectLoadLobMetaRowIter();
  ~ObDirectLoadLobMetaRowIter();
  int init(const ObDirectLoadLobMetaIterParam &param,
           ObDirectLoadOriginTable &origin_table,
           const ObDirectLoadTableHandleArray &sstable_array,
           const blocksstable::ObDatumRange &range);
  int get_next_row(const ObDirectLoadDatumRow *&datum_row);

private:
  int init_range();
  int switch_next_lob_id();

private:
  common::ObArenaAllocator allocator_;
  ObDirectLoadLobMetaIterParam param_;
  ObDirectLoadMultipleDatumRange scan_range_;
  ObDirectLoadMultipleSSTableScanMerge scan_merge_;
  ObDirectLoadOriginTable *origin_table_;
  ObDirectLoadOriginTableScanner *origin_scanner_;
  common::ObArenaAllocator range_allocator_;
  blocksstable::ObDatumRange range_;
  ObDirectLoadLobIdConflictHandler conflict_handler_;
  const ObDirectLoadDatumRow *lob_id_row_;
  int64_t lob_id_row_cnt_; // for check if lob meta row is scanned
  ObDirectLoadDatumRow datum_row_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
