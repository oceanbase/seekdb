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

#include "storage/blocksstable/ob_sstable.h"
#include "share/table/ob_table_load_define.h"
#include "sql/resolver/cmd/ob_load_data_stmt.h"
#include "storage/direct_load/ob_direct_load_dml_row_handler.h"
#include "storage/direct_load/ob_direct_load_multiple_datum_range.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable_scan_merge.h"
#include "storage/direct_load/ob_direct_load_row_iterator.h"
#include "storage/direct_load/ob_direct_load_sstable_scan_merge.h"
#include "storage/direct_load/ob_direct_load_struct.h"

namespace oceanbase
{
namespace storage
{
struct ObDirectLoadDataInsertParam
{
public:
  ObDirectLoadDataInsertParam();
  ~ObDirectLoadDataInsertParam();
  bool is_valid() const;
  TO_STRING_KV(K_(tablet_id), K_(table_data_desc), KP_(datum_utils), KP_(dml_row_handler));
public:
  common::ObTabletID tablet_id_;
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
};

class ObDirectLoadDataInsert
{
public:
  ObDirectLoadDataInsert();
  ~ObDirectLoadDataInsert();
  int init(const ObDirectLoadDataInsertParam &param,
           ObDirectLoadIStoreRowIterator *load_iter);
  int get_next_row(const ObDirectLoadDatumRow *&datum_row);
private:
  ObDirectLoadDataInsertParam param_;
  ObDirectLoadIStoreRowIterator *load_iter_;
  bool is_inited_;
};

class ObDirectLoadSSTableDataInsert final : public ObDirectLoadIStoreRowIterator
{
public:
  ObDirectLoadSSTableDataInsert();
  ~ObDirectLoadSSTableDataInsert();
  int get_next_row(const ObDirectLoadDatumRow *&datum_row) override;
private:
  ObDirectLoadSSTableScanMerge scan_merge_;
  ObDirectLoadDataInsert data_insert_;
  bool is_inited_;
};

class ObDirectLoadMultipleSSTableDataInsert final : public ObDirectLoadIStoreRowIterator
{
public:
  ObDirectLoadMultipleSSTableDataInsert();
  ~ObDirectLoadMultipleSSTableDataInsert();
  int init(const ObDirectLoadDataInsertParam &param,
           const ObDirectLoadTableHandleArray &sstable_array,
           const blocksstable::ObDatumRange &range);
  int get_next_row(const ObDirectLoadDatumRow *&datum_row) override;
private:
  ObDirectLoadMultipleDatumRange range_;
  ObDirectLoadMultipleSSTableScanMerge scan_merge_;
  ObDirectLoadDataInsert data_insert_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
