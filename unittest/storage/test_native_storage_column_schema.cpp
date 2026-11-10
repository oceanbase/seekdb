/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include "storage/ob_storage_column_schema.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native storage record tests must not include the old value definition"
#endif
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::storage;
namespace {
class StorageSchemaTestAllocator : public ObIAllocator
{
public:
  std::set<void *> live;
  int64_t calls=0,fail_at=-1;
  ~StorageSchemaTestAllocator() {
    EXPECT_TRUE(live.empty());
    for(void *ptr:live) { std::free(ptr); }
  }
  void *alloc(int64_t n) override {
    ++calls;
    if(calls==fail_at)return nullptr;
    void *p=std::malloc(n>0?n:1);
    if(p!=nullptr)live.insert(p);
    return p;
  }
  void *alloc(int64_t n,const ObMemAttr &) override {return alloc(n);}
  void free(void *p) override {
    if(p!=nullptr) { ASSERT_EQ(1,live.erase(p));std::free(p); }
  }
};
std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> out;std::istringstream in(line);std::string f;
  while(std::getline(in,f,'\t'))out.push_back(f);
  return out;
}
std::string unhex(const std::string &s) {
  std::string out;
  for(size_t i=0;i<s.size();i+=2)out+=static_cast<char>(std::stoul(s.substr(i,2),nullptr,16));
  return out;
}
std::string wire(const ObStorageColumnSchema &value) {
  std::string result(value.get_serialize_size(),'\0');int64_t pos=0;
  EXPECT_EQ(OB_SUCCESS,value.serialize(&result[0],result.size(),pos));
  EXPECT_EQ(result.size(),pos);
  return result;
}
DatumValue read_value(const std::string &bytes,ObIAllocator &owner) {
  DatumValue value;int64_t pos=0;
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(bytes.data(),bytes.size(),pos,owner,value.datum_,value.meta_));
  EXPECT_EQ(bytes.size(),pos);return value;
}
void put_old_value(ObStorageColumnSchema &column,StorageSchemaTestAllocator &owner) {
  ObArenaAllocator input;DatumValue value;
  ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString::make_string("unchanged"),input));
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,value));
  column.info_=7;column.meta_type_.set_int();column.default_checksum_=918;
}
DatumValue array_value(ObIAllocator &owner,int count,bool reference_leaves=false) {
  auto *array=DatumParameterArray::alloc(owner,count);
  EXPECT_NE(nullptr,array);DatumParameter value;
  if(array==nullptr)return value.value_view();
  for(int i=0;i<count;++i) {
    if(reference_leaves) {
      EXPECT_EQ(OB_SUCCESS,array->data_[i].set_varchar(ObString("owned array leaf"),owner));
      array->data_[i].set_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else { EXPECT_EQ(OB_SUCCESS,array->data_[i].set_int(i+100,owner)); }
  }
  SqlTypeMeta meta;meta.set_ext();meta.set_extend_type(T_EXT_SQL_ARRAY);
  value.set_type_meta(meta);EXPECT_EQ(OB_SUCCESS,value.set_array_address(array,owner));
  return value.value_view();
}
}

TEST(NativeStorageColumnSchema, all_original_full_records_preserve_wire_and_fields) {
  std::ifstream in("unittest/storage/data/storage_column_schema.golden");ASSERT_TRUE(in.good());
  std::string line;int checked=0;
  while(std::getline(in,line)) {
    if(line.compare(0,2,"C\t")!=0)continue;
    const auto f=split(line);ASSERT_EQ(5,f.size());const int id=std::stoi(f[1]),flags=std::stoi(f[2]);
    SCOPED_TRACE(f[1]);
    SCOPED_TRACE(f[2]);
    ObArenaAllocator source;const DatumValue value=read_value(unhex(f[3]),source);
    StorageSchemaTestAllocator owner;ObStorageColumnSchema column;
    column.info_=flags;column.default_checksum_=flags%2?INT64_MIN+id:INT64_MAX-id;column.meta_type_=value.get_meta();
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,value));
    EXPECT_EQ(unhex(f[4]),wire(column));
    ObStorageColumnSchema decoded;const std::string bytes=unhex(f[4]);int64_t pos=0;
    ASSERT_EQ(OB_SUCCESS,decoded.deserialize(owner,bytes.data(),bytes.size(),pos));
    EXPECT_EQ(bytes.size(),pos);EXPECT_EQ(flags,decoded.info_);
    EXPECT_EQ(column.default_checksum_,decoded.default_checksum_);EXPECT_EQ(bytes,wire(decoded));
    decoded.destroy(owner);column.destroy(owner);EXPECT_TRUE(owner.live.empty());++checked;
  }
  EXPECT_EQ(888,checked);
}

TEST(NativeStorageColumnSchema, old_short_records_and_transactional_failure) {
  std::ifstream in("unittest/storage/data/storage_column_schema.golden");ASSERT_TRUE(in.good());
  std::string line;int checked=0,unsafe=0;
  while(std::getline(in,line)) {
    if(line.compare(0,2,"D\t")!=0)continue;
    const auto f=split(line);ASSERT_EQ(6,f.size());SCOPED_TRACE(f[1]);
    SCOPED_TRACE(f[2]);
    const std::string bytes=unhex(f[4]);StorageSchemaTestAllocator owner;ObStorageColumnSchema column;put_old_value(column,owner);
    const std::string before=wire(column);int64_t pos=0;
    const int result=column.deserialize(owner,bytes.data(),bytes.size(),pos);
    if(f[5]=="UNSAFE_SIZE") {
      // The old decoder accepted a state whose size function returned a
      // negative error code. It has no valid normalized record to reproduce.
      ++unsafe;
    } else {
      EXPECT_EQ(std::stoi(f[3]),result);
      if(result==OB_SUCCESS) {EXPECT_EQ(unhex(f[5]),wire(column));}
    }
    if(result!=OB_SUCCESS) {EXPECT_EQ(before,wire(column));EXPECT_EQ(1,owner.live.size());}
    column.destroy(owner);EXPECT_TRUE(owner.live.empty());++checked;
  }
  EXPECT_EQ(3107,checked);EXPECT_EQ(16,unsafe);
}

TEST(NativeStorageColumnSchema, source_lifetime_and_repeated_self_replacement) {
  StorageSchemaTestAllocator owner;ObStorageColumnSchema column;
  {
    ObArenaAllocator source;DatumValue value;
    ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString::make_string("independent payload"),source));
    value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,value));
  }
  EXPECT_EQ("independent payload",std::string(column.get_orig_default_datum().get_string().ptr(),column.get_orig_default_datum().get_string().length()));
  for(int i=0;i<100;++i) {
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_datum(owner,column.get_orig_default_datum(),column.get_orig_default_meta()));
    EXPECT_EQ(1,owner.live.size());
  }
  column.info_=7;column.default_checksum_=918;column.meta_type_.set_int();
  column.clear_default_value(owner);EXPECT_TRUE(owner.live.empty());
  EXPECT_EQ(7,column.info_);EXPECT_EQ(918,column.default_checksum_);EXPECT_EQ(ObIntType,column.meta_type_.get_type());
  column.destroy(owner);column.destroy(owner);EXPECT_TRUE(owner.live.empty());
}

TEST(NativeStorageColumnSchema, array_copy_failures_preserve_existing_default) {
  for(bool reference_leaves : {false,true}) {
    ObArenaAllocator source;const DatumValue input=array_value(source,40,reference_leaves);bool success=false;
    for(int offset=1;offset<100&&!success;++offset) {
      StorageSchemaTestAllocator owner;ObStorageColumnSchema column;put_old_value(column,owner);
      const std::string before=wire(column);owner.fail_at=owner.calls+offset;
      const int result=column.deep_copy_default_val(owner,input);
      if(result==OB_SUCCESS) {success=true;EXPECT_GT(owner.live.size(),reference_leaves?16:0);}
      else {EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,result);EXPECT_EQ(before,wire(column));EXPECT_EQ(1,owner.live.size());}
      column.destroy(owner);EXPECT_TRUE(owner.live.empty());
    }
    EXPECT_TRUE(success);
  }
}

TEST(NativeStorageColumnSchema, decoder_rolls_back_all_nested_payload_allocations) {
  for(bool reference_leaves : {false,true}) {
    ObArenaAllocator source;const DatumValue input=array_value(source,40,reference_leaves);
    ObStorageColumnSchema original;ASSERT_EQ(OB_SUCCESS,original.deep_copy_default_val(source,input));
    original.info_=7;original.default_checksum_=918;original.meta_type_.set_int();const std::string bytes=wire(original);
    bool success=false;
    for(int offset=1;offset<100&&!success;++offset) {
      StorageSchemaTestAllocator owner;ObStorageColumnSchema column;put_old_value(column,owner);
      const std::string before=wire(column);owner.fail_at=owner.calls+offset;int64_t pos=0;
      const int result=column.deserialize(owner,bytes.data(),bytes.size(),pos);
      if(result==OB_SUCCESS) {success=true;EXPECT_EQ(bytes,wire(column));EXPECT_GT(owner.live.size(),reference_leaves?16:0);}
      else {EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,result);EXPECT_EQ(before,wire(column));EXPECT_EQ(1,owner.live.size());}
      column.destroy(owner);EXPECT_TRUE(owner.live.empty());
    }
    EXPECT_TRUE(success);original.destroy(source);
  }
}

TEST(NativeStorageColumnSchema, typed_null_keeps_column_declaration_and_no_payload) {
  DatumValue input;input.meta_.set_enum();StorageSchemaTestAllocator owner;ObStorageColumnSchema column;
  column.meta_type_.set_enum();ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,input));
  EXPECT_EQ(ObEnumType,column.get_data_type());EXPECT_TRUE(column.get_orig_default_datum().is_null());
  EXPECT_EQ(ObNullType,column.get_orig_default_meta().get_type());EXPECT_TRUE(owner.live.empty());
  column.destroy(owner);
}


TEST(NativeStorageColumnSchema, native_scalar_import_self_copy_and_decode_are_allocation_free)
{
  for (const int64_t number : {INT64_MIN,int64_t(0),INT64_MAX}) {
    SCOPED_TRACE(number);
    StorageSchemaTestAllocator owner,decode_owner;
    owner.fail_at=1;decode_owner.fail_at=1;
    ObStorageColumnSchema column,decoded;
    column.info_=3;column.default_checksum_=918;column.meta_type_.set_int();
    {
      ObArenaAllocator inputs;DatumValue source;
      ASSERT_EQ(OB_SUCCESS,source.set_int(number,inputs));
      ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,source));
    }
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_datum(owner,column.get_orig_default_datum(),column.get_orig_default_meta()));
    EXPECT_EQ(number,column.get_orig_default_datum().get_int());
    EXPECT_EQ(0,column.get_orig_default_datum().length());
    const std::string bytes=wire(column);int64_t position=0;
    ASSERT_EQ(OB_SUCCESS,decoded.deserialize(decode_owner,bytes.data(),bytes.size(),position));
    EXPECT_EQ(bytes.size(),position);EXPECT_EQ(bytes,wire(decoded));
    EXPECT_EQ(number,decoded.get_orig_default_datum().get_int());
    EXPECT_EQ(0,owner.calls);EXPECT_EQ(0,decode_owner.calls);
    column.clear_default_value(owner);
    EXPECT_TRUE(column.get_orig_default_datum().is_null());
    EXPECT_EQ(3,column.info_);EXPECT_EQ(918,column.default_checksum_);EXPECT_EQ(ObIntType,column.meta_type_.get_type());
    column.destroy(owner);decoded.destroy(decode_owner);
    EXPECT_TRUE(owner.live.empty());EXPECT_TRUE(decode_owner.live.empty());
  }
}

TEST(NativeStorageColumnSchema, empty_reference_defaults_release_their_real_zero_length_allocations)
{
  for (const bool decimal : {false,true}) {
    SCOPED_TRACE(decimal);
    StorageSchemaTestAllocator owner;ObStorageColumnSchema column;
    ObArenaAllocator inputs;DatumValue source;
    if (decimal) { ASSERT_EQ(OB_SUCCESS,source.set_decimal_int(0,5,nullptr,inputs)); }
    else {
      ASSERT_EQ(OB_SUCCESS,source.set_varchar(ObString(""),inputs));
      source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,source));
    EXPECT_TRUE(column.get_orig_default_datum().is_present());
    EXPECT_EQ(0,column.get_orig_default_datum().length());
    ASSERT_EQ(1,owner.live.size());
    EXPECT_NE(nullptr,column.get_orig_default_datum().get_reference());
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_datum(owner,column.get_orig_default_datum(),column.get_orig_default_meta()));
    EXPECT_EQ(1,owner.live.size());
    column.clear_default_value(owner);
    EXPECT_TRUE(owner.live.empty());EXPECT_TRUE(column.get_orig_default_datum().is_null());
    column.destroy(owner);column.destroy(owner);EXPECT_TRUE(owner.live.empty());
  }
}

TEST(NativeStorageColumnSchema, native_array_copy_is_independent_and_failed_copy_keeps_existing_record)
{
  StorageSchemaTestAllocator original_owner,copy_owner,decode_owner;
  ObStorageColumnSchema original,copied,decoded;
  {
    ObArenaAllocator inputs;const DatumValue array=array_value(inputs,4);
    ASSERT_EQ(OB_SUCCESS,original.deep_copy_default_val(original_owner,array));
  }
  original.info_=7;original.default_checksum_=918;original.meta_type_.set_int();
  ASSERT_EQ(OB_SUCCESS,copied.deep_copy_default_datum(copy_owner,original.get_orig_default_datum(),original.get_orig_default_meta()));
  copied.info_=original.info_;copied.default_checksum_=original.default_checksum_;copied.meta_type_=original.meta_type_;
  EXPECT_NE(original.get_orig_default_datum().get_reference(),copied.get_orig_default_datum().get_reference());
  const std::string expected=wire(original);
  original.destroy(original_owner);EXPECT_TRUE(original_owner.live.empty());
  std::string input=expected;int64_t position=0;
  ASSERT_EQ(OB_SUCCESS,decoded.deserialize(decode_owner,input.data(),input.size(),position));
  MEMSET(&input[0],0x5a,input.size());
  for (const ObStorageColumnSchema *column : {&copied,&decoded}) {
    alignas(8) char scratch[64];DatumValue view;
    ASSERT_EQ(OB_SUCCESS,column->export_orig_default_value(view,scratch,sizeof(scratch)));
    const auto *array=reinterpret_cast<const DatumParameterArray *>(view.get_ext());
    ASSERT_NE(nullptr,array);ASSERT_EQ(4,array->count_);
    for (int64_t i=0;i<4;++i) { EXPECT_EQ(i+100,array->data_[i].get_int()); }
    EXPECT_EQ(expected,wire(*column));
  }
  StorageSchemaTestAllocator failing_owner;ObStorageColumnSchema target;put_old_value(target,failing_owner);
  const std::string before=wire(target);const size_t blocks=failing_owner.live.size();
  failing_owner.fail_at=failing_owner.calls+1;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,target.deep_copy_default_datum(failing_owner,copied.get_orig_default_datum(),copied.get_orig_default_meta()));
  EXPECT_EQ(before,wire(target));EXPECT_EQ(blocks,failing_owner.live.size());
  target.destroy(failing_owner);copied.destroy(copy_owner);decoded.destroy(decode_owner);
  EXPECT_TRUE(failing_owner.live.empty());EXPECT_TRUE(copy_owner.live.empty());EXPECT_TRUE(decode_owner.live.empty());
}

TEST(NativeStorageColumnSchema, canonical_and_wrapped_extended_states_release_without_leaks)
{
  for (const int64_t marker : {int64_t(-3),int64_t(-2),int64_t(ObActionFlag::OP_NOP)}) {
    SCOPED_TRACE(marker);
    ObArenaAllocator inputs;DatumValue source;
    ASSERT_EQ(OB_SUCCESS,source.set_extend(marker,0,inputs));
    StorageSchemaTestAllocator owner,decode_owner;
    ObStorageColumnSchema column,decoded;
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,source));
    EXPECT_EQ(marker,column.get_orig_default_datum().get_ext());
    EXPECT_TRUE(owner.live.empty()); // Canonical special states own no wrapper.
    const std::string bytes=wire(column);int64_t position=0;
    ASSERT_EQ(OB_SUCCESS,decoded.deserialize(decode_owner,bytes.data(),bytes.size(),position));
    EXPECT_EQ(marker,decoded.get_orig_default_datum().get_ext());
    EXPECT_TRUE(decode_owner.live.empty()); // Any temporary legacy decoder wrapper was reclaimed.
    column.destroy(owner);decoded.destroy(decode_owner);
    EXPECT_TRUE(owner.live.empty());EXPECT_TRUE(decode_owner.live.empty());
  }
  ObArenaAllocator inputs;DatumValue boxed;
  ASSERT_EQ(OB_SUCCESS,boxed.set_extend(ObActionFlag::OP_NOP,0,inputs));
  alignas(8) char legacy_payload[DATUM_EXTEND_PAYLOAD_SIZE];
  ObDatum physical_marker; physical_marker.ptr_ = legacy_payload;
  physical_marker.set_extend(ObActionFlag::OP_NOP,0,0);
  Datum wrapped;
  ASSERT_EQ(OB_SUCCESS,wrapped.set_reference(physical_marker.ptr_,physical_marker.len_,Datum::LegacyExtended));
  ASSERT_TRUE(wrapped.has_ext_value());
  StorageSchemaTestAllocator owner;ObStorageColumnSchema column;
  ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_datum(owner,wrapped,boxed.meta_));
  EXPECT_EQ(int64_t(ObActionFlag::OP_NOP),column.get_orig_default_datum().get_ext());
  EXPECT_TRUE(column.get_orig_default_datum().is_nop() || column.get_orig_default_datum().has_ext_value());
  column.destroy(owner);EXPECT_TRUE(owner.live.empty());
}

int main(int argc,char **argv) {
  testing::InitGoogleTest(&argc,argv);if(ObCharset::init_charset()!=OB_SUCCESS)return 2;return RUN_ALL_TESTS();
}
