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
#include "common/datum/datum_parameter_codec.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/blocksstable/index_block/ob_agg_row_struct.h"

namespace oceanbase
{
namespace storage
{
using namespace common;
void ObAggNativeCell::reset()
{
  value_.set_null();
  if (buffer_ != nullptr && owner_ != nullptr) { owner_->free(buffer_); }
  buffer_ = nullptr; capacity_ = 0; type_ = nullptr;
  domain_ = ObAggNativeDomain::Semantic; coefficient_bytes_ = 0;
}

int ObAggNativeCell::physical_meta(SqlTypeMeta &meta) const
{
  int ret = OB_SUCCESS;
  if (domain_ == ObAggNativeDomain::Signed) { meta.set_int(); }
  else if (domain_ == ObAggNativeDomain::Float32) { meta.set_type(ObFloatType); }
  else if (domain_ == ObAggNativeDomain::Float64) { meta.set_type(ObDoubleType); }
  else if (domain_ == ObAggNativeDomain::Number) { meta.set_type(ObNumberType); }
  else if (domain_ == ObAggNativeDomain::DecimalCoefficient) { meta.set_decimal_int(0); }
  else { ret = OB_STATE_NOT_MATCH; }
  return ret;
}

int ObAggNativeCell::capture_legacy(const ObDatum &source)
{
  int ret = OB_SUCCESS;
  Datum candidate;
  if (domain_ == ObAggNativeDomain::Semantic) {
    if (type_ == nullptr) { ret = OB_NOT_INIT; }
    else { ret = DatumCopyPlan(*type_).import_legacy(source, candidate); }
  } else {
    SqlTypeMeta meta;
    if (OB_FAIL(physical_meta(meta))) {
    } else if (domain_ == ObAggNativeDomain::DecimalCoefficient && !source.is_null()
        && source.len_ != 4 && source.len_ != 8 && source.len_ != 16
        && source.len_ != 32 && source.len_ != 64) { ret = OB_INVALID_ARGUMENT; }
    else { ret = DatumParameterCodec::borrow_native_value(source, meta, candidate); }
  }
  if (OB_SUCC(ret)) { ret = capture(candidate); }
  return ret;
}

int ObAggNativeCell::capture_native(const Datum &source)
{
  if (domain_ != ObAggNativeDomain::Semantic || type_ == nullptr) { return OB_STATE_NOT_MATCH; }
  int64_t checked = 0;
  const int ret = DatumCopyPlan(*type_).carrier_span_size(source, checked);
  return ret == OB_SUCCESS ? capture(source) : ret;
}

int ObAggNativeCell::capture(const Datum &source)
{
  const Datum candidate = source;
  int ret = OB_SUCCESS;
  int64_t bytes = 0;
  if (domain_ == ObAggNativeDomain::Semantic) {
    if (type_ == nullptr) { ret = OB_NOT_INIT; }
    else { ret = DatumCopyPlan(*type_).carrier_span_size(candidate, bytes); }
  } else { bytes = candidate.is_null() ? 0 : candidate.length(); }
  if (OB_FAIL(ret)) {
  } else if (bytes == 0) { value_ = candidate; }
  else if (owner_ == nullptr || bytes > UINT32_MAX) { ret = OB_INVALID_ARGUMENT; }
  else {
    int64_t needed = domain_ == ObAggNativeDomain::Number || domain_ == ObAggNativeDomain::DecimalCoefficient
        ? std::max<int64_t>(64, bytes) : bytes;
    char *next = buffer_;
    if (needed > capacity_) {
      next = static_cast<char *>(owner_->alloc(needed));
      if (next == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    }
    if (OB_SUCC(ret)) {
      Datum owned;
      if (domain_ == ObAggNativeDomain::Semantic) {
        int64_t pos = 0;
        ret = DatumCopyPlan(*type_).copy_carrier_span_borrowing_resources(
            candidate, owned, next, needed, pos);
      } else {
        MEMMOVE(next, candidate.get_reference(), bytes);
        ret = owned.set_reference(next, bytes, candidate.reference_flags());
      }
      if (OB_SUCC(ret)) {
        char *old = buffer_;
        buffer_ = next; capacity_ = std::max(capacity_, needed); value_ = owned;
        if (old != next && old != nullptr) { owner_->free(old); }
        if (domain_ == ObAggNativeDomain::DecimalCoefficient) { coefficient_bytes_ = bytes; }
      } else if (next != buffer_) { owner_->free(next); }
    }
  }
  return ret;
}

int ObAggNativeCell::export_legacy(ObDatum &output, void *scratch, int64_t capacity) const
{
  if (domain_ == ObAggNativeDomain::Semantic) {
    return type_ == nullptr ? OB_NOT_INIT : DatumCopyPlan(*type_).legacy_view(value_, output, scratch, capacity);
  }
  SqlTypeMeta meta;
  const int ret = physical_meta(meta);
  return ret == OB_SUCCESS ? DatumParameterCodec::export_native_value(value_, meta, output, scratch, capacity) : ret;
}

int ObAggNativeCell::edit_view(ObDatum &output, void *scratch, int64_t capacity) const
{
  if (scratch == nullptr || capacity < 64) { return OB_INVALID_ARGUMENT; }
  output.ptr_ = static_cast<char *>(scratch);
  const int ret = export_legacy(output, scratch, capacity);
  if (ret == OB_SUCCESS && output.is_null()) { output.ptr_ = static_cast<char *>(scratch); }
  return ret;
}

void ObAggNativeCell::take(ObAggNativeCell &source)
{
  reset();
  owner_ = source.owner_; type_ = source.type_; value_ = source.value_;
  buffer_ = source.buffer_; capacity_ = source.capacity_;
  domain_ = source.domain_; coefficient_bytes_ = source.coefficient_bytes_;
  source.value_.set_null(); source.buffer_ = nullptr; source.capacity_ = 0;
}

int ObNativeAggDatumBuf::init(int64_t basic_count, const SqlType *type,
    ObAggNativeDomain domain, int32_t coefficient_bytes)
{
  if (basic_count <= 0 || basic_count > USE_GROUP_BY_MAX_DISTINCT_CNT || cells_ != nullptr) { return OB_INVALID_ARGUMENT; }
  basic_count_ = basic_count; type_ = type; domain_ = domain; coefficient_bytes_ = coefficient_bytes;
  return reserve(basic_count);
}

int ObNativeAggDatumBuf::reserve(int64_t count)
{
  if (count <= 0 || count > USE_GROUP_BY_MAX_DISTINCT_CNT) { return OB_INVALID_ARGUMENT; }
  const int64_t needed = std::max(count, basic_count_);
  if (needed > allocated_) {
    const int64_t next_count = std::min<int64_t>(USE_GROUP_BY_MAX_DISTINCT_CNT,
        std::max(needed, allocated_ > 0 ? allocated_ * 2 : needed));
    if (next_count > INT64_MAX / sizeof(ObAggNativeCell)) { return OB_SIZE_OVERFLOW; }
    auto *next = static_cast<ObAggNativeCell *>(owner_.alloc(next_count * sizeof(ObAggNativeCell)));
    if (next == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    for (int64_t i = 0; i < next_count; ++i) {
      new (next + i) ObAggNativeCell(&owner_);
      next[i].configure(type_, domain_, coefficient_bytes_);
    }
    for (int64_t i = 0; i < allocated_; ++i) { next[i].take(cells_[i]); cells_[i].~ObAggNativeCell(); }
    if (cells_ != nullptr) { owner_.free(cells_); }
    cells_ = next; allocated_ = next_count;
  }
  capacity_ = needed;
  return OB_SUCCESS;
}

void ObNativeAggDatumBuf::reset()
{
  for (int64_t i = 0; i < allocated_; ++i) { cells_[i].~ObAggNativeCell(); }
  if (cells_ != nullptr) { owner_.free(cells_); }
  cells_ = nullptr; allocated_ = capacity_ = basic_count_ = 0;
}
void ObNativeAggDatumBuf::reuse()
{ for (int64_t i = 0; i < allocated_; ++i) { cells_[i].reuse(); } }
void ObNativeAggDatumBuf::configure(const SqlType *type, ObAggNativeDomain domain, int32_t width)
{
  type_ = type; domain_ = domain; coefficient_bytes_ = width;
  for (int64_t i = 0; i < allocated_; ++i) { cells_[i].configure(type, domain, width); }
}
void ObNativeAggDatumBuf::fill_datum_items(FillDatumType fill)
{
  for (int64_t i = 0; i < capacity_; ++i) {
    if (fill == ZERO_DATUM) { cells_[i].set_int(0); }
    else { cells_[i].set_null(); }
  }
}
void ObNativeAggDatumBuf::swap(ObNativeAggDatumBuf &other)
{
  OB_ASSERT(&owner_ == &other.owner_);
  std::swap(cells_, other.cells_); std::swap(allocated_, other.allocated_);
  std::swap(capacity_, other.capacity_); std::swap(basic_count_, other.basic_count_);
  std::swap(type_, other.type_); std::swap(domain_, other.domain_); std::swap(coefficient_bytes_, other.coefficient_bytes_);
}

ObAggCellBase::ObAggCellBase(common::ObIAllocator &allocator)
  : bitmap_(nullptr),
    agg_row_reader_(nullptr),
    result_datum_(&allocator),
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
  } else if (OB_UNLIKELY(count <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_NOT_NULL(bitmap_)) {
    if (OB_FAIL(bitmap_->reserve(count))) {
    } else {
      bitmap_->reuse(); // all false
    }
  } else {
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObBitmap)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(bitmap_ = new (buf) ObBitmap(allocator_))) {
    } else if (OB_FAIL(bitmap_->init(count))) {
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
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < ref_cnt_; ++i) {
    if (OB_UNLIKELY(refs_buf_[i] >= distinct_cnt_)) {
      ret = OB_ERR_UNEXPECTED;
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
  } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObDatum) * size))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(datums_ = new (buf) ObDatum[size])) {
  } else if (OB_ISNULL(buf = allocator_.alloc(datum_size * size))) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(buf_ = static_cast<char*>(buf))) {
  } else if (need_cell_data_ptr) {
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(char*) * size))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
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
  if (size <= 0) { return OB_INVALID_ARGUMENT; }
  if (datum_buf != nullptr && size <= datum_buf->get_capacity()) {
    datum_buf->set_size(size);
    datum_buf->reuse();
    return OB_SUCCESS;
  }
  int64_t new_size = size;
  if (datum_buf != nullptr) {
    if (datum_buf->get_capacity() > INT64_MAX / 2) { return OB_SIZE_OVERFLOW; }
    new_size = std::max(size, 2 * datum_buf->get_capacity());
  }
  if (new_size > INT64_MAX / static_cast<int64_t>(sizeof(ObDatum))
      || datum_size <= 0 || new_size > INT64_MAX / datum_size
      || new_size > INT64_MAX / static_cast<int64_t>(sizeof(char *))) { return OB_SIZE_OVERFLOW; }
  void *memory = allocator.alloc(sizeof(ObAggDatumBuf));
  if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  auto *candidate = new(memory) ObAggDatumBuf(allocator);
  int ret = candidate->init(new_size, need_cell_data_ptr, datum_size);
  if (OB_FAIL(ret)) { candidate->~ObAggDatumBuf(); allocator.free(candidate); }
  else {
    if (datum_buf != nullptr) { datum_buf->~ObAggDatumBuf(); allocator.free(datum_buf); }
    datum_buf = candidate;
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
  if (size <= 0 || size > USE_GROUP_BY_MAX_DISTINCT_CNT) { ret = OB_INVALID_ARGUMENT; }
  else {
    const int32_t candidate_capacity = std::max(sql_datums_cnt_, size);
    if (candidate_capacity > sql_datums_cnt_ && result_datum_buf_ == nullptr) {
      ret = ObAggDatumBuf::new_agg_datum_buf(USE_GROUP_BY_MAX_DISTINCT_CNT,
          true, allocator_, result_datum_buf_, datum_size_);
    }
    if (OB_SUCC(ret)) { capacity_ = candidate_capacity; }
  }
  return ret;
}


}
}
