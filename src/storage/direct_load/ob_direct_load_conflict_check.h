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

#include "storage/direct_load/ob_direct_load_multiple_datum_range.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable_scan_merge.h"
#include "storage/direct_load/ob_direct_load_row_iterator.h"
#include "storage/direct_load/ob_direct_load_sstable_scan_merge.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadDMLRowHandler;
class ObDirectLoadOriginTable;
class ObDirectLoadOriginTableScanner;

struct ObDirectLoadConflictCheckParam
{
public:
  ObDirectLoadConflictCheckParam();
  ~ObDirectLoadConflictCheckParam();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id),
               K_(table_data_desc),
               KP_(origin_table),
               KPC_(range), 
               KP_(col_descs),
               KP_(datum_utils),
               KP_(dml_row_handler));
public:
  common::ObTabletID tablet_id_;
  ObDirectLoadTableDataDesc table_data_desc_;
  ObDirectLoadOriginTable *origin_table_;
  const blocksstable::ObDatumRange *range_;
  const common::ObIArray<share::schema::ObColDesc> *col_descs_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
};

class ObDirectLoadConflictCheck
{
public:
  static const int64_t SKIP_THESHOLD = 100;
  ObDirectLoadConflictCheck();
  virtual ~ObDirectLoadConflictCheck();
  int init(
      const ObDirectLoadConflictCheckParam &param, 
      ObDirectLoadIStoreRowIterator *load_iter);
  int get_next_row(const ObDirectLoadDatumRow *&datum_row);
private:
  int handle_get_next_row_finish(
      const ObDirectLoadDatumRow *load_row,
      const ObDirectLoadDatumRow *&datum_row);
  int compare(
      const ObDirectLoadDatumRow &first_row,
      const ObDirectLoadDatumRow &second_row, 
      int &cmp_ret);
  int reopen_origin_scanner(const ObDirectLoadDatumRow *datum_row);
private:
  common::ObArenaAllocator allocator_;
  common::ObArenaAllocator range_allocator_;
  ObDirectLoadConflictCheckParam param_;
  ObDirectLoadIStoreRowIterator *load_iter_;
  ObDirectLoadOriginTableScanner *origin_scanner_;
  const ObDirectLoadDatumRow *origin_row_;
  ObDatumRange new_range_;
  bool origin_iter_is_end_;
  bool is_inited_;
};

class ObDirectLoadSSTableConflictCheck final : public ObDirectLoadIStoreRowIterator
{
public:
  ObDirectLoadSSTableConflictCheck();
  ~ObDirectLoadSSTableConflictCheck();
  int get_next_row(const ObDirectLoadDatumRow *&datum_row) override;
private:
  ObDirectLoadSSTableScanMerge scan_merge_;
  ObDirectLoadConflictCheck conflict_check_;
  bool is_inited_;
};

class ObDirectLoadMultipleSSTableConflictCheck final : public ObDirectLoadIStoreRowIterator
{
public:
  ObDirectLoadMultipleSSTableConflictCheck();
  virtual ~ObDirectLoadMultipleSSTableConflictCheck();
  int init(
      const ObDirectLoadConflictCheckParam &param, 
      const ObDirectLoadTableHandleArray &sstable_array);
  int get_next_row(const ObDirectLoadDatumRow *&datum_row) override;
private:
  ObDirectLoadMultipleDatumRange range_;
  ObDirectLoadMultipleSSTableScanMerge scan_merge_;
  ObDirectLoadConflictCheck conflict_check_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
