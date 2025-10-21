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
#include "storage/direct_load/ob_direct_load_sstable_data_block.h"

namespace oceanbase
{
namespace storage
{

template <typename T>
class ObDirectLoadSSTableDataBlockWriter
  : public ObDirectLoadDataBlockWriter<ObDirectLoadSSTableDataBlock::Header, T>
{
public:
  ObDirectLoadSSTableDataBlockWriter();
  virtual ~ObDirectLoadSSTableDataBlockWriter();
  int append_row(const T &row);
  // When flushing the buffer, read the last line
  int get_flush_last_row(T &row);
private:
  int pre_write_item() override;
  int pre_flush_buffer() override;
private:
  int32_t last_row_pos_;
  int32_t cur_row_pos_; // for one row data block
  DISALLOW_COPY_AND_ASSIGN(ObDirectLoadSSTableDataBlockWriter);
};

template <typename T>
ObDirectLoadSSTableDataBlockWriter<T>::ObDirectLoadSSTableDataBlockWriter()
  : last_row_pos_(-1), cur_row_pos_(-1)
{
}

template <typename T>
ObDirectLoadSSTableDataBlockWriter<T>::~ObDirectLoadSSTableDataBlockWriter()
{
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::append_row(const T &row)
{
  int ret = common::OB_SUCCESS;
  if (OB_FAIL(this->write_item(row))) {
    STORAGE_LOG(WARN, "fail to write item", KR(ret));
  } else {
    last_row_pos_ = cur_row_pos_;
  }
  return ret;
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::get_flush_last_row(T &row)
{
  int ret = common::OB_SUCCESS;
  ObDirectLoadSSTableDataBlock::Header &header = this->data_block_writer_.get_header();
  if (OB_UNLIKELY(header.last_row_pos_ == 0)) {
    ret = OB_ERR_UNEXPECTED;
    STORAGE_LOG(WARN, "unexpected get flush last row", KR(ret), K(header));
  } else {
    int64_t pos = header.last_row_pos_;
    if (OB_FAIL(this->data_block_writer_.read_item(pos, row))) {
      STORAGE_LOG(WARN, "fail to read item", KR(ret), K(pos));
    }
  }
  return ret;
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::pre_write_item()
{
  cur_row_pos_ = this->data_block_writer_.get_pos();
  return common::OB_SUCCESS;
}

template <typename T>
int ObDirectLoadSSTableDataBlockWriter<T>::pre_flush_buffer()
{
  ObDirectLoadSSTableDataBlock::Header &header = this->data_block_writer_.get_header();
  header.last_row_pos_ = (last_row_pos_ != -1 ? last_row_pos_ : cur_row_pos_);
  last_row_pos_ = -1;
  cur_row_pos_ = -1;
  return common::OB_SUCCESS;
}

} // namespace storage
} // namespace oceanbase
