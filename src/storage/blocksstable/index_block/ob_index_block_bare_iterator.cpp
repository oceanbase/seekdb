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

#include "ob_index_block_bare_iterator.h"
#include "storage/blocksstable/index_block/ob_index_block_row_struct.h"

namespace oceanbase
{
namespace blocksstable
{

ObIndexBlockBareIterator::ObIndexBlockBareIterator()
    : ObMicroBlockBareIterator{},
      rowkey_column_count_(0),
      cur_row_idx_(0),
      row_count_(0),
      row_{}
{}

ObIndexBlockBareIterator::~ObIndexBlockBareIterator()
{
  if (is_inited_) {
    reset();
  }
}

void ObIndexBlockBareIterator::reset()
{
  rowkey_column_count_ = 0;
  cur_row_idx_ = 0;
  row_count_ = 0;
  row_.reset();
  ObMicroBlockBareIterator::reset();
}

int ObIndexBlockBareIterator::open(
    const char *macro_block_buf,
    const int64_t macro_block_buf_size,
    const bool is_macro_meta_block,
    const bool need_check_data_integrity)
{
  int ret = OB_SUCCESS;
  ObMicroBlockData index_micro_block;
  const ObMicroBlockHeader *index_micro_block_header = nullptr;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObIndexBlockBareIterator already inited", KR(ret));
  } else if (OB_FAIL(ObMicroBlockBareIterator::open(
      macro_block_buf, macro_block_buf_size,
      need_check_data_integrity, false/*need_deserialize*/))) {
  } else if (OB_FAIL(get_index_block(
      index_micro_block, true/*force_deserialize*/, is_macro_meta_block))) {
  } else if (OB_ISNULL(index_micro_block_header = index_micro_block.get_micro_header())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get index micro block header", KR(ret), K(index_micro_block), KPC(this));
  } else if (OB_FAIL(set_reader(get_row_type()))) {
  } else if (OB_FAIL(reader_->init(index_micro_block, nullptr/*datum_utils*/))) {
  } else if (OB_FAIL(reader_->get_row_count(row_count_))) {
  } else if (OB_FAIL(row_.init(allocator_, index_micro_block_header->column_count_))) {
  } else {
    is_inited_ = true;
    rowkey_column_count_ = index_micro_block_header->rowkey_column_count_;
    cur_row_idx_ = 0;
  }

  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObIndexBlockBareIterator::get_next_logic_micro_id(
    ObLogicMicroBlockId &logic_micro_id, int64_t &micro_checksum)
{
  int ret = OB_SUCCESS;
  row_.reuse();
  logic_micro_id.reset();
  micro_checksum = 0;
  ObIndexBlockRowParser idx_row_parser;
  const ObIndexBlockRowHeader *idx_row_header = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObIndexBlockBareIterator not inited", KR(ret));
  } else if (OB_UNLIKELY(cur_row_idx_ >= row_count_)) {
    ret = OB_ITER_END;
    // skip log
  } else if (OB_FAIL(reader_->get_row(cur_row_idx_, row_))) {
  } else if (OB_FAIL(idx_row_parser.init(rowkey_column_count_, row_))) {
  } else if (OB_FAIL(idx_row_parser.get_header(idx_row_header))) {
  } else if (OB_ISNULL(idx_row_header)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("idx_row_header is NULL", KR(ret), K(idx_row_parser), K(row_), KPC(this));
  } else if (OB_UNLIKELY(!idx_row_header->has_valid_logic_micro_id())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("logic micro id is invalid", KR(ret),
        K(idx_row_header), K(idx_row_parser), K(row_), KPC(this));
  } else {
    logic_micro_id = idx_row_header->get_logic_micro_id();
    micro_checksum = idx_row_header->get_data_checksum();
    cur_row_idx_++;
  }
  return ret;
}

} // namespace blocksstable
} // namespace oceanbase
