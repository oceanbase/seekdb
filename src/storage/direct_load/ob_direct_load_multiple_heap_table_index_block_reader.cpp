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

#include "storage/direct_load/ob_direct_load_multiple_heap_table_index_block_reader.h"

namespace oceanbase
{
namespace storage
{
using namespace common;

ObDirectLoadMultipleHeapTableIndexBlockReader::ObDirectLoadMultipleHeapTableIndexBlockReader()
{
}

ObDirectLoadMultipleHeapTableIndexBlockReader::~ObDirectLoadMultipleHeapTableIndexBlockReader()
{
}

int ObDirectLoadMultipleHeapTableIndexBlockReader::init(int64_t data_block_size,
                                                        ObCompressorType compressor_type)
{
  return ParentType::init(data_block_size, compressor_type);
}

int ObDirectLoadMultipleHeapTableIndexBlockReader::get_next_index(
  const ObDirectLoadMultipleHeapTableTabletIndex *&index)
{
  int ret = OB_SUCCESS;
  index = nullptr;
  const Entry *item = nullptr;
  if (OB_FAIL(this->get_next_item(item))) {
    if (OB_UNLIKELY(OB_ITER_END != ret)) {
      STORAGE_LOG(WARN, "fail to get next item", KR(ret));
    }
  } else {
    index_.tablet_id_ = item->tablet_id_;
    index_.row_count_ = item->row_count_;
    index_.fragment_idx_ = item->fragment_idx_;
    index_.offset_ = item->offset_;
    index = &index_;
  }
  return ret;
}


int ObDirectLoadMultipleHeapTableIndexBlockReader::get_index(
  int64_t idx, const ObDirectLoadMultipleHeapTableTabletIndex *&index)
{
  int ret = OB_SUCCESS;
  const Header &header = this->data_block_reader_.get_header();
  if (OB_UNLIKELY(idx >= header.count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(header), K(idx));
  } else {
    const int64_t pos = ObDirectLoadMultipleHeapTableIndexBlock::get_header_size() +
                        ObDirectLoadMultipleHeapTableIndexBlock::get_entry_size() * idx;
    Entry item;
    if (OB_FAIL(this->data_block_reader_.read_item(pos, item))) {
      STORAGE_LOG(WARN, "fail to read item", KR(ret));
    } else {
      index_.tablet_id_ = item.tablet_id_;
      index_.row_count_ = item.row_count_;
      index_.fragment_idx_ = item.fragment_idx_;
      index_.offset_ = item.offset_;
      index = &index_;
    }
  }
  return ret;
}

int ObDirectLoadMultipleHeapTableIndexBlockReader::seek_index(int64_t idx)
{
  int ret = OB_SUCCESS;
  const Header &header = this->data_block_reader_.get_header();
  if (OB_UNLIKELY(idx >= header.count_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), K(header), K(idx));
  } else {
    const int64_t pos = ObDirectLoadMultipleHeapTableIndexBlock::get_header_size() +
                        ObDirectLoadMultipleHeapTableIndexBlock::get_entry_size() * idx;
    if (OB_FAIL(this->data_block_reader_.set_pos(pos))) {
      STORAGE_LOG(WARN, "fail to set pos", KR(ret));
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
