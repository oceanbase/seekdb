/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include "common/datum/datum_value.h"
#include "lib/container/ob_array_helper.h"
#include "lib/container/ob_array_wrap.h"

namespace oceanbase
{
namespace common {

// A row of typed native values. Ordinary copies borrow descriptors and bytes.
// Schema setters and persistent records explicitly clone into their own owner.
// key wire is count + value frames; row wire additionally retains the projector.
class PartitionBoundRow
{
public:
  PartitionBoundRow() : cells_(nullptr), count_(0), projector_(nullptr), projector_size_(0) {}
  PartitionBoundRow(common::DatumValue *cells, int64_t count)
    : cells_(cells), count_(count), projector_(nullptr), projector_size_(0) {}
  void reset() { *this = PartitionBoundRow(); }
  void assign(common::DatumValue *cells, int64_t count) { reset(); cells_ = cells; count_ = count; }
  int64_t count() const { return count_; }
  int64_t length() const { return count_; }
  int64_t get_count() const { return projector_size_ > 0 ? projector_size_ : count_; }
  common::DatumValue *get_cells() { return cells_; }
  const common::DatumValue *get_cells() const { return cells_; }
  const common::DatumValue &get_cell(int64_t index) const
  { return cells_[projector_size_ > 0 ? projector_[index] : index]; }
  common::DatumValue &get_cell(int64_t index)
  { return cells_[projector_size_ > 0 ? projector_[index] : index]; }
  common::ObArrayHelper<common::DatumValue> values_view() const
  { return common::ObArrayHelper<common::DatumValue>(count_, cells_, count_); }
  bool is_legal() const;
  bool is_valid() const { return cells_ != nullptr && count_ > 0 && is_legal(); }
  bool is_invalid() const { return !is_valid(); }
  bool is_min_row() const;
  bool is_max_row() const;
  void set_min_row() { *this = MIN_ROW; }
  void set_max_row() { *this = MAX_ROW; }

  // All payload allocations are kept in one block, including nested SQL arrays.
  // destroy() is only for the result of clone(), never a borrowed/buffer copy.
  int clone(PartitionBoundRow &output, common::ObIAllocator &owner) const;
  void destroy(common::ObIAllocator &owner);
  int64_t get_deep_copy_size() const;
  int deep_copy(const PartitionBoundRow &source, char *buf, int64_t len, int64_t &pos);

  int serialize(char *buf, int64_t len, int64_t &pos) const;
  int64_t get_serialize_size() const;
  int deserialize(common::ObIAllocator &owner, const char *buf, int64_t len, int64_t &pos);
  int serialize_row(char *buf, int64_t len, int64_t &pos) const;
  int64_t get_row_serialize_size() const;
  int deserialize_row(common::ObIAllocator &owner, const char *buf, int64_t len, int64_t &pos);

  TO_STRING_KV("values", common::ObArrayWrap<common::DatumValue>(cells_, count_),
               "projector", common::ObArrayWrap<int32_t>(projector_, projector_size_));
  static const PartitionBoundRow MIN_ROW;
  static const PartitionBoundRow MAX_ROW;

  common::DatumValue *cells_;
  int64_t count_;
  int32_t *projector_;
  int64_t projector_size_;
private:
  int decode(common::ObIAllocator &owner, const char *buf, int64_t len, int64_t &pos, bool row_wire);
};

} }
