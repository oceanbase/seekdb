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

#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_schema_macro_define.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <type_traits>
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
static_assert(std::is_same<decltype(std::declval<const ObColumnSchemaV2 &>().get_orig_default_value()),
    const DatumValue &>::value, "Schema defaults must expose the stored native value");
namespace {
std::vector<std::string> columns(const std::string &line) {
  std::vector<std::string> out;std::istringstream input(line);std::string field;
  while(std::getline(input,field,'\t')) { out.push_back(field); }return out;
}
std::string unhex(const std::string &s) {
  std::string out;for(size_t i=0;i<s.size();i+=2) { out+=static_cast<char>(std::stoul(s.substr(i,2),nullptr,16)); }return out;
}
std::string hex(const std::string &s) {
  const char *digits="0123456789abcdef";std::string out;
  for(unsigned char c:s) { out+=digits[c>>4];out+=digits[c&15]; }return out;
}
std::string wire(const ObColumnSchemaV2 &column) {
  std::string bytes(column.get_serialize_size(),'\0');int64_t pos=0;
  if(column.serialize(&bytes[0],bytes.size(),pos)!=OB_SUCCESS || pos!=bytes.size()) { return "SERIALIZATION_ERROR"; }
  return hex(bytes);
}
class FailingArena : public ObIAllocator {
public:
  ObArenaAllocator arena_; bool fail_=false; int64_t requested_=0;
  void *alloc(int64_t size) override { if(fail_) { return nullptr; }requested_+=size;return arena_.alloc(size); }
  void *alloc(int64_t size,const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override { arena_.free(p); }
};
}

TEST(NativeColumnSchema, full_frames_match_original_production_serializer) {
  struct Dataset { const char *inputs; const char *oracle; int count; };
  const Dataset datasets[] = {
    {"unittest/share/data/typed_values_compare.golden", "unittest/share/data/column_schema.golden", 104},
    {"unittest/share/data/literal_lob_temporal_inputs.golden", "unittest/share/data/column_schema_lob_temporal.golden", 49},
  };
  int64_t checked = 0;
  for (const auto &dataset : datasets) {
  ObArenaAllocator owner;std::vector<DatumValue> values;std::string line;
  std::ifstream input(dataset.inputs);ASSERT_TRUE(input.good());
  while(std::getline(input,line)) {
    if(line.empty() || line[0]=='#') { continue; }const auto f=columns(line);if(f[0]!="V") { continue; }
    const auto bytes=unhex(f[3]);DatumValue value;int64_t pos=0;
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,owner,value.datum_,value.meta_));
    ASSERT_EQ(bytes.size(),pos);values.push_back(value);
  }
  ASSERT_EQ(dataset.count,values.size());
  std::ifstream oracle(dataset.oracle);ASSERT_TRUE(oracle.good());
  while(std::getline(oracle,line)) {
    if(line.empty() || line[0]=='#') { continue; }const auto f=columns(line);if(f[0]!="C") { continue; }
    const int i=std::stoi(f[1]),j=std::stoi(f[2]),variant=std::stoi(f[3]);SCOPED_TRACE(line);
    ObArenaAllocator row_owner;ObColumnSchemaV2 column(&row_owner);
    column.set_table_id(50001+i);column.set_column_id(16+i);column.set_schema_version(123456+variant);
    column.set_data_type(variant%2?ObVarcharType:ObIntType);column.set_data_length(128);
    column.set_data_precision(18);column.set_data_scale(4);column.set_nullable(variant%2);
    column.set_collation_type(CS_TYPE_UTF8MB4_BIN);column.set_charset_type(CHARSET_UTF8MB4);
    column.set_rowkey_position(variant%2);column.set_prev_column_id(15+i);column.set_next_column_id(17+i);
    ASSERT_EQ(OB_SUCCESS,column.set_column_name(ObString::make_string("customer_客户")));
    ASSERT_EQ(OB_SUCCESS,column.set_comment("original default oracle"));
    ASSERT_EQ(OB_SUCCESS,column.set_orig_default_value(values.at(i)));
    DatumParameter parameter;parameter.assign_value(values.at(j));
    ObAccuracy accuracy;accuracy.set_precision(30);accuracy.set_scale(7);parameter.set_accuracy(accuracy);
    parameter.set_result_flags(0x1357);parameter.set_param_meta();
    ASSERT_EQ(OB_SUCCESS,column.set_cur_default_value(parameter,variant>=2));
    if(variant>=2) { ASSERT_EQ(OB_SUCCESS,column.add_cascaded_column_id(100+i)); }
    EXPECT_EQ(f[4],wire(column));++checked;
  }
  }
  EXPECT_EQ(612,checked);
}

TEST(NativeColumnSchema, short_and_truncated_bodies_match_original_decoder) {
  int64_t checked = 0;
  const char *paths[] = {"unittest/share/data/column_schema.golden", "unittest/share/data/column_schema_lob_temporal.golden"};
  for (const char *path : paths) {
  std::ifstream oracle(path);ASSERT_TRUE(oracle.good());
  std::string line;
  while(std::getline(oracle,line)) {
    if(line.empty() || line[0]=='#') { continue; }const auto f=columns(line);if(f[0]!="D") { continue; }
    SCOPED_TRACE(line);ObArenaAllocator owner;ObColumnSchemaV2 column(&owner);
    const auto input=unhex(f[4]);int64_t pos=0;
    const int result=column.deserialize(input.data(),input.size(),pos);
    ASSERT_EQ(std::stoi(f[3]),result);
    EXPECT_EQ(input.size(),pos);
    if(result==OB_SUCCESS) { EXPECT_EQ(f[5],wire(column)); }
    ++checked;
  }
  }
  EXPECT_EQ(2064,checked);
}

TEST(NativeColumnSchema, copy_and_decode_own_payloads_after_source_is_destroyed) {
  ObArenaAllocator owner;ObColumnSchemaV2 copy(&owner),decoded(&owner);
  {
    ObArenaAllocator source_owner;ObColumnSchemaV2 source(&source_owner);DatumValue number,text;
    ASSERT_EQ(OB_SUCCESS,number.set_int(918,source_owner));
    ASSERT_EQ(OB_SUCCESS,text.set_varchar(ObString::make_string("customer_客户"),source_owner));
    text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS,source.set_orig_default_value(number));
    ASSERT_EQ(OB_SUCCESS,source.set_cur_default_value(text,true));
    ASSERT_NE(number.datum_.ptr_,source.get_orig_default_value().datum_.ptr_);
    ASSERT_NE(text.datum_.ptr_,source.get_cur_default_value().datum_.ptr_);
    ASSERT_EQ(OB_SUCCESS,copy.assign(source));
    ASSERT_NE(source.get_orig_default_value().datum_.ptr_,copy.get_orig_default_value().datum_.ptr_);
    const auto bytes=unhex(wire(source));int64_t pos=0;
    ASSERT_EQ(OB_SUCCESS,decoded.deserialize(bytes.data(),bytes.size(),pos));
    ASSERT_NE(source.get_cur_default_value().datum_.ptr_,decoded.get_cur_default_value().datum_.ptr_);
  }
  EXPECT_EQ(918,copy.get_orig_default_value().get_int());
  EXPECT_EQ(ObString::make_string("customer_客户"),copy.get_cur_default_value().get_string());
  EXPECT_EQ(wire(copy),wire(decoded));
  EXPECT_GE(copy.get_convert_size(),sizeof(copy)+sizeof(int64_t)+copy.get_cur_default_value().get_string_len());
}

TEST(NativeColumnSchema, failed_replacement_keeps_value_and_expression_flag) {
  FailingArena owner;ObColumnSchemaV2 column(&owner);ObArenaAllocator inputs;
  DatumValue old_value,replacement;
  ASSERT_EQ(OB_SUCCESS,old_value.set_int(1,inputs));
  ASSERT_EQ(OB_SUCCESS,replacement.set_varchar(ObString::make_string("replacement"),inputs));
  replacement.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,column.set_orig_default_value(old_value));
  ASSERT_EQ(OB_SUCCESS,column.set_cur_default_value(old_value,false));
  const auto before=wire(column);const char *original=column.get_cur_default_value().datum_.ptr_;
  owner.fail_=true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,column.set_cur_default_value(replacement,true));
  EXPECT_EQ(original,column.get_cur_default_value().datum_.ptr_);EXPECT_EQ(before,wire(column));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,column.set_orig_default_value(replacement));
  EXPECT_EQ(before,wire(column));
}

TEST(NativeColumnSchema, nested_native_arrays_are_owned_and_counted) {
  FailingArena owner;ObColumnSchemaV2 column(&owner);
  {
    ObArenaAllocator source;DatumParameterArray *array=DatumParameterArray::alloc(source,2);ASSERT_NE(nullptr,array);
    DatumParameterArray *nested=DatumParameterArray::alloc(source,1);ASSERT_NE(nullptr,nested);
    ASSERT_EQ(OB_SUCCESS,nested->data_[0].set_int(42,source));
    ASSERT_EQ(OB_SUCCESS,array->data_[1].set_varchar(ObString::make_string("customer"),source));
    SqlTypeMeta meta;meta.set_ext();meta.set_extend_type(T_EXT_SQL_ARRAY);
    array->data_[0].set_type_meta(meta);
    ASSERT_EQ(OB_SUCCESS,array->data_[0].alloc_datum_reserved_buff(meta,-1,source));
    array->data_[0].set_array_address(nested);
    DatumParameter value;value.set_type_meta(meta);
    ASSERT_EQ(OB_SUCCESS,value.alloc_datum_reserved_buff(meta,-1,source));value.set_array_address(array);
    ASSERT_EQ(OB_SUCCESS,column.set_orig_default_value(value));
    EXPECT_NE(value.datum_.ptr_,column.get_orig_default_value().datum_.ptr_);
  }
  const auto *array=reinterpret_cast<const DatumParameterArray *>(column.get_orig_default_value().get_ext());
  ASSERT_NE(nullptr,array);ASSERT_EQ(2,array->count_);
  const auto *nested=array->data_[0].get_array<DatumParameterArray>();
  ASSERT_NE(nullptr,nested);ASSERT_EQ(1,nested->count_);EXPECT_EQ(42,nested->data_[0].get_int());
  EXPECT_EQ(ObString::make_string("customer"),array->data_[1].get_string());
  EXPECT_GE(column.get_convert_size(),sizeof(column)+owner.requested_);
  const auto bytes=unhex(wire(column));ObArenaAllocator decode_owner;ObColumnSchemaV2 decoded(&decode_owner);int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,decoded.deserialize(bytes.data(),bytes.size(),pos));EXPECT_EQ(wire(column),wire(decoded));
}

TEST(NativeColumnSchema, typed_null_defaults_keep_stable_value_metadata_across_storage_roundtrip) {
  ObArenaAllocator owner;
  ObColumnSchemaV2 source(&owner), decoded(&owner);
  source.set_data_type(ObEnumType);
  DatumParameter parameter;
  SqlTypeMeta declaration; declaration.set_enum(); parameter.set_type_meta(declaration);
  ASSERT_TRUE(parameter.is_null());
  ASSERT_EQ(OB_SUCCESS, source.set_orig_default_value(parameter));
  ASSERT_EQ(OB_SUCCESS, source.set_cur_default_value(parameter, false));
  EXPECT_TRUE(source.get_cur_default_value().is_null());
  EXPECT_EQ(ObNullType, source.get_orig_default_value().get_type());
  EXPECT_EQ(ObNullType, source.get_cur_default_value().get_type());
  const auto bytes = unhex(wire(source)); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(bytes.data(), bytes.size(), pos));
  EXPECT_EQ(source.get_orig_default_value().get_type(), decoded.get_orig_default_value().get_type());
  EXPECT_EQ(source.get_cur_default_value().get_type(), decoded.get_cur_default_value().get_type());
  EXPECT_EQ(ObEnumType, parameter.get_type()); // the source declaration is separate
  EXPECT_EQ(ObEnumType, source.get_data_type());
}

TEST(NativeColumnSchema, generated_scalar_default_payloads_are_owned_by_schema)
{
  ObArenaAllocator owner;
  ObColumnSchemaV2 column(&owner);
  const uint64_t uint_value = 0xf123456789abcdefULL;
  DatumValue uint_default = make_inner_table_default_uint64(uint_value);
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(uint_default));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(uint_default, false));
  EXPECT_EQ(ObUInt64Type, column.get_orig_default_value().get_type());
  EXPECT_EQ(uint_value, column.get_orig_default_value().datum_.get_uint());
  EXPECT_EQ(uint_value, column.get_cur_default_value().datum_.get_uint());
  EXPECT_NE(uint_default.datum_.ptr_, column.get_orig_default_value().datum_.ptr_);
  EXPECT_NE(uint_default.datum_.ptr_, column.get_cur_default_value().datum_.ptr_);

  DatumValue int_default = make_inner_table_default_int(ObIntType, -918273645);
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(int_default));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(int_default, false));
  EXPECT_EQ(-918273645, column.get_orig_default_value().get_int());
  EXPECT_EQ(-918273645, column.get_cur_default_value().get_int());
  EXPECT_NE(int_default.datum_.ptr_, column.get_orig_default_value().datum_.ptr_);

  DatumValue timestamp_default = make_inner_table_default_timestamp(1234567890123LL);
  ASSERT_EQ(OB_SUCCESS, column.set_orig_default_value(timestamp_default));
  ASSERT_EQ(OB_SUCCESS, column.set_cur_default_value(timestamp_default, false));
  EXPECT_EQ(1234567890123LL, column.get_orig_default_value().get_timestamp());
  EXPECT_EQ(1234567890123LL, column.get_cur_default_value().get_timestamp());
  EXPECT_NE(timestamp_default.datum_.ptr_, column.get_cur_default_value().datum_.ptr_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
