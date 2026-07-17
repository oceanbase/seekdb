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

#include "storage/direct_load/ob_direct_load_data_block_writer.h"
#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadMultipleHeapTableIndexBlockWriter
  : public ObDirectLoadDataBlockWriter<ObDirectLoadMultipleHeapTableIndexBlock::Header,
                                       ObDirectLoadMultipleHeapTableIndexBlock::Entry,
                                       true/*align*/>
{
public:
  ObDirectLoadMultipleHeapTableIndexBlockWriter();
  virtual ~ObDirectLoadMultipleHeapTableIndexBlockWriter();
  int init(int64_t data_block_size, common::ObCompressorType compressor_type);
  int append_index(const ObDirectLoadMultipleHeapTableTabletIndex &tablet_index);
private:
  int pre_write_item() override;
  int pre_flush_buffer() override;
private:
  int64_t entry_count_;
  int32_t last_entry_pos_;
  int32_t cur_entry_pos_;
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadMultipleHeapTableIndexBlockWriter);
};

} // namespace storage
} // namespace oceanbase
