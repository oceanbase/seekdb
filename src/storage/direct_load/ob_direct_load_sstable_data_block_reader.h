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
#include "storage/direct_load/ob_direct_load_sstable_data_block.h"

namespace oceanbase
{
namespace storage
{

template <typename T>
class ObDirectLoadSSTableDataBlockReader
  : public ObDirectLoadDataBlockReader<ObDirectLoadSSTableDataBlock::Header, T>
{
public:
  ObDirectLoadSSTableDataBlockReader() = default;
  virtual ~ObDirectLoadSSTableDataBlockReader() = default;
  int get_next_row(const T *&row);
  int get_last_row(const T *&row);
};

template <typename T>
int ObDirectLoadSSTableDataBlockReader<T>::get_next_row(const T *&row)
{
  return this->get_next_item(row);
}

template <typename T>
int ObDirectLoadSSTableDataBlockReader<T>::get_last_row(const T *&row)
{
  int ret = common::OB_SUCCESS;
  const ObDirectLoadSSTableDataBlock::Header &header = this->data_block_reader_.get_header();
  this->curr_item_.reuse();
  if (OB_FAIL(this->data_block_reader_.read_item(header.last_row_pos_, this->curr_item_))) {
    STORAGE_LOG(WARN, "fail to read item", KR(ret));
  } else {
    row = &this->curr_item_;
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
