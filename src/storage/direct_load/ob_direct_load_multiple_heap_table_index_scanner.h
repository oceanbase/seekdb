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

#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block_reader.h"

namespace oceanbase
{
namespace storage
{
class ObDirectLoadTableDataDesc;
class ObDirectLoadMultipleHeapTable;

class ObIDirectLoadMultipleHeapTableIndexScanner
{
public:
  virtual ~ObIDirectLoadMultipleHeapTableIndexScanner() = default;
  virtual int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&tablet_index) = 0;
  TO_STRING_EMPTY();
};

class ObDirectLoadMultipleHeapTableIndexWholeScanner
  : public ObIDirectLoadMultipleHeapTableIndexScanner
{
public:
  ObDirectLoadMultipleHeapTableIndexWholeScanner();
  virtual ~ObDirectLoadMultipleHeapTableIndexWholeScanner();
  int init(const ObDirectLoadTmpFileHandle &index_file_handle,
           int64_t file_size,
           const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&tablet_index) override;
private:
  ObDirectLoadMultipleHeapTableIndexBlockReader index_block_reader_;
  bool is_inited_;
};

class ObDirectLoadMultipleHeapTableTabletIndexWholeScanner
  : public ObIDirectLoadMultipleHeapTableIndexScanner
{
public:
  ObDirectLoadMultipleHeapTableTabletIndexWholeScanner();
  virtual ~ObDirectLoadMultipleHeapTableTabletIndexWholeScanner();
  int init(ObDirectLoadMultipleHeapTable *heap_table,
           const common::ObTabletID &tablet_id,
           const ObDirectLoadTableDataDesc &table_data_desc);
  int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&tablet_index) override;
  TO_STRING_KV(KP_(heap_table), K_(tablet_id));
private:
  int locate_left_border();
private:
  ObDirectLoadMultipleHeapTable *heap_table_;
  common::ObTabletID tablet_id_;
  ObDirectLoadMultipleHeapTableIndexBlockReader index_block_reader_;
  bool is_iter_end_;
  bool is_inited_;
};

} // namespace storage
} // namespace oceanbase
