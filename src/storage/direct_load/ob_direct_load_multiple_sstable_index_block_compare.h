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

#include "storage/direct_load/ob_direct_load_multiple_datum_row.h"
#include "storage/direct_load/ob_direct_load_multiple_sstable.h"
#include "storage/direct_load/ob_direct_load_sstable_data_block_reader.h"
#include "storage/direct_load/ob_direct_load_sstable_index_block_reader.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadMultipleSSTableIndexBlockEndKeyCompare
{
  typedef ObDirectLoadMultipleDatumRow RowType;
  typedef ObDirectLoadSSTableIndexBlockReader IndexBlockReader;
  typedef ObDirectLoadSSTableDataBlockReader<RowType> DataBlockReader;
public:
  ObDirectLoadMultipleSSTableIndexBlockEndKeyCompare(
    int &ret,
    ObDirectLoadMultipleSSTable *sstable,
    const blocksstable::ObStorageDatumUtils *datum_utils,
    IndexBlockReader &index_block_reader,
    DataBlockReader &data_block_reader);
  // for upper_bound
  bool operator()(const ObDirectLoadMultipleDatumRowkey &rowkey,
                  const ObDirectLoadMultipleSSTable::IndexBlockIterator &iter);
  // for lower_bound
private:
  int &ret_;
  ObDirectLoadMultipleSSTable *sstable_;
  const blocksstable::ObStorageDatumUtils *datum_utils_;
  IndexBlockReader &index_block_reader_;
  DataBlockReader &data_block_reader_;
};

} // namespace storage
} // namespace oceanbase
