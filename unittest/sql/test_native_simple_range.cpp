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

#include "sql/optimizer/simple_range.h"
#include "common/datum/datum_parameter_codec.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native optimizer range must not require a complete legacy object value"
#endif
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
std::vector<std::string> fields(const std::string &line) {
  std::istringstream in(line); std::vector<std::string> result; std::string field;
  while(std::getline(in,field,'\t')) { result.push_back(field); } return result;
}
std::string unhex(const std::string &s) {
  std::string result;
  for(size_t i=0;i<s.size();i+=2) { result.push_back(std::stoi(s.substr(i,2),nullptr,16)); }
  return result;
}
std::string wire(const DatumValue &value) {
  int64_t size=0,pos=0;
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(value.datum_,value.meta_,size));
  std::string bytes(size,'\0');
  EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_value(value.datum_,value.meta_,&bytes[0],size,pos));
  EXPECT_EQ(size,pos);
  const char *digits="0123456789abcdef"; std::string encoded;
  for(unsigned char c:bytes) { encoded+=digits[c>>4]; encoded+=digits[c&15]; }
  return encoded;
}
}
TEST(NativeSimpleRange, frozen_legacy_bounds_intersections_copies_and_scaling) {
  std::ifstream input("unittest/sql/data/simple_range.golden"); ASSERT_TRUE(input.good());
  ObArenaAllocator owner; std::vector<DatumValue> values; std::string line;
  SimpleRange range; int checked=0;
  while(std::getline(input,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line);
    if(f[0]=="V") {
      const std::string bytes=unhex(f[2]); int64_t pos=0; DatumValue value;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,owner,value.datum_,value.meta_));
      ASSERT_EQ(bytes.size(),pos); ASSERT_EQ(values.size(),std::stoi(f[1])); values.push_back(value);
    } else {
      ASSERT_EQ(10,f.size()); SCOPED_TRACE(line);
      const int a=std::stoi(f[2]),b=std::stoi(f[3]); int result=-1;
      if(f[1]=="W") { range.set_whole_range(); }
      else if(f[1]=="F") { range.set_false_range(); }
      else if(f[1]=="B") { range.set_bound(static_cast<ObItemType>(a),values.at(b)); }
      else if(f[1]=="D") { range.set_bound(static_cast<ObItemType>(a),values.at(b).get_double()); }
      else if(f[1]=="M") { range.multiply_double(a); }
      else if(f[1]=="C") {
        SimpleRange copy(range),assigned; assigned=range; range.set_false_range(); range=assigned;
      } else if(f[1]=="S" || f[1]=="I") {
        SimpleRange other; other.set_bound(static_cast<ObItemType>(a),values.at(b));
        result=f[1]=="S" ? range.is_superset(other) : range.intersect(other);
      } else { FAIL()<<"unknown opcode"; }
      EXPECT_EQ(std::stoi(f[4]),result);
      EXPECT_EQ(f[5],wire(range.start_)); EXPECT_EQ(f[6],wire(range.end_));
      EXPECT_EQ(std::stoi(f[7])!=0,range.inclusive_start_);
      EXPECT_EQ(std::stoi(f[8])!=0,range.inclusive_end_);
      EXPECT_EQ(std::stoi(f[9])!=0,range.is_valid_range());
      ++checked;
    }
  }
  EXPECT_EQ(19,values.size()); EXPECT_EQ(3663,checked);
}
TEST(NativeSimpleRange, double_bounds_survive_copy_source_destruction_and_negative_scaling) {
  SimpleRange copied,assigned;
  {
    auto source=std::make_unique<SimpleRange>();
    source->set_bound(T_OP_GE,2.0); source->set_bound(T_OP_LE,7.0);
    copied=SimpleRange(*source); assigned=*source;
    EXPECT_NE(source->start_.datum_.ptr_,copied.start_.datum_.ptr_);
    EXPECT_NE(source->end_.datum_.ptr_,assigned.end_.datum_.ptr_);
    source->set_bound(T_OP_EQ,99.0);
  }
  EXPECT_EQ(2.0,copied.start_.get_double()); EXPECT_EQ(7.0,copied.end_.get_double());
  EXPECT_EQ(2.0,assigned.start_.get_double()); EXPECT_EQ(7.0,assigned.end_.get_double());
  copied.multiply_double(-1.0);
  EXPECT_EQ(-7.0,copied.start_.get_double()); EXPECT_EQ(-2.0,copied.end_.get_double());
  copied.set_bound(T_OP_LE,5.0);
  EXPECT_EQ(-7.0,copied.start_.get_double()); EXPECT_EQ(5.0,copied.end_.get_double());
  copied= copied;
  EXPECT_EQ(-7.0,copied.start_.get_double()); EXPECT_EQ(5.0,copied.end_.get_double());
}
TEST(NativeSimpleRange, intersected_double_endpoints_are_owned_but_text_borrows_statement_owner) {
  SimpleRange range; range.set_bound(T_OP_GE,0.0); range.set_bound(T_OP_LE,10.0);
  {
    SimpleRange other; other.set_bound(T_OP_GE,2.0); other.set_bound(T_OP_LE,7.0);
    ASSERT_TRUE(range.intersect(other)); other.set_bound(T_OP_EQ,99.0);
  }
  EXPECT_EQ(2.0,range.start_.get_double()); EXPECT_EQ(7.0,range.end_.get_double());
  ObArenaAllocator owner; DatumValue text;
  ASSERT_EQ(OB_SUCCESS,text.set_varchar(ObString::make_string("customer"),owner));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  range.set_bound(T_OP_EQ,text); SimpleRange copy(range);
  EXPECT_EQ(text.get_string_ptr(),copy.start_.get_string_ptr());
  EXPECT_EQ(ObString::make_string("customer"),copy.end_.get_string());
}
TEST(NativeSimpleRange, bound_markers_share_immutable_lifetime_and_encode_like_legacy) {
  DatumValue minimum,maximum;
  minimum.set_min_value(); maximum.set_max_value();
  EXPECT_TRUE(minimum.is_min_value()); EXPECT_TRUE(maximum.is_max_value());
  EXPECT_EQ("19070000fdffffffffffffffff01",wire(minimum));
  EXPECT_EQ("19070000feffffffffffffffff01",wire(maximum));
  const char *marker=minimum.datum_.ptr_; DatumValue other=minimum;
  ObArenaAllocator owner; ASSERT_EQ(OB_SUCCESS,other.set_int(42,owner));
  EXPECT_EQ(marker,minimum.datum_.ptr_); EXPECT_TRUE(minimum.is_min_value());
}
int main(int argc,char **argv) {
  if(ObCharset::init_charset()!=OB_SUCCESS) { return 2; }
  ::testing::InitGoogleTest(&argc,argv); return RUN_ALL_TESTS();
}
