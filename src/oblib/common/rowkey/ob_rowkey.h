/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/partition_bound_row.h"
#include "common/datum/datum_key_ops.h"
#include "lib/json/ob_yson.h"
namespace oceanbase { namespace common {
class ObStoreRowkey;
// Ordinary copies are native descriptor/payload views. Allocation is explicit
// in deep_copy/deserialize/transform; callers publish only after success.
class ObRowkey
{
public:
  ObRowkey() : values_() {}
  ObRowkey(DatumValue *values, int64_t count) : values_(values, count) {}
  void reset() { values_.reset(); }
  void destroy(ObIAllocator &owner) { values_.destroy(owner); }
  int64_t count() const { return values_.count(); }
  int64_t get_value_count() const { return count(); }
  int64_t length() const { return count(); }
  DatumValue *get_values() { return values_.get_cells(); }
  const DatumValue *get_values() const { return values_.get_cells(); }
  DatumValue &get_cell(int64_t index) { return values_.get_cell(index); }
  const DatumValue &get_cell(int64_t index) const { return values_.get_cell(index); }
  const DatumValue *ptr() const { return get_values(); }
  const PartitionBoundRow &values_view() const { return values_; }
  bool is_legal() const { return values_.is_legal(); }
  bool is_valid() const { return values_.is_valid(); }
  bool is_min_row() const { return values_.is_min_row(); }
  bool is_max_row() const { return values_.is_max_row(); }
  void set_min_row() { *this = MIN_ROWKEY; }
  void set_max_row() { *this = MAX_ROWKEY; }
  void set_length(int64_t count) { values_.count_ = count; }
  void assign(DatumValue *values, int64_t count) { values_.assign(values, count); }
  int copy_values(ObRowkey &output, DatumValue *buffer, int64_t capacity) const;
  int64_t get_deep_copy_size() const { return values_.get_deep_copy_size(); }
  template<class Allocator> int deep_copy(ObRowkey &output, Allocator &owner) const {
    int ret = OB_SUCCESS;
    const int64_t size = get_deep_copy_size();
    if (size == INT64_MAX) { ret = OB_SIZE_OVERFLOW; }
    else if (count() == 0) { output.reset(); }
    else {
      char *buffer = static_cast<char *>(owner.alloc(size));
      if (buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else if (OB_SUCCESS != (ret = deep_copy(output, buffer, size))) { owner.free(buffer); }
    }
    return ret;
  }
  int deep_copy(char *buffer, int64_t size) const;
  int deep_copy(ObRowkey &output, char *buffer, int64_t size) const;
  int deep_copy(const ObRowkey &source, char *buffer, int64_t size, int64_t &pos);
  int deserialize(ObIAllocator &owner, const char *buffer, int64_t size, int64_t &pos);
  int deserialize(const char *buffer, int64_t size, int64_t &pos, bool check_zero = false);
  // Decode into a caller-reserved descriptor array. Payloads belong to owner;
  // reset/reassign descriptors before owner.reset() invalidates earlier bytes.
  int deserialize(ObIAllocator &owner, const char *buffer, int64_t size, int64_t &pos,
                  bool check_zero) { return decode(owner,buffer,size,pos,check_zero); }

  int serialize(char *buffer, int64_t size, int64_t &pos) const;
  int64_t get_serialize_size() const;
  int serialize_datums(char *buffer, int64_t size, int64_t &pos) const;
  int64_t get_serialize_datums_size() const;
  int deserialize_datums(const char *buffer, int64_t size, int64_t &pos);
  int to_yson(char *buffer, int64_t size, int64_t &pos) const {
    ObCStringHelper helper;
    return oceanbase::yson::databuff_encode_elements(buffer,size,pos,::oceanbase::name::rowkey,helper.convert(*this));
  }
  int64_t to_string(char *buffer, int64_t size) const;
  int64_t to_smart_string(char *buffer, int64_t size) const;
  int64_t to_format_string(char *buffer, int64_t size) const;
  int64_t to_plain_string(char *buffer, int64_t size) const;
  int need_transform_to_collation_free(bool &required) const;
  template<class Allocator> int to_collation_free_rowkey(ObRowkey &output, Allocator &owner) const {
    ObRowkey candidate;
    int ret = deep_copy(candidate,owner);
    for (int64_t i=0; ret==OB_SUCCESS && i<count(); ++i) {
      if (get_cell(i).meta_.is_character_type()) {
        bool valid = true;
        ret = transform_key_sortkey(get_cell(i),candidate.get_cell(i),valid,owner);
        if (ret==OB_SUCCESS && !valid) { candidate.destroy(owner); candidate.reset(); break; }
      }
    }
    if (ret==OB_SUCCESS) { output=candidate; }
    return ret;
  }
  template<class Allocator> int to_collation_free_rowkey_on_demand(ObRowkey &output, Allocator &owner) const {
    bool required=false; int ret=need_transform_to_collation_free(required);
    if (ret==OB_SUCCESS && required) { ret=to_collation_free_rowkey(output,owner); }
    return ret;
  }
  int checksum(ObBatchChecksum &checksum) const;
  uint64_t murmurhash(uint64_t seed) const;
  uint64_t hash() const { return murmurhash(0); }
  int hash(uint64_t &value) const { value=hash();return OB_SUCCESS; }
  static int get_common_prefix_length(const ObRowkey &left,const ObRowkey &right,int64_t &length);
  int equal(const ObRowkey &right,bool &equal) const;
  bool simple_equal(const ObRowkey &right) const;
  int compare(const ObRowkey &right,int &result) const;
  int compare(const ObRowkey &right) const;
  int compare_prefix(const ObRowkey &right,int &result) const;
  int compare_prefix(const ObRowkey &right) const;
  int32_t fast_compare(const ObRowkey &right) const { return compare(right); }
  bool operator<(const ObRowkey &other) const { return compare(other)<0; }
  bool operator<=(const ObRowkey &other) const { return compare(other)<=0; }
  bool operator>(const ObRowkey &other) const { return compare(other)>0; }
  bool operator>=(const ObRowkey &other) const { return compare(other)>=0; }
  bool operator==(const ObRowkey &other) const { return compare(other)==0; }
  bool operator!=(const ObRowkey &other) const { return compare(other)!=0; }
  static DatumValue MIN_VALUE;
  static DatumValue MAX_VALUE;
  static ObRowkey MIN_ROWKEY;
  static ObRowkey MAX_ROWKEY;
private:
  int decode(ObIAllocator &owner,const char *buffer,int64_t size,int64_t &pos,bool check_zero);
  PartitionBoundRow values_;
};
template<class Allocator>
int ob_write_rowkey(Allocator &owner,const ObRowkey &source,ObRowkey &output)
{ return source.deep_copy(output,owner); }
} }
