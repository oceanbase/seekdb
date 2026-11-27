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

#include "ob_imicro_block_writer.h"
#include "storage/blocksstable/index_block/ob_index_block_aggregator.h"
namespace oceanbase
{
namespace blocksstable
{

/**
 * -------------------------------------------------------------------ObMicroBlockDesc-------------------------------------------------------------------
 */
bool ObMicroBlockDesc::is_valid() const
{
  return last_rowkey_.is_valid()
         && NULL != header_ && header_->is_valid()
         && NULL != buf_ && buf_size_ > 0
         && data_size_ > 0 && row_count_ > 0 && column_count_ > 0
         && max_merged_trans_version_  >= 0;
}

void ObMicroBlockDesc::reset()
{
  last_rowkey_.reset();
  buf_ = NULL;
  header_ = nullptr;
  header_storage_.reset();
  serialized_header_ = nullptr;
  writable_header_ = nullptr;
  aggregated_row_ = NULL;
  buf_size_ = 0;
  data_size_ = 0;
  row_count_ = 0;
  column_count_ = 0;
  max_merged_trans_version_ = 0;
  macro_id_.reset();
  logic_micro_id_.reset();
  block_offset_ = 0;
  block_checksum_ = 0;
  row_count_delta_ = 0;
  contain_uncommitted_row_ = false;
  can_mark_deletion_ = false;
  has_string_out_row_ = false;
  has_lob_out_row_ = false;
  original_size_ = 0;
  is_last_row_last_flag_ = false;
}

bool ObMicroBlockDesc::is_complete_micro_block_memory() const
{
  return nullptr != header_ && nullptr != serialized_header_
      && serialized_header_ + header_->header_size_ == buf_;
}

void ObMicroBlockDesc::set_header(const ObMicroBlockHeader &header, const char *serialized_header)
{
  header_storage_ = header;
  header_ = &header_storage_;
  serialized_header_ = serialized_header;
  writable_header_ = nullptr;
}

int ObMicroBlockDesc::init_header(const char *block, const int64_t size)
{
  ObMicroBlockHeader header;
  const int ret = header.deserialize_and_check_header(block, size);
  if (OB_SUCCESS == ret) {
    set_header(header, block);
  }
  return ret;
}

int ObMicroBlockDesc::init_writable_header(char *block, const int64_t size)
{
  const int ret = init_header(block, size);
  if (OB_SUCCESS == ret) {
    writable_header_ = block;
  }
  return ret;
}

int ObMicroBlockDesc::seal(const char *payload, const int64_t size, common::ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  ObMicroBlockHeader final_header = header_storage_;
  char *prefix = writable_header_;
  if (nullptr == prefix) {
    int64_t pos = 0;
    if (OB_ISNULL(prefix = static_cast<char *>(allocator.alloc(final_header.header_size_)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(header_storage_.serialize(prefix, final_header.header_size_, pos))) {
    } else if (OB_FAIL(final_header.deserialize_and_check_header(prefix, pos))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(final_header.seal(prefix, payload, size))) {
  } else {
    set_header(final_header, prefix);
    writable_header_ = prefix;
    buf_ = payload;
    buf_size_ = size;
  }
  return ret;
}

int ObMicroBlockDesc::deep_copy(
    common::ObIAllocator& allocator,
    ObMicroBlockDesc& dst) const
{
  int ret = OB_SUCCESS;
  if (this != &dst) {
    dst.reset();
    char * block_buffer = nullptr;
    void * row_buffer = nullptr;
    ObSkipIndexAggResult *agg_row = nullptr;

    if (OB_ISNULL(header_) || OB_ISNULL(buf_)) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "can't copy invalid desc", K(ret), K(*this));
    } else if (OB_FAIL(last_rowkey_.deep_copy(dst.last_rowkey_, allocator))) {
    } else {
      const int64_t block_size = header_->header_size_ + buf_size_;
      int64_t pos = 0;
      if (block_size == 0) {
        ret = OB_ERR_UNEXPECTED;
        STORAGE_LOG(WARN, "empty micro block desc", K(ret), K(*this));
      } else if (OB_ISNULL(block_buffer = (char *)allocator.alloc(block_size))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        STORAGE_LOG(WARN, "failed to alloc micro block buf", K(ret));
      } else if (OB_FAIL(header_->serialize(block_buffer, block_size, pos))) {
      } else if (OB_FAIL(dst.init_header(block_buffer, pos))) {
      } else {
        MEMCPY(block_buffer + pos, buf_, buf_size_);
        dst.buf_ = block_buffer + pos;
        dst.buf_size_ = buf_size_;
        dst.data_size_ = data_size_;
        dst.original_size_ = original_size_;
        dst.row_count_ = row_count_;
        dst.column_count_ = column_count_;
        dst.max_merged_trans_version_ = max_merged_trans_version_;
        dst.macro_id_ = macro_id_;
        dst.block_offset_ = block_offset_;
        dst.block_checksum_ = block_checksum_;
        dst.row_count_delta_ = row_count_delta_;
        dst.contain_uncommitted_row_ = contain_uncommitted_row_;
        dst.can_mark_deletion_ = can_mark_deletion_;
        dst.has_string_out_row_ = has_string_out_row_;
        dst.has_lob_out_row_ = has_lob_out_row_;
        dst.is_last_row_last_flag_ = is_last_row_last_flag_;

        if (nullptr != aggregated_row_) {
          if (OB_ISNULL(row_buffer = allocator.alloc(sizeof(ObSkipIndexAggResult)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            STORAGE_LOG(WARN, "failed to alloc row buf", K(ret));
          } else if (FALSE_IT(agg_row = new (row_buffer) ObSkipIndexAggResult())) {
          } else if (OB_FAIL(agg_row->init(aggregated_row_->get_agg_col_cnt(), allocator))) {
          } else if (OB_FAIL(agg_row->deep_copy(*aggregated_row_, allocator))) {
          } else {
            dst.aggregated_row_ = agg_row;
          }
        }
      }
    }

    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(block_buffer)) {
        allocator.free(block_buffer);
      }

      if (OB_NOT_NULL(agg_row)) {
        agg_row->~ObSkipIndexAggResult();
      }

      if (OB_NOT_NULL(row_buffer)) {
        allocator.free(row_buffer);
      }
    }
  } 
  return ret;
}

 /**
 * -------------------------------------------------------------------ObIMicroBlockWriter-------------------------------------------------------------------
 */
int ObIMicroBlockWriter::build_micro_block_desc(ObMicroBlockDesc &micro_block_desc)
{
  int ret = OB_SUCCESS;
  micro_block_desc.reset();

  char *block_buffer = NULL;
  int64_t block_size = 0;
  if (OB_FAIL(build_block(block_buffer, block_size))) {
  } else if (OB_FAIL(micro_block_desc.init_writable_header(block_buffer, block_size))) {
  } else {
    micro_block_desc.buf_ = block_buffer + micro_block_desc.header_->header_size_;
    micro_block_desc.buf_size_ = block_size - micro_block_desc.header_->header_size_;
    micro_block_desc.data_size_ = micro_block_desc.buf_size_;
    micro_block_desc.row_count_ = get_row_count();
    micro_block_desc.column_count_ = get_column_count();
    micro_block_desc.row_count_delta_ = get_row_count_delta();
    micro_block_desc.max_merged_trans_version_ = get_max_merged_trans_version();
    micro_block_desc.contain_uncommitted_row_ = is_contain_uncommitted_row();
    micro_block_desc.block_checksum_ = get_micro_block_checksum();
    micro_block_desc.has_string_out_row_ = has_string_out_row_;
    micro_block_desc.has_lob_out_row_ = has_lob_out_row_;
    micro_block_desc.original_size_ = micro_block_desc.header_->original_length_;
    micro_block_desc.is_last_row_last_flag_ = is_last_row_last_flag();
  }
  // do not reuse micro writer here
  return ret;
}

}//end namespace blocksstable
}//end namespace oceanbase
