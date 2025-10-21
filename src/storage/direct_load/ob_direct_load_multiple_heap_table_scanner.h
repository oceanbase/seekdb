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

#include "storage/direct_load/ob_direct_load_i_table.h"
#include "storage/direct_load/ob_direct_load_data_block.h"
#include "storage/direct_load/ob_direct_load_data_block_reader.h"
#include "storage/direct_load/ob_direct_load_multiple_external_row.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_scanner.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTableDataDesc;
class ObDirectLoadMultipleHeapTable;

class ObDirectLoadMultipleHeapTableTabletWholeScanner
{
  typedef ObDirectLoadMultipleExternalRow RowType;
  typedef ObDirectLoadDataBlockReader<ObDirectLoadDataBlock::Header, RowType> DataBlockReader;
public:
  ObDirectLoadMultipleHeapTableTabletWholeScanner();
  ~ObDirectLoadMultipleHeapTableTabletWholeScanner();
  int init(const ObDirectLoadTableHandle &heap_table, const common::ObTabletID &tablet_id,
           const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_row(const RowType *&external_row);
  TO_STRING_KV(K_(heap_table), K_(tablet_id));
private:
  int switch_next_fragment();
private:
  ObDirectLoadTableHandle heap_table_;
  common::ObTabletID tablet_id_;
  ObDirectLoadMultipleHeapTableTabletIndexWholeScanner index_scanner_;
  DataBlockReader data_block_reader_;
  bool is_iter_end_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
