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
#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_sstable_index_block_writer.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

ObDirectLoadSSTableIndexBlockWriter::ObDirectLoadSSTableIndexBlockWriter()
  : start_offset_(0), entry_count_(0), last_entry_pos_(-1), cur_entry_pos_(-1)
{
}

ObDirectLoadSSTableIndexBlockWriter::~ObDirectLoadSSTableIndexBlockWriter()
{
}

int ObDirectLoadSSTableIndexBlockWriter::init(int64_t data_block_size,
                                              ObCompressorType compressor_type)
{
  return ObDirectLoadDataBlockWriter::init(data_block_size, compressor_type, nullptr, 0, nullptr);
}

int ObDirectLoadSSTableIndexBlockWriter::append_entry(const ObDirectLoadSSTableIndexEntry &entry)
{
  int ret = OB_SUCCESS;
  ObDirectLoadSSTableIndexBlock::Entry item;
  item.offset_ = entry.offset_ + entry.size_;
  if (OB_FAIL(this->write_item(item))) {
    STORAGE_LOG(WARN, "fail to write item", KR(ret));
  } else {
    if (0 == entry_count_) {
      start_offset_ = entry.offset_;
    }
    ++entry_count_;
    last_entry_pos_ = cur_entry_pos_;
  }
  return ret;
}

int ObDirectLoadSSTableIndexBlockWriter::pre_write_item()
{
  cur_entry_pos_ = this->data_block_writer_.get_pos();
  return OB_SUCCESS;
}

int ObDirectLoadSSTableIndexBlockWriter::pre_flush_buffer()
{
  ObDirectLoadSSTableIndexBlock::Header &header = this->data_block_writer_.get_header();
  header.offset_ = start_offset_;
  header.count_ = entry_count_;
  header.last_entry_pos_ = (last_entry_pos_ != -1 ? last_entry_pos_ : cur_entry_pos_);
  start_offset_ = 0;
  entry_count_ = 0;
  last_entry_pos_ = -1;
  cur_entry_pos_ = -1;
  return OB_SUCCESS;
}

} // namespace storage
} // namespace oceanbase
