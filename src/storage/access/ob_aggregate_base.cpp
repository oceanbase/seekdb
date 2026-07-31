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
#include "ob_aggregate_base.h"
#include "lib/container/ob_bitmap.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/blocksstable/index_block/ob_agg_row_struct.h"

namespace oceanbase
{
namespace storage
{
ObAggCellBase::ObAggCellBase(common::ObIAllocator &allocator)
  : bitmap_(nullptr),
    agg_row_reader_(nullptr),
    result_datum_(),
    skip_index_datum_(),
    allocator_(allocator),
    agg_type_(PD_MAX_TYPE),
    is_assigned_to_group_by_processor_(false),
    skip_index_datum_is_prefix_(false),
    is_inited_(false)
{
}

void ObAggCellBase::reset()
{
  if (nullptr != bitmap_) {
    bitmap_->~ObBitmap();
    allocator_.free(bitmap_);
    bitmap_ = nullptr;
  }
  if (nullptr != agg_row_reader_) {
    agg_row_reader_->~ObAggRowReader();
    allocator_.free(agg_row_reader_);
    agg_row_reader_ = nullptr;
  }
  result_datum_.reset();
  skip_index_datum_.reset();
  skip_index_datum_is_prefix_ = false;
  is_assigned_to_group_by_processor_ = false;
  agg_type_ = PD_MAX_TYPE;
  is_inited_ = false;
}

void ObAggCellBase::reuse()
{
  if (nullptr != bitmap_) {
    bitmap_->reuse();
  }
  result_datum_.reuse();
  result_datum_.set_null();
  skip_index_datum_.reuse();
  skip_index_datum_.set_null();
  skip_index_datum_is_prefix_ = false;
}

int ObAggCellBase::reserve_bitmap(const int64_t count)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("aggregate cell not inited", K(ret));
  } else if (OB_UNLIKELY(count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid count", K(ret), K(count));
  } else if (OB_NOT_NULL(bitmap_)) {
    if (OB_FAIL(bitmap_->reserve(count))) {
      LOG_WARN("Failed to reserve bitmap", K(ret));
    } else {
      bitmap_->reuse(); // all false
    }
  } else {
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObBitmap)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc memory for bitmap", K(ret));
    } else if (FALSE_IT(bitmap_ = new (buf) ObBitmap(allocator_))) {
    } else if (OB_FAIL(bitmap_->init(count))) { // all false
      LOG_WARN("Failed to init bitmap", K(ret));
    }
  }
  return ret;
}

ObGroupByCellBase::ObGroupByCellBase(const int64_t batch_size, common::ObIAllocator &allocator)
  : batch_size_(batch_size),
    row_capacity_(batch_size),
    distinct_cnt_(0),
    ref_cnt_(0),
    projected_cnt_(0),
    refs_buf_(nullptr),
    group_by_col_expr_(nullptr),
    group_by_col_param_(nullptr),
    distinct_projector_buf_(nullptr),
    padding_allocator_("GroupByPad", OB_MALLOC_NORMAL_BLOCK_SIZE),
    allocator_(allocator),
    group_by_col_offset_(-1),
    need_extract_distinct_(false),
    is_processing_(false),
    is_inited_(false)
{
}

ObGroupByCellBase::~ObGroupByCellBase()
{
  reset();
}

void ObGroupByCellBase::reset()
{
  batch_size_ = 0;
  row_capacity_ = 0;
  group_by_col_offset_ = -1;
  group_by_col_expr_ = nullptr;
  distinct_cnt_ = 0;
  ref_cnt_ = 0;
  if (nullptr != refs_buf_) {
    allocator_.free(refs_buf_);
    refs_buf_ = nullptr;
  }
  need_extract_distinct_ = false;
  free_group_by_buf(allocator_, distinct_projector_buf_);
  padding_allocator_.reset();
  is_processing_ = false;
  projected_cnt_ = 0;
  is_inited_ = false;
}

void ObGroupByCellBase::reuse()
{
  distinct_cnt_ = 0;
  ref_cnt_ = 0;
  need_extract_distinct_ = false;
  if (nullptr != distinct_projector_buf_) {
    distinct_projector_buf_->fill_items(-1);
  }
  padding_allocator_.reuse();
  is_processing_ = false;
  projected_cnt_ = 0;
}

int ObGroupByCellBase::check_distinct_and_ref_valid()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ref_cnt_ <= 0 || distinct_cnt_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected state", K(ret), K(ref_cnt_), K(distinct_cnt_));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < ref_cnt_; ++i) {
    if (OB_UNLIKELY(refs_buf_[i] >= distinct_cnt_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected ref", K(ret), K(i), K(ref_cnt_), K(distinct_cnt_), K(ObArrayWrap<uint32_t>(refs_buf_, ref_cnt_)));
    }
  }
  return ret;
}

ObAggDatumBuf::ObAggDatumBuf(common::ObIAllocator &allocator)
  : size_(0), capacity_(0), datum_size_(0), datums_(nullptr), buf_(nullptr), cell_data_ptrs_(nullptr), allocator_(allocator)
{
}

int ObAggDatumBuf::init(const int64_t size, const bool need_cell_data_ptr, const int64_t datum_size)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (OB_UNLIKELY(size <= 0 || datum_size <= 0 || datum_size > common::OBJ_DATUM_DECIMALINT_MAX_RES_SIZE)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(size), K(datum_size));
  } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObDatum) * size))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc datum buf", K(ret), K(size));
  } else if (FALSE_IT(datums_ = new (buf) ObDatum[size])) {
  } else if (OB_ISNULL(buf = allocator_.alloc(datum_size * size))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc datum buf", K(ret), K(size));
  } else if (FALSE_IT(buf_ = static_cast<char*>(buf))) {
  } else if (need_cell_data_ptr) {
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(char*) * size))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc cell data ptrs", K(ret), K(size));
    } else {
      cell_data_ptrs_ = static_cast<const char**> (buf);
    }
  }
  if (OB_SUCC(ret)) {
    size_ = size;
    capacity_ = size;
    datum_size_ = datum_size;
    reuse();
  } else {
    reset();
  }
  return ret;
}

void ObAggDatumBuf::reset()
{
  if (OB_NOT_NULL(datums_)) {
    allocator_.free(datums_);
    datums_ = nullptr;
  }
  if (OB_NOT_NULL(buf_)) {
    allocator_.free(buf_);
    buf_ = nullptr;
  }
  if (OB_NOT_NULL(cell_data_ptrs_)) {
    allocator_.free(cell_data_ptrs_);
    cell_data_ptrs_ = nullptr;
  }
  size_ = 0;
  capacity_ = 0;
  datum_size_ = 0;
}

void ObAggDatumBuf::reuse()
{
  for(int64_t i = 0; i < size_; ++i) {
    datums_[i].pack_ = 0;
    datums_[i].ptr_ = buf_ + i * datum_size_;
  }
}

int ObAggDatumBuf::new_agg_datum_buf(
    const int64_t size,
    const bool need_cell_data_ptr,
    common::ObIAllocator &allocator,
    ObAggDatumBuf *&datum_buf,
    const int64_t datum_size)
{
  int ret = OB_SUCCESS;
  int64_t new_size = size;
  if (OB_UNLIKELY(size <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid size", K(ret), K(size));
  } else if (nullptr != datum_buf) {
    if (size > datum_buf->get_capacity()) {
      new_size = MAX(size, 2 * datum_buf->get_capacity());
      allocator.reuse();
      datum_buf = nullptr;
    } else {
      datum_buf->set_size(size);
      datum_buf->reuse();
    }
  } 
  if (OB_SUCC(ret) && nullptr == datum_buf) {
    void *buf = nullptr;
    if (OB_ISNULL(buf = allocator.alloc(sizeof(ObAggDatumBuf)))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("Failed to alloc agg datum buffer", K(ret));
    } else if (FALSE_IT(datum_buf = new (buf) ObAggDatumBuf(allocator))) {
    } else if (OB_FAIL(datum_buf->init(new_size, need_cell_data_ptr, datum_size))) {
      LOG_WARN("Failed to init agg datum buf", K(ret));
    }
  }
  return ret;
}


ObAggGroupByDatumBuf::ObAggGroupByDatumBuf(
    common::ObDatum *basic_data,
    const int32_t basic_size,
    const int32_t datum_size,
    common::ObIAllocator &allocator)
    : capacity_(basic_size),
      sql_datums_cnt_(basic_size),
      sql_result_datums_(basic_data),
      result_datum_buf_(nullptr),
      datum_size_(datum_size),
      allocator_(allocator)
{
}

void ObAggGroupByDatumBuf::reset()
{
  capacity_ = 0;
  sql_result_datums_ = nullptr;
  sql_datums_cnt_ = 0;
  if (nullptr != result_datum_buf_) {
    result_datum_buf_->reset();
    allocator_.free(result_datum_buf_);
  }
  result_datum_buf_ = nullptr;
  datum_size_ = 0;
}

int ObAggGroupByDatumBuf::reserve(const int32_t size)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(size <= 0 || size > USE_GROUP_BY_MAX_DISTINCT_CNT)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Unexpected size", K(ret), K(size));
  } else {
    capacity_ = MAX(sql_datums_cnt_, size);
    if (is_use_extra_buf()) {
      if (OB_ISNULL(result_datum_buf_)) {
        if (OB_FAIL(ObAggDatumBuf::new_agg_datum_buf(USE_GROUP_BY_MAX_DISTINCT_CNT,
            true, allocator_, result_datum_buf_, datum_size_))) {
          LOG_WARN("Failed to alloc agg datum buf", K(ret));
        }
      }
    }
  }
  return ret;
}


}
}
