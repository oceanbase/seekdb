/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/datum/partition_bound_row.h"
#include "common/datum/datum_parameter_codec.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
namespace oceanbase { namespace common {
namespace {
class BoundBufferAllocator final : public ObIAllocator
{
public:
  BoundBufferAllocator(char *buf, int64_t len, int64_t &pos) : buf_(buf), len_(len), pos_(pos) {}
  void *alloc(int64_t size) override
  {
    void *result = nullptr;
    if (buf_ == nullptr || size < 0 || pos_ < 0 || pos_ > len_) { return nullptr; }
    const int64_t padding = (8 - (reinterpret_cast<uintptr_t>(buf_ + pos_) & 7)) & 7;
    if (padding <= len_ - pos_ && size <= len_ - pos_ - padding) {
      pos_ += padding; result = buf_ + pos_; pos_ += size;
    }
    return result;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}
private:
  char *buf_;
  int64_t len_;
  int64_t &pos_;
};
bool valid_bound_payload(const DatumValue &value)
{
  const SqlValueType type = value.get_type();
  if (type < ObNullType || type >= ObMaxType) { return false; }
  if (value.is_null()) { return true; }
  if (value.datum_.ptr_ == nullptr && value.datum_.len_ > 0) { return false; }
  if (value.is_ext_sql_array()) { return value.datum_.ptr_ != nullptr && value.datum_.len_ == sizeof(int64_t); }
  switch (ObDatum::get_sql_value_datum_map_type(type)) {
    case OBJ_DATUM_8BYTE_DATA: return value.datum_.ptr_ != nullptr && value.datum_.len_ == 8;
    case OBJ_DATUM_4BYTE_DATA: return value.datum_.ptr_ != nullptr && value.datum_.len_ == 4;
    case OBJ_DATUM_1BYTE_DATA: return value.datum_.ptr_ != nullptr && value.datum_.len_ == 1;
    case OBJ_DATUM_EXTEND: return value.datum_.ptr_ != nullptr && value.datum_.has_ext_value();
    case OBJ_DATUM_NUMBER:
      return value.datum_.ptr_ != nullptr && value.datum_.len_ >= sizeof(number::ObNumber::Desc)
          && value.datum_.len_ == sizeof(number::ObNumber::Desc) + sizeof(uint32_t) * value.datum_.get_number_desc().len_;
    case OBJ_DATUM_DECIMALINT:
      return value.datum_.len_ == 0 || value.datum_.len_ == 4 || value.datum_.len_ == 8
          || value.datum_.len_ == 16 || value.datum_.len_ == 32 || value.datum_.len_ == 64;
    default: return true;
  }
}
int64_t bound_payload_size(const DatumValue &value, int depth = 0)
{
  int64_t size = 0;
  if (depth > 64 || !valid_bound_payload(value)) { size = INT64_MAX; }
  else if (!value.is_null()) {
    size = MAX(1, value.datum_.len_) + 7;
    if (value.is_ext_sql_array()) {
      const DatumParameterArray *array = reinterpret_cast<const DatumParameterArray *>(value.datum_.get_int());
      if (array == nullptr || array->count_ < 0 || array->count_ > (INT64_MAX - sizeof(DatumParameterArray) - 7) / sizeof(DatumParameter)
          || (array->count_ > 0 && array->data_ == nullptr)) { size = INT64_MAX; }
      else {
        const int64_t descriptor = sizeof(DatumParameterArray) + array->count_ * sizeof(DatumParameter) + 7;
        if (descriptor > INT64_MAX - size) { size = INT64_MAX; }
        else { size += descriptor; }
        for (int64_t i = 0; i < array->count_ && size != INT64_MAX; ++i) {
          const int64_t child = bound_payload_size(array->data_[i].value_view(), depth + 1);
          if (child > INT64_MAX - size) { size = INT64_MAX; }
          else { size += child; }
        }
      }
    }
  }
  return size;
}
DatumValue make_bound_marker(bool maximum)
{
  DatumValue value;
  if (maximum) { value.set_max_value(); } else { value.set_min_value(); }
  return value;
}
DatumValue native_min_bound = make_bound_marker(false);
DatumValue native_max_bound = make_bound_marker(true);
}
const PartitionBoundRow PartitionBoundRow::MIN_ROW(&native_min_bound, 1);
const PartitionBoundRow PartitionBoundRow::MAX_ROW(&native_max_bound, 1);

bool PartitionBoundRow::is_legal() const
{
  bool valid = count_ >= 0 && (count_ == 0 || cells_ != nullptr)
      && projector_size_ >= 0 && (projector_size_ == 0 || projector_ != nullptr);
  for (int64_t i = 0; valid && i < projector_size_; ++i) {
    valid = projector_[i] >= 0 && projector_[i] < count_;
  }
  return valid;
}
bool PartitionBoundRow::is_min_row() const
{
  bool result = is_valid();
  for (int64_t i = 0; result && i < count_; ++i) { result = cells_[i].is_min_value(); }
  return result;
}
bool PartitionBoundRow::is_max_row() const
{
  bool result = is_valid();
  for (int64_t i = 0; result && i < count_; ++i) { result = cells_[i].is_max_value(); }
  return result;
}
int64_t PartitionBoundRow::get_deep_copy_size() const
{
  int64_t size = 7;
  if (!is_legal() || count_ > (INT64_MAX - size) / sizeof(DatumValue)) { size = INT64_MAX; }
  else { size += count_ * sizeof(DatumValue); }
  for (int64_t i = 0; i < count_ && size != INT64_MAX; ++i) {
    const int64_t bytes = bound_payload_size(cells_[i]);
    if (bytes > INT64_MAX - size) { size = INT64_MAX; } else { size += bytes; }
  }
  if (size != INT64_MAX) {
    if (projector_size_ > (INT64_MAX - size - 7) / sizeof(int32_t)) { size = INT64_MAX; }
    else { size += projector_size_ * sizeof(int32_t) + 7; }
  }
  return size;
}
int PartitionBoundRow::deep_copy(const PartitionBoundRow &source, char *buf, int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t next = pos;
  PartitionBoundRow candidate;
  BoundBufferAllocator allocator(buf, len, next);
  if (buf == nullptr || pos < 0 || pos > len || !source.is_legal()) { ret = OB_INVALID_ARGUMENT; }
  else if (source.count_ > INT64_MAX / sizeof(DatumValue)) { ret = OB_SIZE_OVERFLOW; }
  else if (source.count_ > 0) {
    void *memory = allocator.alloc(source.count_ * sizeof(DatumValue));
    if (memory == nullptr) { ret = OB_BUF_NOT_ENOUGH; }
    else { candidate.cells_ = new (memory) DatumValue[source.count_]; candidate.count_ = source.count_; }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < source.count_; ++i) {
    DatumParameter input, owned;
    input.datum_ = source.cells_[i].datum_; input.set_type_meta(source.cells_[i].meta_);
    ret = bound_payload_size(source.cells_[i]) == INT64_MAX ? OB_INVALID_ARGUMENT
        : DatumParameterArray::clone_parameter(input, owned, allocator);
    if (OB_SUCC(ret)) { candidate.cells_[i] = owned.value_view(); }
  }
  if (OB_SUCC(ret) && source.projector_size_ > 0) {
    void *memory = allocator.alloc(source.projector_size_ * sizeof(int32_t));
    if (memory == nullptr) { ret = OB_BUF_NOT_ENOUGH; }
    else {
      candidate.projector_ = static_cast<int32_t *>(memory);
      candidate.projector_size_ = source.projector_size_;
      MEMCPY(candidate.projector_, source.projector_, source.projector_size_ * sizeof(int32_t));
    }
  }
  if (OB_SUCC(ret)) { *this = candidate; pos = next; }
  return ret;
}
int PartitionBoundRow::clone(PartitionBoundRow &output, ObIAllocator &owner) const
{
  int ret = OB_SUCCESS;
  const int64_t size = get_deep_copy_size();
  char *memory = nullptr;
  if (!is_legal()) { ret = OB_INVALID_ARGUMENT; }
  else if (count_ == 0 && projector_size_ == 0) { output.reset(); }
  else if (size == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
  else if ((memory = static_cast<char *>(owner.alloc(size))) == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  else {
    int64_t pos = 0;
    if (OB_FAIL(output.deep_copy(*this, memory, size, pos))) { owner.free(memory); }
  }
  return ret;
}
void PartitionBoundRow::destroy(ObIAllocator &owner)
{
  if (cells_ != nullptr) { owner.free(cells_); }
  reset();
}
int PartitionBoundRow::serialize(char *buf, int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (buf == nullptr || len <= 0) { ret = OB_INVALID_ARGUMENT; }
  else if (!is_legal()) { ret = OB_INVALID_DATA; }
  else { ret = serialization::encode_vi64(buf, len, pos, count_); }
  for (int64_t i = 0; OB_SUCC(ret) && i < count_; ++i) {
    ret = DatumParameterCodec::encode_value(cells_[i].datum_, cells_[i].meta_, buf, len, pos);
  }
  return ret;
}
int64_t PartitionBoundRow::get_serialize_size() const
{
  int64_t size = serialization::encoded_length_vi64(count_);
  for (int64_t i = 0; i < count_; ++i) {
    int64_t bytes = 0;
    if (DatumParameterCodec::value_size(cells_[i].datum_, cells_[i].meta_, bytes) != OB_SUCCESS) { return 0; }
    size += bytes;
  }
  return size;
}
int PartitionBoundRow::serialize_row(char *buf, int64_t len, int64_t &pos) const
{
  int ret = serialize(buf, len, pos);
  if (OB_SUCC(ret)) { ret = serialization::encode_vi64(buf, len, pos, projector_size_); }
  for (int64_t i = 0; OB_SUCC(ret) && i < projector_size_; ++i) {
    ret = serialization::encode_vi32(buf, len, pos, projector_[i]);
  }
  return ret;
}
int64_t PartitionBoundRow::get_row_serialize_size() const
{
  int64_t size = get_serialize_size() + serialization::encoded_length_vi64(projector_size_);
  for (int64_t i = 0; i < projector_size_; ++i) { size += serialization::encoded_length_vi32(projector_[i]); }
  return size;
}
int PartitionBoundRow::decode(ObIAllocator &owner, const char *buf, int64_t len, int64_t &pos, bool row_wire)
{
  int ret = OB_SUCCESS;
  int64_t next = pos, count = 0, projector_count = 0;
  ObArenaAllocator scratch("PartBoundDec");
  ObSEArray<DatumValue, 4> cells;
  PartitionBoundRow decoded, candidate;
  if (buf == nullptr || len <= 0 || pos < 0 || (!row_wire && pos > len)) { ret = OB_INVALID_ARGUMENT; }
  else if ((!row_wire || next < len) && OB_FAIL(serialization::decode_vi64(buf, len, next, &count))) {}
  else if (count < 0) { ret = OB_INVALID_DATA; }
  else if (count > (row_wire ? 1024 : OB_MAX_ROWKEY_COLUMN_NUMBER)) { ret = row_wire ? OB_ERR_SYS : OB_BUF_NOT_ENOUGH; }
  else if (OB_FAIL(cells.prepare_allocate(count))) {}
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    // An entirely omitted legacy value retains both the payload and metadata
    // of its scratch slot. Once any metadata byte is present, its value
    // deserializer starts from zero while omitted metadata fields are retained.
    if (row_wire && i < count_) { cells.at(i) = cells_[i]; }
    if (next < len) {
      ret = DatumParameterCodec::decode_unis_value(buf, len, next, scratch, cells.at(i).datum_, cells.at(i).meta_);
    }
  }
  if (OB_SUCC(ret) && row_wire) {
    if (next < len) { ret = serialization::decode_vi64(buf, len, next, &projector_count); }
    // The persisted LIST reader historically reserves no projector slots.
    if (OB_SUCC(ret) && projector_count != 0) { ret = OB_ERR_SYS; }
  }
  if (OB_SUCC(ret)) {
    decoded.assign(cells.get_data(), cells.count());
    ret = decoded.clone(candidate, owner);
  }
  if (OB_SUCC(ret)) { *this = candidate; pos = next; }
  return ret;
}
int PartitionBoundRow::deserialize(ObIAllocator &owner, const char *buf, int64_t len, int64_t &pos)
{ return decode(owner, buf, len, pos, false); }
int PartitionBoundRow::deserialize_row(ObIAllocator &owner, const char *buf, int64_t len, int64_t &pos)
{ return decode(owner, buf, len, pos, true); }

} }
