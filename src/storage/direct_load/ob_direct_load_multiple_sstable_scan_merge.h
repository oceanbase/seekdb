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

#include "lib/container/ob_loser_tree.h"
#include "storage/access/ob_simple_rows_merger.h"
#include "storage/direct_load/ob_direct_load_datum_row.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable_scan_merge_loser_tree.h"
#include "storage/direct_load/ob_direct_load_row_iterator.h"
#include "storage/direct_load/ob_direct_load_table_data_desc.h"

namespace oceanbase
{
namespace blocksstable
{
class ObStorageDatumUtils;
} // namespace blocksstable
namespace storage
{
class ObDirectLoadDMLRowHandler;

struct ObDirectLoadMultipleSSTableScanMergeParam
{
public:
  ObDirectLoadMultipleSSTableScanMergeParam();
  ~ObDirectLoadMultipleSSTableScanMergeParam();
  bool is_valid() const;
  TO_STRING_KV(K_(table_data_desc), KP_(datum_utils), KP_(dml_row_handler));
public:
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
};

class ObDirectLoadMultipleSSTableScanMerge final : public ObDirectLoadIStoreRowIterator
{
public:
  static const int64_t MAX_SSTABLE_COUNT = 1024;
  typedef ObDirectLoadMultipleSSTableScanMergeLoserTreeItem LoserTreeItem;
  typedef ObDirectLoadMultipleSSTableScanMergeLoserTreeCompare LoserTreeCompare;
  typedef ObSimpleRowsMerger<LoserTreeItem, LoserTreeCompare> ScanSimpleMerger;
  typedef common::ObLoserTree<LoserTreeItem, LoserTreeCompare>
    ScanMergeLoserTree;
public:
  ObDirectLoadMultipleSSTableScanMerge();
  ~ObDirectLoadMultipleSSTableScanMerge();
  void reset();
  int init(const ObDirectLoadMultipleSSTableScanMergeParam &param,
           const ObDirectLoadTableHandleArray &sstable_array,
           const ObDirectLoadMultipleDatumRange &range);
  int get_next_row(const ObDirectLoadMultipleDatumRow *&external_row);
  int get_next_row(const ObDirectLoadDatumRow *&datum_row) override;
private:
  int init_rows_merger(int64_t sstable_count);
  int supply_consume();
  int inner_get_next_row(const ObDirectLoadMultipleDatumRow *&external_row);
private:
  common::ObArenaAllocator allocator_;
  ObDirectLoadTableDataDesc table_data_desc_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  ObDirectLoadDMLRowHandler *dml_row_handler_;
  const ObDirectLoadMultipleDatumRange *range_;
  common::ObArray<ObDirectLoadMultipleSSTableScanner *> scanners_;
  int64_t *consumers_;
  int64_t consumer_cnt_;
  LoserTreeCompare compare_;
  ScanSimpleMerger *simple_merge_;
  ScanMergeLoserTree *loser_tree_;
  common::ObRowsMerger<LoserTreeItem, LoserTreeCompare> *rows_merger_;
  ObDirectLoadDatumRow datum_row_;
  common::ObArray<const ObDirectLoadMultipleDatumRow *> rows_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
