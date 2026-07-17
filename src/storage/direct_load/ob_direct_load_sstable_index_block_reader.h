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
#include "storage/direct_load/ob_direct_load_sstable_index_block.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadSSTableIndexBlockReader
  : public ObDirectLoadDataBlockReader<ObDirectLoadSSTableIndexBlock::Header,
                                       ObDirectLoadSSTableIndexBlock::Entry,
                                       true/*align*/>
{
  typedef ObDirectLoadDataBlockReader<ObDirectLoadSSTableIndexBlock::Header,
                                      ObDirectLoadSSTableIndexBlock::Entry,
                                      true>
    ParentType;
public:
  ObDirectLoadSSTableIndexBlockReader();
  virtual ~ObDirectLoadSSTableIndexBlockReader();
  int init(int64_t data_block_size, common::ObCompressorType compressor_type);
  int get_next_entry(const ObDirectLoadSSTableIndexEntry *&entry);
  int get_last_entry(const ObDirectLoadSSTableIndexEntry *&entry);
  int get_entry(int64_t idx, const ObDirectLoadSSTableIndexEntry *&entry);
  const ObDirectLoadSSTableIndexBlock::Header &get_header() const
  {
    return this->data_block_reader_.get_header();
  }
protected:
  int prepare_read_block() override;
private:
  int64_t last_offset_;
  ObDirectLoadSSTableIndexEntry entry_;
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadSSTableIndexBlockReader);
};

} // namespace storage
} // namespace oceanbase
