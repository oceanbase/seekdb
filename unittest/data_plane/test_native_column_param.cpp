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


#include "data_plane/access/ob_table_param.h"
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdlib>
#include <type_traits>
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using oceanbase::blocksstable::ObStorageDatum;
static_assert(std::is_same<decltype(std::declval<const ObColumnParam &>().get_orig_default_value()),const DatumValue &>::value);
namespace {
struct Dataset { const char *inputs; const char *oracle; int count; };
const Dataset datasets[]={
 {"unittest/data_plane/data/column_param_inputs.golden","unittest/data_plane/data/column_param.golden",104},
 {"unittest/data_plane/data/column_param_lob_temporal_inputs.golden","unittest/data_plane/data/column_param_lob_temporal.golden",49},
};
std::vector<std::string> fields(const std::string &line){std::vector<std::string> out;std::string f;std::istringstream in(line);while(std::getline(in,f,'\t'))out.push_back(f);return out;}
std::string unhex(const std::string &s){std::string out;for(size_t i=0;i<s.size();i+=2)out+=static_cast<char>(std::stoul(s.substr(i,2),nullptr,16));return out;}
std::string hex(const std::string &s){const char *d="0123456789abcdef";std::string out;for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;}
std::string wire(const ObColumnParam &p){std::string b(p.get_serialize_size(),'\0');int64_t pos=0;if(p.serialize(&b[0],b.size(),pos)!=OB_SUCCESS||pos!=b.size())return "SERIALIZE_ERROR";return b;}
class ColumnParamTestAllocator : public ObIAllocator {
public:
 int64_t calls_=0,fail_at_=INT64_MAX;std::unordered_map<void *,int64_t> live_;
 void *alloc(int64_t size) override {if(++calls_==fail_at_)return nullptr;void *p=std::malloc(size>0?size:1);if(p)live_[p]=size;return p;}
 void *alloc(int64_t size,const ObMemAttr &) override{return alloc(size);}
 void free(void *p) override {if(p){EXPECT_EQ(1,live_.erase(p));std::free(p);}}
 ~ColumnParamTestAllocator(){EXPECT_TRUE(live_.empty());for(auto &p:live_)std::free(p.first);}
};
std::vector<DatumValue> load(const char *path,ObIAllocator &owner){
 std::ifstream in(path);EXPECT_TRUE(in.good());std::vector<DatumValue> values;std::string line;
 while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;const auto f=fields(line);if(f[0]!="V")continue;
  const auto b=unhex(f[3]);int64_t pos=0;DatumValue value;const int ret=DatumParameterCodec::decode_value(b.data(),b.size(),pos,owner,value.datum_,value.meta_);EXPECT_EQ(OB_SUCCESS,ret);EXPECT_EQ(b.size(),pos);if(ret==OB_SUCCESS)values.push_back(value);
 }return values;
}
void metadata(ObColumnParam &p,int i,int variant){
 SqlTypeMeta meta;meta.set_int();p.set_meta_type(meta);p.set_column_id(16+i);ObAccuracy accuracy;accuracy.set_precision(18);accuracy.set_scale(4);p.set_accuracy(accuracy);
 p.set_column_order(variant%2?ObOrderType::DESC:ObOrderType::ASC);p.set_nullable_for_write(variant%2);p.set_nullable_for_read(variant>=2);p.set_gen_col_flag(variant>=2,variant==3);p.set_gen_col_udf_expr(variant==1);p.set_is_hidden(variant==2);p.set_lob_chunk_size(8192+variant);p.set_is_data_table_rowkey(variant==3);
}
DatumParameter make_array(ObIAllocator &owner,int count){
 DatumParameter result;auto *a=DatumParameterArray::alloc(owner,count);EXPECT_NE(nullptr,a);
 if(a){for(int i=0;i<count;++i){EXPECT_EQ(OB_SUCCESS,a->data_[i].set_varchar(ObString::make_string("native array payload"),owner));a->data_[i].set_collation_type(CS_TYPE_UTF8MB4_BIN);}
  SqlTypeMeta meta;meta.set_ext();meta.set_extend_type(T_EXT_SQL_ARRAY);result.set_type_meta(meta);EXPECT_EQ(OB_SUCCESS,result.alloc_datum_reserved_buff(meta,-1,owner));result.set_array_address(a);
 }return result;
}
}
TEST(NativeColumnParam, original_full_frames_are_unchanged_and_all_payloads_are_released){
 int checked=0;
 for(const auto &dataset:datasets){ObArenaAllocator inputs;auto values=load(dataset.inputs,inputs);ASSERT_EQ(dataset.count,values.size());std::ifstream oracle(dataset.oracle);ASSERT_TRUE(oracle.good());std::string line;
  while(std::getline(oracle,line)){if(line.empty()||line[0]=='#')continue;auto f=fields(line);if(f[0]!="C")continue;SCOPED_TRACE(line);
   ColumnParamTestAllocator owner;ObColumnParam p(owner);metadata(p,std::stoi(f[1]),std::stoi(f[3]));
   ASSERT_EQ(OB_SUCCESS,p.set_orig_default_value(values.at(std::stoi(f[1]))));
   DatumParameter parameter;parameter.assign_value(values.at(std::stoi(f[2])));ObAccuracy accuracy;accuracy.set_precision(30);accuracy.set_scale(7);parameter.set_accuracy(accuracy);parameter.set_result_flags(0x1357);
   ASSERT_EQ(OB_SUCCESS,p.set_cur_default_value(parameter));EXPECT_EQ(f[4],hex(wire(p)));p.destroy();EXPECT_TRUE(owner.live_.empty());++checked;
  }
 }
 EXPECT_EQ(612,checked);
}
TEST(NativeColumnParam, original_short_body_status_and_nullable_fallback_are_preserved){
 int checked=0;
 for(const auto &dataset:datasets){std::ifstream oracle(dataset.oracle);ASSERT_TRUE(oracle.good());std::string line;
  while(std::getline(oracle,line)){if(line.empty()||line[0]=='#')continue;auto f=fields(line);if(f[0]!="D")continue;SCOPED_TRACE(line);
   ColumnParamTestAllocator owner;ObColumnParam p(owner);const auto input=unhex(f[4]);int64_t pos=0;const int ret=p.deserialize(input.data(),input.size(),pos);EXPECT_EQ(std::stoi(f[3]),ret);EXPECT_EQ(input.size(),pos);
   if(ret==OB_SUCCESS){EXPECT_EQ(f[5],hex(wire(p)));}p.destroy();EXPECT_TRUE(owner.live_.empty());++checked;
  }
 }
 EXPECT_EQ(736,checked);
}
TEST(NativeStorageValue, old_conversion_bytes_flags_and_checksums_are_preserved){
 int checked=0;
 for(const auto &dataset:datasets){ObArenaAllocator inputs;auto values=load(dataset.inputs,inputs);ASSERT_EQ(dataset.count,values.size());std::ifstream oracle(dataset.oracle);ASSERT_TRUE(oracle.good());std::string line;
  while(std::getline(oracle,line)){if(line.empty()||line[0]=='#')continue;auto f=fields(line);if(f[0]!="S")continue;SCOPED_TRACE(line);
   ObStorageDatum result;const int ret=result.from_value(values.at(std::stoi(f[1])));ASSERT_EQ(std::stoi(f[2]),ret);EXPECT_EQ(std::stoul(f[3]),result.pack_);
   const auto payload=result.is_null()?"-":hex(std::string(result.ptr_,result.len_));EXPECT_EQ(f[4],payload);EXPECT_EQ(std::stoll(f[5]),result.checksum(0));++checked;
  }
 }
 EXPECT_EQ(153,checked);
}
TEST(NativeColumnParam, setter_owns_temporary_values_and_replacement_does_not_leak){
 ColumnParamTestAllocator owner;ObColumnParam p(owner);
 {ObArenaAllocator input;DatumValue number,text;ASSERT_EQ(OB_SUCCESS,number.set_int(918,input));ASSERT_EQ(OB_SUCCESS,text.set_varchar(ObString::make_string("customer_客户"),input));text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,p.set_orig_default_value(number));ASSERT_EQ(OB_SUCCESS,p.set_cur_default_value(text));EXPECT_NE(number.datum_.ptr_,p.get_orig_default_value().datum_.ptr_);EXPECT_NE(text.datum_.ptr_,p.get_cur_default_value().datum_.ptr_);
 }
 EXPECT_EQ(918,p.get_orig_default_value().get_int());EXPECT_EQ(ObString::make_string("customer_客户"),p.get_cur_default_value().get_string());
 for(int i=0;i<100;++i){ASSERT_EQ(OB_SUCCESS,p.set_orig_default_value(p.get_orig_default_value()));ASSERT_EQ(OB_SUCCESS,p.set_cur_default_value(p.get_cur_default_value()));EXPECT_EQ(2,owner.live_.size());}
 p.reset();EXPECT_TRUE(owner.live_.empty());p.destroy();EXPECT_TRUE(owner.live_.empty());
}
TEST(NativeColumnParam, failed_setter_and_assign_keep_prior_values_and_metadata){
 ColumnParamTestAllocator owner,source_owner;ObColumnParam p(owner),source(source_owner);ObArenaAllocator input;DatumValue old_value,new_value;
 ASSERT_EQ(OB_SUCCESS,old_value.set_int(7,input));ASSERT_EQ(OB_SUCCESS,new_value.set_varchar(ObString::make_string("new value"),input));new_value.set_collation_type(CS_TYPE_UTF8MB4_BIN);
 ASSERT_EQ(OB_SUCCESS,p.set_orig_default_value(old_value));ASSERT_EQ(OB_SUCCESS,p.set_cur_default_value(old_value));metadata(p,0,0);const auto before=wire(p);const auto blocks=owner.live_.size();
 owner.fail_at_=owner.calls_+1;EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,p.set_cur_default_value(new_value));EXPECT_EQ(before,wire(p));EXPECT_EQ(blocks,owner.live_.size());
 ASSERT_EQ(OB_SUCCESS,source.set_orig_default_value(new_value));ASSERT_EQ(OB_SUCCESS,source.set_cur_default_value(new_value));metadata(source,30,3);
 owner.fail_at_=owner.calls_+2;EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,p.assign(source));EXPECT_EQ(before,wire(p));EXPECT_EQ(blocks,owner.live_.size());
 owner.fail_at_=INT64_MAX;ASSERT_EQ(OB_SUCCESS,p.assign(source));EXPECT_EQ(wire(source),wire(p));EXPECT_EQ(OB_SUCCESS,p.assign(p));p.destroy();source.destroy();EXPECT_TRUE(owner.live_.empty());EXPECT_TRUE(source_owner.live_.empty());
}
TEST(NativeColumnParam, decode_failure_rolls_back_all_allocations_including_large_arrays){
 ObArenaAllocator input,source_owner;ObColumnParam source(source_owner);const auto array=make_array(input,40);ASSERT_EQ(OB_SUCCESS,source.set_orig_default_value(array));DatumValue current;ASSERT_EQ(OB_SUCCESS,current.set_int(918,input));ASSERT_EQ(OB_SUCCESS,source.set_cur_default_value(current));const auto encoded=wire(source);
 int64_t successful_calls=0;
 {ColumnParamTestAllocator owner;ObColumnParam p(owner);int64_t pos=0;ASSERT_EQ(OB_SUCCESS,p.deserialize(encoded.data(),encoded.size(),pos));successful_calls=owner.calls_;EXPECT_GT(successful_calls,16);p.destroy();EXPECT_TRUE(owner.live_.empty());}
 for(int64_t failure=1;failure<=successful_calls+1;++failure){SCOPED_TRACE(failure);ColumnParamTestAllocator owner;ObColumnParam p(owner);ASSERT_EQ(OB_SUCCESS,p.set_orig_default_value(current));ASSERT_EQ(OB_SUCCESS,p.set_cur_default_value(current));const auto before=wire(p);const size_t blocks=owner.live_.size();owner.fail_at_=owner.calls_+failure;int64_t pos=0;
  const int ret=p.deserialize(encoded.data(),encoded.size(),pos);if(failure<=successful_calls){EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ret);EXPECT_EQ(before,wire(p));EXPECT_EQ(blocks,owner.live_.size());}else{EXPECT_EQ(OB_SUCCESS,ret);EXPECT_EQ(encoded,wire(p));}
  p.destroy();EXPECT_TRUE(owner.live_.empty());
 }
 source.destroy();
}
TEST(NativeColumnParam, typed_null_defaults_do_not_retain_a_payload_type){
 ColumnParamTestAllocator owner;ObColumnParam p(owner);DatumParameter value;SqlTypeMeta meta;meta.set_enum();value.set_type_meta(meta);ASSERT_TRUE(value.is_null());ASSERT_EQ(OB_SUCCESS,p.set_orig_default_value(value));ASSERT_EQ(OB_SUCCESS,p.set_cur_default_value(value));EXPECT_EQ(ObNullType,p.get_orig_default_value().get_type());EXPECT_EQ(ObNullType,p.get_cur_default_value().get_type());EXPECT_EQ(ObEnumType,value.get_type());EXPECT_TRUE(owner.live_.empty());p.destroy();
}
TEST(NativeStorageValue, scalar_copy_survives_temporary_owner_and_typed_null_is_safe){
 ObStorageDatum result;
 {ObArenaAllocator source;DatumValue value;ASSERT_EQ(OB_SUCCESS,value.set_int(918,source));ASSERT_EQ(OB_SUCCESS,result.from_value(value));EXPECT_TRUE(result.is_local_buf());EXPECT_NE(value.datum_.ptr_,result.ptr_);}
 EXPECT_EQ(918,result.get_int());DatumValue null_value;null_value.meta_.set_bit();EXPECT_EQ(OB_SUCCESS,result.from_value(null_value));EXPECT_TRUE(result.is_null());
}
int main(int argc,char **argv){::testing::InitGoogleTest(&argc,argv);if(ObCharset::init_charset()!=OB_SUCCESS)return 1;return RUN_ALL_TESTS();}
