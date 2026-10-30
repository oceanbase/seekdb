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
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,owner,value.datum_,value.meta_));
  EXPECT_EQ(bytes.size(),pos);return value;
}
void put_old_value(ObStorageColumnSchema &column,StorageSchemaTestAllocator &owner) {
  ObArenaAllocator input;DatumValue value;
  ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString::make_string("unchanged"),input));
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,value));
  column.info_=7;column.meta_type_.set_int();column.default_checksum_=918;
}
DatumValue array_value(ObIAllocator &owner,int count) {
  auto *array=DatumParameterArray::alloc(owner,count);
  EXPECT_NE(nullptr,array);DatumParameter value;
  if(array==nullptr)return value.value_view();
  for(int i=0;i<count;++i)EXPECT_EQ(OB_SUCCESS,array->data_[i].set_int(i+100,owner));
  SqlTypeMeta meta;meta.set_ext();meta.set_extend_type(T_EXT_SQL_ARRAY);
  value.set_type_meta(meta);EXPECT_EQ(OB_SUCCESS,value.alloc_datum_reserved_buff(meta,-1,owner));
  value.set_array_address(array);return value.value_view();
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
  EXPECT_EQ("independent payload",std::string(column.get_orig_default_value().get_string_ptr(),column.get_orig_default_value().get_string_len()));
  for(int i=0;i<100;++i) {
    ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,column.get_orig_default_value()));
    EXPECT_EQ(1,owner.live.size());
  }
  column.info_=7;column.default_checksum_=918;column.meta_type_.set_int();
  column.clear_default_value(owner);EXPECT_TRUE(owner.live.empty());
  EXPECT_EQ(7,column.info_);EXPECT_EQ(918,column.default_checksum_);EXPECT_EQ(ObIntType,column.meta_type_.get_type());
  column.destroy(owner);column.destroy(owner);EXPECT_TRUE(owner.live.empty());
}

TEST(NativeStorageColumnSchema, array_copy_failures_preserve_existing_default) {
  ObArenaAllocator source;const DatumValue input=array_value(source,40);bool success=false;
  for(int offset=1;offset<100&&!success;++offset) {
    StorageSchemaTestAllocator owner;ObStorageColumnSchema column;put_old_value(column,owner);
    const std::string before=wire(column);owner.fail_at=owner.calls+offset;
    const int result=column.deep_copy_default_val(owner,input);
    if(result==OB_SUCCESS) {success=true;EXPECT_GT(owner.live.size(),16);}
    else {EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,result);EXPECT_EQ(before,wire(column));EXPECT_EQ(1,owner.live.size());}
    column.destroy(owner);EXPECT_TRUE(owner.live.empty());
  }
  EXPECT_TRUE(success);
}

TEST(NativeStorageColumnSchema, decoder_rolls_back_all_nested_payload_allocations) {
  ObArenaAllocator source;const DatumValue input=array_value(source,40);
  ObStorageColumnSchema original;ASSERT_EQ(OB_SUCCESS,original.deep_copy_default_val(source,input));
  original.info_=7;original.default_checksum_=918;original.meta_type_.set_int();const std::string bytes=wire(original);
  bool success=false;
  for(int offset=1;offset<100&&!success;++offset) {
    StorageSchemaTestAllocator owner;ObStorageColumnSchema column;put_old_value(column,owner);
    const std::string before=wire(column);owner.fail_at=owner.calls+offset;int64_t pos=0;
    const int result=column.deserialize(owner,bytes.data(),bytes.size(),pos);
    if(result==OB_SUCCESS) {success=true;EXPECT_EQ(bytes,wire(column));EXPECT_GT(owner.live.size(),16);}
    else {EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,result);EXPECT_EQ(before,wire(column));EXPECT_EQ(1,owner.live.size());}
    column.destroy(owner);EXPECT_TRUE(owner.live.empty());
  }
  EXPECT_TRUE(success);original.destroy(source);
}

TEST(NativeStorageColumnSchema, typed_null_keeps_column_declaration_and_no_payload) {
  DatumValue input;input.meta_.set_enum();StorageSchemaTestAllocator owner;ObStorageColumnSchema column;
  column.meta_type_.set_enum();ASSERT_EQ(OB_SUCCESS,column.deep_copy_default_val(owner,input));
  EXPECT_EQ(ObEnumType,column.get_data_type());EXPECT_TRUE(column.get_orig_default_value().is_null());
  EXPECT_EQ(ObNullType,column.get_orig_default_value().get_type());EXPECT_TRUE(owner.live.empty());
  column.destroy(owner);
}

int main(int argc,char **argv) {
  testing::InitGoogleTest(&argc,argv);if(ObCharset::init_charset()!=OB_SUCCESS)return 2;return RUN_ALL_TESTS();
}
