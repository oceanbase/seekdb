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

#ifndef OCEANBASE_COMMON_OB_ROW_
#define OCEANBASE_COMMON_OB_ROW_

#include "lib/container/ob_array.h"
#include "lib/utility/ob_hang_fatal_error.h"
#include "lib/json/ob_yson.h"
#include "common/row/ob_row_desc.h"
#include "common/datum/datum_value_buffer.h"

namespace commontest
{
class ObRowTest_reset_test_Test;
}

namespace oceanbase
{
namespace common
{

class ObColumnInfo
{
public:
  int64_t index_;
  common::ObCollationType cs_type_;
  ObColumnInfo() : index_(common::OB_INVALID_INDEX), cs_type_(common::CS_TYPE_INVALID) {}
  virtual ~ObColumnInfo() {}
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV2(N_INDEX_ID, index_,
                N_COLLATION_TYPE, common::ObCharset::collation_name(cs_type_));
};

/**
 * @brief: Borrow a row of typed native DatumValue descriptors and its physical size
 */
class ObNewRow final
{
public:
  ObNewRow()
      :cells_(NULL),
       count_(0),
       projector_size_(0),
       projector_(NULL)
  {}
  ObNewRow(DatumValue *cells, int64_t count)
      :cells_(cells),
       count_(count),
       projector_size_(0),
       projector_(NULL)
  {}

  void reset();
  OB_INLINE bool is_invalid() const
  {
    return NULL == cells_ || count_ <= 0 || (projector_size_ > 0 && NULL == projector_);
  }

  OB_INLINE bool is_valid() const
  {
    return !is_invalid();
  }

  OB_INLINE void assign(DatumValue *ptr, const int64_t count)
  {
    cells_ = ptr;
    count_ = count;
  }
  int64_t get_deep_copy_size() const;
  int deep_copy(const ObNewRow &src, char *buf, int64_t len, int64_t &pos);
  //According to the existing buffer to construct an ObNewRow, the memory format is:
  //The memory structure allocated by sizeof(ObNewRow) + ObNewRow.deep_copy()
  static int construct(char *buf, int64_t len, int64_t &pos, ObNewRow *&row);
  int serialize(char *buf, int64_t buf_len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  // count_ and projector_size_ are the caller-reserved capacities on entry.
  // Payload ownership is explicit; successful decode retains both pointers.
  int deserialize(ObIAllocator &owner, const char *buf, int64_t data_len, int64_t &pos);
  int clone(ObIAllocator &owner, ObNewRow &output) const;
  int64_t get_projected_deep_copy_size() const;
  int deep_copy_projected(const ObNewRow &source, char *buf, int64_t len, int64_t &pos);


  bool operator==(const ObNewRow &other) const;
  OB_INLINE int64_t get_count() const { return projector_size_ > 0 ? projector_size_ : count_; }
  const DatumValue &get_cell(int64_t index) const
  {
    int64_t real_idx = index;
    if (projector_size_ > 0) {
      if (OB_ISNULL(projector_) || index >= projector_size_ || index < 0) {
        COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "index is invalid", K(index), K_(projector_size), K_(projector));
      } else {
        real_idx = projector_[index];
      }
    }
    if (OB_UNLIKELY(real_idx >= count_)) {
      COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "real_idx is invalid", K_(count), K(real_idx));
    }
    return cells_[real_idx];
  }

  DatumValue &get_cell(int64_t index)
  {
    int64_t real_idx = index;
    if (projector_size_ > 0) {
      if (OB_ISNULL(projector_) || index >= projector_size_ || index < 0) {
        COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "index is invalid", K(index), K_(projector_size), K_(projector));
        right_to_die_or_duty_to_live();
      } else {
        real_idx = projector_[index];
      }
    }
    if (real_idx >= count_) {
      COMMON_LOG_RET(ERROR, common::OB_INVALID_ARGUMENT, "real_idx is invalid", K_(count), K(real_idx));
      right_to_die_or_duty_to_live();
    }
    return cells_[real_idx];
  }

  int32_t get_project_idx(int64_t cell_idx) const
  {
    int32_t proj_idx = -1;
    if (0 <= cell_idx && cell_idx < count_) {
      if (OB_ISNULL(projector_)) {
        proj_idx = static_cast<int32_t>(cell_idx);
      } else {
        for (int32_t i = 0; i < projector_size_; i++) {
          if (projector_[i] == cell_idx) {
            proj_idx = i;
            break;
          }
        }
      }
    }
    return proj_idx;
  }

  int to_yson(char *buf, const int64_t buf_len, int64_t &pos) const
  {
    ObCStringHelper helper;
    return oceanbase::yson::databuff_encode_elements(buf, buf_len, pos,
        ::oceanbase::name::row, helper.convert(*this));
  }
  int64_t to_string(char* buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_NAME(N_ROW);
    J_COLON();
    (void)databuff_print_obj_array(buf, buf_len, pos, cells_, count_);
    J_COMMA();
    J_NAME(N_PROJECTOR);
    J_COLON();
    (void)databuff_print_obj_array(buf, buf_len, pos, projector_, projector_size_);
    J_OBJ_END();
    return pos;
  }
public:
  DatumValue *cells_;
  int64_t count_;  // cells count
  int64_t projector_size_;
  int32_t *projector_;  // if projector is not NULL, the caller should use this projector to access cells_
};

template<typename AllocatorT>
int ob_write_row(AllocatorT &allocator, const ObNewRow &src, ObNewRow &dst)
{
  int ret = OB_SUCCESS;
  if (src.count_ <= 0) {
    dst.count_ = src.count_; dst.cells_ = nullptr; dst.projector_size_ = 0; dst.projector_ = nullptr;
  } else {
    const int64_t bytes = src.get_deep_copy_size();
    char *memory = nullptr;
    if (bytes == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
    else if ((memory = static_cast<char *>(allocator.alloc(bytes))) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      ObNewRow candidate; int64_t pos = 0;
      if (OB_FAIL(candidate.deep_copy(src, memory, bytes, pos))) { allocator.free(memory); }
      else { dst = candidate; }
    }
  }
  return ret;
}

template<typename AllocatorT>
int ob_write_row_by_projector(AllocatorT &allocator, const ObNewRow &src, ObNewRow &dst)
{
  int ret = OB_SUCCESS;
  if (src.count_ <= 0) { dst.reset(); }
  else {
    const int64_t bytes = src.get_projected_deep_copy_size();
    char *memory = nullptr;
    if (bytes == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
    else if ((memory = static_cast<char *>(allocator.alloc(bytes))) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      ObNewRow candidate; int64_t pos = 0;
      if (OB_FAIL(candidate.deep_copy_projected(src, memory, bytes, pos))) { allocator.free(memory); }
      else { dst = candidate; }
    }
  }
  return ret;
}

template<typename AllocatorT>
int ob_create_row(AllocatorT &allocator, int64_t col_count, ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  const int64_t cells_size = DatumValueBuffer::allocation_size(col_count);
  if (col_count <= 0) { ret = OB_INVALID_ARGUMENT; }
  else if (cells_size > INT64_MAX - sizeof(ObNewRow)) { ret = OB_SIZE_OVERFLOW; }
  else {
    char *memory = static_cast<char *>(allocator.alloc(sizeof(ObNewRow) + cells_size));
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      ObNewRow *candidate = new (memory) ObNewRow();
      DatumValueBuffer::initialize(memory + sizeof(ObNewRow), col_count, candidate->cells_);
      candidate->count_ = col_count; row = candidate;
    }
  }
  return ret;
}

template<typename AllocatorT>
int ob_create_row(AllocatorT &allocator, int64_t col_count, ObNewRow &row)
{
  int ret = OB_SUCCESS;
  row.reset();
  const int64_t bytes = DatumValueBuffer::allocation_size(col_count);
  if (col_count <= 0) { ret = OB_INVALID_ARGUMENT; }
  else if (bytes == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
  else {
    void *memory = allocator.alloc(bytes);
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else { DatumValueBuffer::initialize(memory, col_count, row.cells_); row.count_ = col_count; }
  }
  return ret;
}

template<typename AllocatorT>
int ob_create_rows(AllocatorT &allocator, int64_t row_count, int64_t col_count, ObNewRow *&rows)
{
  int ret = OB_SUCCESS;
  const int64_t cells_size = DatumValueBuffer::allocation_size(col_count);
  if (col_count <= 0 || row_count <= 0) { ret = OB_INVALID_ARGUMENT; }
  else if (cells_size > INT64_MAX - sizeof(ObNewRow)
      || row_count > INT64_MAX / (sizeof(ObNewRow) + cells_size)) { ret = OB_SIZE_OVERFLOW; }
  else {
    const int64_t total = (sizeof(ObNewRow) + cells_size) * row_count;
    char *memory = static_cast<char *>(allocator.alloc(total));
    if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
    else {
      char *cells = memory + sizeof(ObNewRow) * row_count;
      for (int64_t i = 0; i < row_count; ++i) {
        ObNewRow *candidate = new (memory + sizeof(ObNewRow) * i) ObNewRow();
        DatumValueBuffer::initialize(cells, col_count, candidate->cells_);
        candidate->count_ = col_count; cells += cells_size;
      }
      rows = reinterpret_cast<ObNewRow *>(memory);
    }
  }
  return ret;
}

static_assert(sizeof(ObNewRow) == 32, "Native row views retain their established layout");
} // end namespace common
} // end namespace oceanbase
#endif /* OCEANBASE_COMMON_OB_ROW_ */
