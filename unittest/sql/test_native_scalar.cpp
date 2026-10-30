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

#include "sql/optimizer/ob_opt_est_utils.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
std::vector<std::string> fields(const std::string &line) {
  std::istringstream in(line); std::vector<std::string> result; std::string f;
  while(std::getline(in,f,'\t')) { result.push_back(f); } return result;
}
std::string unhex(const std::string &s) {
  std::string result; for(size_t i=0;i<s.size();i+=2) { result.push_back(std::stoi(s.substr(i,2),nullptr,16)); } return result;
}
std::string wire(const DatumValue &value) {
  int64_t size=0,pos=0;
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(value.datum_,value.meta_,size));
  std::string bytes(size,'\0');
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_value(value.datum_,value.meta_,&bytes[0],size,pos)); EXPECT_EQ(size,pos);
  const char *digits="0123456789abcdef"; std::string result;
  for(unsigned char c:bytes) { result+=digits[c>>4]; result+=digits[c&15]; } return result;
}
class RejectAllocator : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t,const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
}
void verify_scalar_fixture(const char *inputs,const char *expected,int input_count,int expected_count) {
  std::ifstream input(inputs); ASSERT_TRUE(input.good()); ObArenaAllocator input_owner;
  std::vector<DatumValue> values; std::string line;
  while(std::getline(input,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); if(f[0]!="V") { continue; }
    const std::string bytes=unhex(f[3]); int64_t pos=0; DatumValue value;
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,input_owner,value.datum_,value.meta_));
    ASSERT_EQ(bytes.size(),pos); values.push_back(value);
  }
  ASSERT_EQ(input_count,values.size());
  std::ifstream golden(expected); ASSERT_TRUE(golden.good()); int checked=0,mismatches=0;
  while(std::getline(golden,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); ObArenaAllocator owner; DatumValue results[4];
    const int scale=std::stoi(f[f[0]=="S"?2:7]);
    for(auto &v:results) { ASSERT_EQ(OB_SUCCESS,v.set_int(7788,owner)); v.set_scale(scale); }
    int ret=OB_SUCCESS; bool match=true;
    if(f[0]=="S") {
      ASSERT_EQ(5,f.size());
      ret=ObOptEstObjToScalar::convert_value_to_scalar(&values.at(std::stoi(f[1])),&results[0],owner);
      match=ret==std::stoi(f[3]) && wire(results[0])==f[4];
    } else {
      ASSERT_EQ(13,f.size()); ASSERT_EQ("Q",f[0]);
      const bool with_minmax=std::stoi(f[5])!=0;
      ret=ObOptEstObjToScalar::convert_values_to_scalars(
        with_minmax?&values.at(std::stoi(f[1])):nullptr,with_minmax?&values.at(std::stoi(f[2])):nullptr,
        &values.at(std::stoi(f[3])),&values.at(std::stoi(f[4])),
        with_minmax?&results[0]:nullptr,with_minmax?&results[1]:nullptr,&results[2],&results[3],owner,std::stoi(f[6])!=0);
      match=ret==std::stoi(f[8]);
      for(int i=0;i<4;++i) { match=match && wire(results[i])==f[9+i]; }
    }
    if(!match) {
      if(mismatches++<20) {
        ADD_FAILURE()<<line<<" actual ret="<<ret<<" values="<<wire(results[0])<<" "<<wire(results[1])<<" "<<wire(results[2])<<" "<<wire(results[3]);
      }
    }
    ++checked;
  }
  EXPECT_EQ(expected_count,checked); EXPECT_EQ(0,mismatches);
}
TEST(NativeScalar, original_head_numeric_string_bounds_scale_and_failure_results) {
  verify_scalar_fixture("unittest/sql/data/scalar_inputs.golden","unittest/sql/data/scalar_values.golden",104,3536);
}
TEST(NativeScalar, original_head_lob_json_and_mysql_temporal_results) {
  verify_scalar_fixture("unittest/sql/data/scalar_lob_temporal_inputs.golden","unittest/sql/data/scalar_lob_temporal.golden",49,1666);
}
TEST(NativeScalar, owned_results_survive_source_and_aliasing_and_allocation_failure_is_atomic) {
  ObArenaAllocator owner; DatumValue output;
  {
    ObArenaAllocator source_owner; DatumValue value;
    ASSERT_EQ(OB_SUCCESS,value.set_unknown(42,source_owner));
    ASSERT_EQ(OB_SUCCESS,ObOptEstObjToScalar::convert_value_to_scalar(&value,&output,owner));
    EXPECT_NE(value.datum_.ptr_,output.datum_.ptr_);
  }
  EXPECT_EQ(42,output.get_unknown());
  ASSERT_EQ(OB_SUCCESS,output.set_int(9,owner));
  ASSERT_EQ(OB_SUCCESS,ObOptEstObjToScalar::convert_value_to_scalar(&output,&output,owner));
  EXPECT_EQ(9.0,output.get_double());
  RejectAllocator reject; DatumValue input;
  ASSERT_EQ(OB_SUCCESS,input.set_int(12,owner));
  ASSERT_EQ(OB_SUCCESS,output.set_int(99,owner)); output.set_scale(2); const char *original=output.datum_.ptr_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ObOptEstObjToScalar::convert_value_to_scalar(&input,&output,reject));
  EXPECT_EQ(original,output.datum_.ptr_); EXPECT_EQ(99,output.get_int()); EXPECT_EQ(2,output.get_scale());
  EXPECT_EQ(OB_INVALID_ARGUMENT,ObOptEstObjToScalar::convert_value_to_scalar(nullptr,&output,owner));
}
int main(int argc,char **argv) {
  if(ObCharset::init_charset()!=OB_SUCCESS) { return 2; }
  ObArenaAllocator constants;
  if(ObNumberConstValue::init(constants)!=OB_SUCCESS
      || wide::ObDecimalIntConstValue::init_const_values(constants)!=OB_SUCCESS) { return 3; }
  ::testing::InitGoogleTest(&argc,argv); return RUN_ALL_TESTS();
}
