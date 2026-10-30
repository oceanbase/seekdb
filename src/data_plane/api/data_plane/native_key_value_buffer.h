/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once
#include "common/datum/datum_value.h"
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "lib/allocator/ob_allocator.h"
namespace oceanbase { namespace storage {
// Reusable key descriptors plus their small scalar payloads. No allocation is
// performed per value. Large payloads borrow the source row; keys retained
// beyond that source row must use ObRowkey::deep_copy into their real owner.
class NativeKeyValueBuffer final
{
public:
  NativeKeyValueBuffer() : owner_(nullptr), values_(nullptr), payloads_(nullptr), capacity_(0) {}
  ~NativeKeyValueBuffer() { reset(); }
  int init(common::ObIAllocator *owner) {
    int ret=common::OB_SUCCESS;
    if(owner_!=nullptr)ret=common::OB_INIT_TWICE;
    else if(owner==nullptr)ret=common::OB_INVALID_ARGUMENT;
    else {owner_=owner;values_=local_values_;payloads_=local_payloads_;capacity_=LOCAL_COUNT;}
    return ret;
  }
  bool is_inited() const { return owner_!=nullptr; }
  int64_t get_count() const { return capacity_; }
  common::DatumValue *get_data() { return values_; }
  common::DatumValue &at(int64_t index) { OB_ASSERT(index>=0&&index<capacity_);return values_[index]; }
  int reserve(int64_t count) {
    int ret=common::OB_SUCCESS;
    if(owner_==nullptr)ret=common::OB_NOT_INIT;
    else if(count<0||count>INT64_MAX/(sizeof(common::DatumValue)+PAYLOAD_BYTES))ret=common::OB_SIZE_OVERFLOW;
    else if(count>capacity_){
      const int64_t size=count*(sizeof(common::DatumValue)+PAYLOAD_BYTES);
      common::DatumValue *values=static_cast<common::DatumValue *>(owner_->alloc(size));
      if(values==nullptr)ret=common::OB_ALLOCATE_MEMORY_FAILED;
      else {
        new(values) common::DatumValue[count];
        if(values_!=local_values_)owner_->free(values_);
        values_=values;payloads_=reinterpret_cast<char (*)[PAYLOAD_BYTES]>(values+count);capacity_=count;
      }
    }
    return ret;
  }
  // Descriptor/fixed-byte ownership belongs to owner. Large source payloads
  // remain borrowed exactly as in the storage view API; callers can clone the
  // resulting native rowkey when retaining it beyond that payload producer.
  static int allocate_values(common::ObIAllocator &owner,int64_t count,common::DatumValue *&output) {
    int ret=common::OB_SUCCESS;
    if(count<0||count>INT64_MAX/(sizeof(common::DatumValue)+PAYLOAD_BYTES))ret=common::OB_SIZE_OVERFLOW;
    else {
      auto *values=static_cast<common::DatumValue *>(owner.alloc(count*(sizeof(common::DatumValue)+PAYLOAD_BYTES)));
      if(values==nullptr)ret=common::OB_ALLOCATE_MEMORY_FAILED;
      else {new(values) common::DatumValue[count];output=values;}
    }
    return ret;
  }
  static int project_value(const common::DatumValue &source, common::DatumValue *values,
                           int64_t count, int64_t index) {
    int ret=common::OB_SUCCESS;common::DatumValue candidate=source;
    if(values==nullptr||index<0||index>=count)ret=common::OB_INVALID_ARGUMENT;
    else if(source.datum_.len_>0&&source.datum_.ptr_==nullptr)ret=common::OB_INVALID_ARGUMENT;
    else {
      if(!candidate.is_null()&&candidate.datum_.len_>0&&candidate.datum_.len_<=PAYLOAD_BYTES){
        char *payload=reinterpret_cast<char *>(values+count)+index*PAYLOAD_BYTES;
        MEMMOVE(payload,candidate.datum_.ptr_,candidate.datum_.len_);candidate.datum_.ptr_=payload;
      }
      values[index]=candidate;
    }
    return ret;
  }
  static int project_storage(const blocksstable::ObStorageDatum &value,const common::SqlTypeMeta &meta,
                              common::DatumValue *values,int64_t count,int64_t index) {
    int ret=common::OB_SUCCESS;common::DatumValue candidate;
    if(values==nullptr||index<0||index>=count)ret=common::OB_INVALID_ARGUMENT;
    else if((candidate=values[index],ret=value.to_value_view(meta,candidate))==common::OB_SUCCESS){
      if(!candidate.is_null()&&candidate.datum_.len_>0&&candidate.datum_.len_<=PAYLOAD_BYTES){
        char *payload=reinterpret_cast<char *>(values+count)+index*PAYLOAD_BYTES;
        MEMMOVE(payload,candidate.datum_.ptr_,candidate.datum_.len_);candidate.datum_.ptr_=payload;
      }
      values[index]=candidate;
    }
    return ret;
  }
  int assign_value(int64_t index,const common::DatumValue &value) {
    int ret=common::OB_SUCCESS;
    if(owner_==nullptr)ret=common::OB_NOT_INIT;
    else if(index<0||index>=capacity_)ret=common::OB_INVALID_ARGUMENT;
    else if(value.datum_.len_>0&&value.datum_.ptr_==nullptr)ret=common::OB_INVALID_ARGUMENT;
    else {
      common::DatumValue candidate=value;
      if(!value.is_null()&&value.datum_.len_>0&&value.datum_.len_<=PAYLOAD_BYTES){
        MEMMOVE(payloads_[index],value.datum_.ptr_,value.datum_.len_);candidate.datum_.ptr_=payloads_[index];
      }
      values_[index]=candidate;
    }
    return ret;
  }
  int assign_storage(int64_t index,const blocksstable::ObStorageDatum &value,const common::SqlTypeMeta &meta) {
    int ret=owner_==nullptr?common::OB_NOT_INIT:index<0||index>=capacity_?common::OB_INVALID_ARGUMENT:common::OB_SUCCESS;
    common::DatumValue candidate;
    if(ret==common::OB_SUCCESS){candidate=values_[index];ret=value.to_value_view(meta,candidate);}
    if(ret==common::OB_SUCCESS)ret=assign_value(index,candidate);
    return ret;
  }
  void reset() {
    if(owner_!=nullptr&&values_!=local_values_)owner_->free(values_);
    owner_=nullptr;values_=nullptr;payloads_=nullptr;capacity_=0;
  }
private:
  NativeKeyValueBuffer(const NativeKeyValueBuffer &)=delete;
  NativeKeyValueBuffer &operator=(const NativeKeyValueBuffer &)=delete;
  static const int64_t LOCAL_COUNT=64;
  static const int64_t PAYLOAD_BYTES=16;
  common::ObIAllocator *owner_;
  common::DatumValue *values_;
  char (*payloads_)[PAYLOAD_BYTES];
  int64_t capacity_;
  common::DatumValue local_values_[LOCAL_COUNT];
  alignas(8) char local_payloads_[LOCAL_COUNT][PAYLOAD_BYTES];
};
} }
