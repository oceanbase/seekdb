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

#include "storage/direct_load/ob_direct_load_rowkey_iterator.h"
#include "storage/direct_load/ob_direct_load_sstable_data_block_reader.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadMultipleSSTable;
class ObDirectLoadTableDataDesc;

class ObDirectLoadMultipleSSTableRowkeyScanner
  : public ObIDirectLoadMultipleDatumRowkeyIterator
{
  typedef ObDirectLoadMultipleDatumRowkey RowkeyType;
  typedef ObDirectLoadSSTableDataBlockReader<RowkeyType> DataBlockReader;
public:
  ObDirectLoadMultipleSSTableRowkeyScanner();
  virtual ~ObDirectLoadMultipleSSTableRowkeyScanner();
  int init(ObDirectLoadMultipleSSTable *sstable, const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_rowkey(const ObDirectLoadMultipleDatumRowkey *&rowkey) override;
private:
  int switch_next_fragment();
private:
  ObDirectLoadMultipleSSTable *sstable_;
  DataBlockReader data_block_reader_;
  int64_t fragment_idx_;
  bool is_inited_;
};

class ObDirectLoadSSTableRowkeyScanner : public ObIDirectLoadDatumRowkeyIterator
{
public:
  ObDirectLoadSSTableRowkeyScanner();
  virtual ~ObDirectLoadSSTableRowkeyScanner();
  int init(ObDirectLoadMultipleSSTable *sstable, const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_rowkey(const blocksstable::ObDatumRowkey *&rowkey) override;
private:
  ObDirectLoadMultipleSSTable *sstable_;
  ObDirectLoadMultipleSSTableRowkeyScanner scanner_;
  blocksstable::ObDatumRowkey rowkey_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
