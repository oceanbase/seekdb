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

#include "storage/direct_load/ob_direct_load_data_block_reader.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadMultipleHeapTableIndexBlockReader
  : public ObDirectLoadDataBlockReader<ObDirectLoadMultipleHeapTableIndexBlock::Header,
                                       ObDirectLoadMultipleHeapTableIndexBlock::Entry,
                                       true/*align*/>
{
  typedef ObDirectLoadMultipleHeapTableIndexBlock::Header Header;
  typedef ObDirectLoadMultipleHeapTableIndexBlock::Entry Entry;
  typedef ObDirectLoadDataBlockReader<Header, Entry, true> ParentType;
public:
  ObDirectLoadMultipleHeapTableIndexBlockReader();
  virtual ~ObDirectLoadMultipleHeapTableIndexBlockReader();
  int init(int64_t data_block_size, common::ObCompressorType compressor_type);
  int get_next_index(const ObDirectLoadMultipleHeapTableTabletIndex *&index);
  int get_index(int64_t idx, const ObDirectLoadMultipleHeapTableTabletIndex *&entry);
  int seek_index(int64_t idx);
  const Header &get_header() const { return this->data_block_reader_.get_header(); }
private:
  ObDirectLoadMultipleHeapTableTabletIndex index_;
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadMultipleHeapTableIndexBlockReader);
};

} // namespace storage
} // namespace oceanbase
