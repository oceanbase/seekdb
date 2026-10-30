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

#include "share/datum/datum_value_cast.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/number_cast_constants.h"
#include "lib/geometry/ob_geo_wkb_define.h"
#include "lib/geometry/ob_geo_wkb_define.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native value casts and context must not require a complete legacy value"
#endif
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
namespace {
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result; std::istringstream in(line); std::string f;
  while (std::getline(in,f,'\t')) { result.push_back(f); } return result;
}
std::string unhex(const std::string &s) {
  std::string result;
  for (size_t i=0;i<s.size();i+=2) { result.push_back(std::stoi(s.substr(i,2),nullptr,16)); }
  return result;
}
std::string hex(const std::string &s) {
  const char *digits="0123456789abcdef"; std::string result;
  for (unsigned char c:s) { result+=digits[c>>4]; result+=digits[c&15]; } return result;
}
class FailAfter : public ObIAllocator {
public:
  ObArenaAllocator arena_;
  int64_t remaining_=-1;
  void *alloc(int64_t n) override {
    if (remaining_==0) { return nullptr; }
    if (remaining_>0) { --remaining_; }
    return arena_.alloc(n);
  }
  void *alloc(int64_t n,const ObMemAttr &) override { return alloc(n); }
  void free(void *p) override { arena_.free(p); }
};
}
void verify_frozen_casts(const char *fixture, int expected_count,
    const char *input_fixture = "unittest/share/data/typed_values_compare.golden", int input_count = 104) {
  ObArenaAllocator input_owner;
  std::vector<DatumValue> values;
  std::ifstream input(input_fixture);
  ASSERT_TRUE(input.good()); std::string line;
  while (std::getline(input,line)) {
    if (line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); if (f[0]!="V") { continue; }
    const std::string wire=unhex(f.at(3)); int64_t pos=0; DatumValue value;
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,input_owner,value.datum_,value.meta_));
    ASSERT_EQ(wire.size(),pos); values.push_back(value);
  }
  ASSERT_EQ(input_count,values.size());
  const ObCastMode modes[]={CM_NONE,CM_WARN_ON_FAIL,CM_WARN_ON_FAIL|CM_NULL_ON_WARN,
    CM_STRING_INTEGER_TRUNC,CM_WARN_ON_FAIL|CM_STRING_INTEGER_TRUNC,
    CM_EXPLICIT_CAST,CM_WARN_ON_FAIL|CM_EXPLICIT_CAST,CM_STRICT_MODE|CM_NO_ZERO_DATE};
  const ObCollationType collations[]={CS_TYPE_INVALID,CS_TYPE_UTF8MB4_GENERAL_CI,CS_TYPE_BINARY};
  std::ifstream expected(fixture); ASSERT_TRUE(expected.good());
  int checked=0,failed=0;
  while (std::getline(expected,line)) {
    if (line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); ASSERT_EQ(12,f.size());
    const DatumValue &source=values.at(std::stoi(f[1]));
    // Exhaust every frozen case, including unsupported extended sentinels.
    SCOPED_TRACE(line);
    ObArenaAllocator owner;
    ObTimeZoneInfo timezone; timezone.set_offset(std::stoi(f[4])*8*3600);
    ObDataTypeCastParams params(&timezone,CS_TYPE_INVALID,CS_TYPE_INVALID,CS_TYPE_UTF8MB4_BIN);
    ObAccuracy accuracy;
    if (std::stoi(f[2]) == ObDecimalIntType) { accuracy.set_precision(18); accuracy.set_scale(2); }
    ObCastCtx context(&owner,&params,modes[std::stoi(f[3])],collations[std::stoi(f[5])],std::stoi(f[6])?&accuracy:nullptr);
    DatumValue result;
    ASSERT_EQ(OB_SUCCESS,result.set_int(7788,owner));
    const char *previous=result.datum_.ptr_;
    const int ret=DatumValueCaster::to_type(static_cast<SqlValueType>(std::stoi(f[2])),context,source,owner,result);
    EXPECT_EQ(std::stoi(f[7]),ret);
    EXPECT_EQ(std::stoi(f[8]),context.warning_);
    EXPECT_EQ(std::stoull(f[9]),accuracy.get_accuracy());
    if (ret==OB_SUCCESS) {
      int64_t size=0,pos=0; ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(result.datum_,result.meta_,size));
      std::string wire(size,'\0');
      EXPECT_EQ(std::stoi(f[10]),DatumParameterCodec::encode_value(result.datum_,result.meta_,&wire[0],size,pos));
      EXPECT_EQ(size,pos); EXPECT_EQ(f[11],hex(wire));
    } else {
      ++failed; EXPECT_EQ(previous,result.datum_.ptr_); EXPECT_EQ(7788,result.get_int());
    }
    ++checked;
  }
  EXPECT_EQ(expected_count,checked); EXPECT_GT(failed,0);
}
TEST(NativeValueCast, frozen_legacy_values_accuracy_and_errors) {
  verify_frozen_casts("unittest/share/data/literal_value_cast.golden",149760);
}
TEST(NativeValueCast, frozen_legacy_lob_json_decimal_outputs) {
  verify_frozen_casts("unittest/share/data/literal_value_cast_extra.golden",44928);
}
TEST(NativeValueCast, frozen_legacy_lob_and_mysql_temporal_inputs) {
  const char *input="unittest/share/data/literal_lob_temporal_inputs.golden";
  verify_frozen_casts("unittest/share/data/literal_lob_temporal_cast.golden",70560,input,49);
  verify_frozen_casts("unittest/share/data/literal_lob_temporal_cast_extra.golden",21168,input,49);
}
TEST(NativeValueCast, result_lifetime_aliasing_and_owner_failure) {
  ObArenaAllocator destination;
  DatumValue scalar,number,text;
  {
    ObArenaAllocator temporary; DatumValue input; ASSERT_EQ(OB_SUCCESS,input.set_int(-123,temporary));
    ObCastCtx context(&temporary,nullptr,CM_NONE,CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS,DatumValueCaster::to_type(ObDoubleType,context,input,destination,scalar));
    ASSERT_EQ(OB_SUCCESS,DatumValueCaster::to_type(ObNumberType,context,input,destination,number));
    ASSERT_EQ(OB_SUCCESS,DatumValueCaster::to_type(ObVarcharType,context,input,destination,text));
  }
  EXPECT_EQ(-123.0,scalar.get_double()); EXPECT_EQ(0,number.get_number().compare(static_cast<int64_t>(-123)));
  EXPECT_EQ(ObString("-123"),text.get_string());
  DatumValue input; ASSERT_EQ(OB_SUCCESS,input.set_int(123,destination));
  ObCastCtx context(&destination,nullptr,CM_NONE,CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,DatumValueCaster::to_type(ObUInt64Type,context,input,destination,input)); EXPECT_EQ(123,input.get_uint64());
  FailAfter failure;
  ObAccuracy accuracy; accuracy.set_precision(17); accuracy.set_scale(3);
  ObCastCtx failing(&destination,nullptr,CM_NONE,CS_TYPE_UTF8MB4_BIN,&accuracy);
  const ObAccuracy before_accuracy=accuracy;
  const char *before=scalar.datum_.ptr_; failure.remaining_=0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,DatumValueCaster::to_type(ObDoubleType,failing,input,failure,scalar));
  EXPECT_EQ(before,scalar.datum_.ptr_); EXPECT_EQ(-123.0,scalar.get_double()); EXPECT_EQ(before_accuracy,accuracy);
}

TEST(NativeValueCast, hex_string_to_signed_integer_range_semantics) {
  const auto cast_hex = [](const std::string &bytes, ObCastMode mode,
                           int expected_ret, int64_t expected_value) {
    ObArenaAllocator owner;
    DatumValue input;
    ASSERT_EQ(OB_SUCCESS, input.set_string(ObHexStringType,
        ObString(static_cast<int32_t>(bytes.size()), bytes.data()), owner));
    input.meta_.set_collation_type(CS_TYPE_BINARY);
    ObCastCtx context(&owner, nullptr, mode, CS_TYPE_BINARY);
    DatumValue output;
    ASSERT_EQ(OB_SUCCESS, output.set_int(7788, owner));
    const int ret = DatumValueCaster::to_type(ObIntType, context, input, owner, output);
    EXPECT_EQ(expected_ret, ret);
    if (OB_SUCCESS == expected_ret) {
      EXPECT_EQ(expected_value, output.get_int());
    } else {
      EXPECT_EQ(7788, output.get_int());
    }
  };

  cast_hex(std::string("\x7f", 1), CM_NONE, OB_SUCCESS, 127);
  cast_hex(std::string("\xff\xff\xff\xff\xff\xff\xff\xff", 8),
      CM_NONE, OB_DATA_OUT_OF_RANGE, 0);
  cast_hex(std::string("\xff\xff\xff\xff\xff\xff\xff\xff", 8),
      CM_EXPLICIT_CAST, OB_SUCCESS, -1);
  cast_hex(std::string("\xff\xff\xff\xff\xff\xff\xff\xff", 8),
      CM_NO_RANGE_CHECK, OB_SUCCESS, -1);
  cast_hex(std::string("\x01\0\0\0\0\0\0\0\0", 9), CM_NONE, OB_SUCCESS, 0);
  cast_hex(std::string(), CM_NONE, OB_SUCCESS, 0);
}
TEST(NativeValueCast, same_type_lob_preserves_native_payload_metadata) {
  ObArenaAllocator source_owner;
  ObArenaAllocator result_owner;

  // Locator bytes and the header flag travel together. The same-type cast
  // must own its result and retain all metadata without decoding the locator.
  const char lob_bytes[] = "native-lob-payload";
  DatumValue lob;
  lob.meta_.set_blob();
  lob.meta_.set_has_lob_header();
  lob.datum_.set_string(lob_bytes, sizeof(lob_bytes) - 1);
  lob.datum_.set_has_lob_header();
  DatumValue lob_result;
  ObCastCtx lob_context(&source_owner, nullptr, CM_NONE, CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObLongTextType, lob_context, lob, result_owner, lob_result));
  EXPECT_EQ(ObLongTextType, lob_result.get_type());
  EXPECT_TRUE(lob_result.meta_.has_lob_header());
  EXPECT_TRUE(lob_result.datum_.has_lob_header());
  EXPECT_EQ(ObString(sizeof(lob_bytes) - 1, lob_bytes), lob_result.get_string());
  EXPECT_NE(lob.datum_.ptr_, lob_result.datum_.ptr_);
}
TEST(NativeValueCast, decimal_requires_accuracy_and_owns_result) {
  ObArenaAllocator destination;
  DatumValue result;
  ASSERT_EQ(OB_SUCCESS,result.set_int(7788,destination));
  {
    ObArenaAllocator temporary;
    DatumValue input;
    ASSERT_EQ(OB_SUCCESS,input.set_varchar(ObString("123.25"),temporary));
    ObCastCtx context(&temporary,nullptr,CM_NONE,CS_TYPE_UTF8MB4_BIN);
    const char *previous=result.datum_.ptr_;
    EXPECT_EQ(OB_ERR_UNEXPECTED,DatumValueCaster::to_type(ObDecimalIntType,context,input,destination,result));
    EXPECT_EQ(previous,result.datum_.ptr_); EXPECT_EQ(7788,result.get_int());
    ObAccuracy accuracy; accuracy.set_precision(18); accuracy.set_scale(2);
    context.res_accuracy_=&accuracy;
    ASSERT_EQ(OB_SUCCESS,DatumValueCaster::to_type(ObDecimalIntType,context,input,destination,result));
  }
  EXPECT_TRUE(result.is_decimal_int()); EXPECT_EQ(2,result.get_scale());
  ASSERT_EQ(8,result.get_int_bytes());
  int64_t coefficient=0; MEMCPY(&coefficient,result.datum_.ptr_,sizeof(coefficient));
  EXPECT_EQ(12325,coefficient);
}
TEST(NativeValueCast, numeric_datetime_interval_uses_native_datum)
{
  ObArenaAllocator owner;
  ObCastCtx context(&owner, nullptr, CM_NONE, CS_TYPE_INVALID);
  ObAccuracy accuracy;
  context.res_accuracy_ = &accuracy;
  DatumValue input, output;

  ASSERT_EQ(OB_SUCCESS, input.set_int(2, owner));
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_datetime_interval(
      ObDateTimeType, context, input, output));
  EXPECT_EQ(2 * USECS_PER_DAY, output.get_datetime());
  EXPECT_EQ(MIN_SCALE_FOR_TEMPORAL, accuracy.get_scale());

  ASSERT_EQ(OB_SUCCESS, input.set_float(1.5f, owner));
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_datetime_interval(
      ObDateTimeType, context, input, output));
  EXPECT_EQ(static_cast<int64_t>(static_cast<double>(1.5f) * USECS_PER_DAY),
      output.get_datetime());
  EXPECT_EQ(-1, accuracy.get_scale());

  ASSERT_EQ(OB_SUCCESS, input.set_double(-0.25, owner));
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_datetime_interval(
      ObDateTimeType, context, input, output));
  EXPECT_EQ(static_cast<int64_t>(-0.25 * USECS_PER_DAY), output.get_datetime());

  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("1.5", owner));
  ASSERT_EQ(OB_SUCCESS, input.set_number(number, owner));
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_datetime_interval(
      ObDateTimeType, context, input, output));
  EXPECT_EQ(static_cast<int64_t>(1.5 * USECS_PER_DAY), output.get_datetime());
  EXPECT_EQ(MIN_SCALE_FOR_TEMPORAL, accuracy.get_scale());

  ASSERT_EQ(OB_SUCCESS, input.set_int(7, owner));
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_datetime_interval(
      ObIntType, context, input, output));
  EXPECT_EQ(static_cast<int64_t>(1.5 * USECS_PER_DAY), output.get_datetime());
}
TEST(NativeValueCast, geometry_to_varchar_strips_version_without_obj_bridge)
{
  ObArenaAllocator owner;
  char wkb[] = {0, 0, 0, 0, 0x41, 1, 1, 0, 0, 0, 7, 8, 9};
  DatumValue geometry;
  geometry.meta_.set_geometry();
  geometry.datum_.set_string(ObString(sizeof(wkb), wkb));
  ObCastCtx context(&owner, nullptr, CM_NONE, CS_TYPE_BINARY);
  ObAccuracy accuracy;
  context.res_accuracy_ = &accuracy;
  DatumValue result;
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObVarcharType, context, geometry, owner, result));
  const ObString converted = result.get_string();
  ASSERT_EQ(sizeof(wkb) - 1, converted.length());
  EXPECT_EQ(0, MEMCMP(wkb, converted.ptr(), WKB_GEO_SRID_SIZE));
  EXPECT_EQ(0, MEMCMP(wkb + WKB_OFFSET, converted.ptr() + WKB_GEO_SRID_SIZE,
      sizeof(wkb) - WKB_OFFSET));
  EXPECT_EQ(static_cast<int32_t>(sizeof(wkb) - 1), accuracy.get_length());

  DatumValue unchanged_geometry;
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObGeometryType, context, geometry, owner, unchanged_geometry));
  EXPECT_TRUE(unchanged_geometry.is_geometry());
  EXPECT_EQ(geometry.get_string(), unchanged_geometry.get_string());
  EXPECT_EQ(OB_NOT_SUPPORTED, DatumValueCaster::to_type(
      ObJsonType, context, geometry, owner, result));

  char short_wkb[] = {0, 0, 0, 0, 0x41};
  geometry.datum_.set_string(ObString(sizeof(short_wkb), short_wkb));
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObVarcharType, context, geometry, owner, result));
}
TEST(NativeValueCast, nonnumeric_truth_matches_frozen_legacy_number_casts) {
  ObArenaAllocator owner;
  std::vector<DatumValue> values;
  std::string line;
  std::ifstream inputs("unittest/share/data/typed_values_compare.golden");
  ASSERT_TRUE(inputs.good());
  while (std::getline(inputs,line)) {
    if (line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); if (f[0]!="V") { continue; }
    const std::string wire=unhex(f[3]); int64_t pos=0; DatumValue value;
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,owner,value.datum_,value.meta_));
    values.push_back(value);
  }
  const ObCastMode modes[]={CM_NONE,CM_WARN_ON_FAIL,CM_WARN_ON_FAIL|CM_NULL_ON_WARN,
    CM_STRING_INTEGER_TRUNC,CM_WARN_ON_FAIL|CM_STRING_INTEGER_TRUNC,
    CM_EXPLICIT_CAST,CM_WARN_ON_FAIL|CM_EXPLICIT_CAST,CM_STRICT_MODE|CM_NO_ZERO_DATE};
  std::ifstream oracle("unittest/share/data/literal_value_cast.golden"); ASSERT_TRUE(oracle.good());
  int checked=0;
  while (std::getline(oracle,line)) {
    if (line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line);
    if (std::stoi(f[2])!=ObNumberType || f[4]!="0" || f[5]!="0" || f[6]!="0") { continue; }
    const DatumValue &input=values.at(std::stoi(f[1]));
    if (input.is_null() || input.is_numeric_type()) { continue; }
    SCOPED_TRACE(line);
    int expected_ret=std::stoi(f[7]); bool expected_truth=false;
    if (expected_ret==OB_SUCCESS) {
      const std::string wire=unhex(f[11]); int64_t pos=0; DatumValue number;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,owner,number.datum_,number.meta_));
      if (number.is_null() || !number.is_number()) { expected_ret=OB_ERR_UNEXPECTED; }
      else { expected_truth=!number.get_number().is_zero(); }
    }
    bool truth=true, falsehood=true;
    EXPECT_EQ(expected_ret,DatumValueEvaluator::is_true(input,modes[std::stoi(f[3])],truth));
    EXPECT_EQ(expected_ret,DatumValueEvaluator::is_false(input,modes[std::stoi(f[3])],falsehood));
    EXPECT_EQ(expected_truth,truth);
    EXPECT_EQ(expected_ret==OB_SUCCESS && !expected_truth,falsehood);
    ++checked;
  }
  EXPECT_EQ(360,checked);
  DatumValue null;
  bool truth=true, falsehood=true;
  EXPECT_EQ(OB_SUCCESS,DatumValueEvaluator::is_true(null,truth));
  EXPECT_EQ(OB_SUCCESS,DatumValueEvaluator::is_false(null,falsehood));
  EXPECT_FALSE(truth); EXPECT_FALSE(falsehood);
}
TEST(NativeValueCast, frozen_relational_comparison_types_collations_and_warnings) {
  ObArenaAllocator input_owner; std::vector<DatumValue> values; std::string line;
  std::ifstream input("unittest/share/data/typed_values_compare.golden"); ASSERT_TRUE(input.good());
  while(std::getline(input,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); if(f[0]!="V") { continue; }
    const std::string wire=unhex(f.at(3)); int64_t pos=0; DatumValue value;
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,input_owner,value.datum_,value.meta_));
    ASSERT_EQ(wire.size(),pos); values.push_back(value);
  }
  ASSERT_EQ(104,values.size());
  const ObCastMode modes[]={CM_NONE,CM_WARN_ON_FAIL,CM_WARN_ON_FAIL|CM_NULL_ON_WARN};
  const ObCollationType collations[]={CS_TYPE_UTF8MB4_GENERAL_CI,CS_TYPE_BINARY};
  std::ifstream expected("unittest/share/data/relational_value_compare.golden"); ASSERT_TRUE(expected.good());
  int checked=0,mismatches=0;
  while(std::getline(expected,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line); ASSERT_EQ(10,f.size());
    ObArenaAllocator owner; ObTimeZoneInfo timezone; timezone.set_offset(std::stoi(f[5])*8*3600);
    ObDataTypeCastParams params(&timezone,CS_TYPE_INVALID,CS_TYPE_INVALID,CS_TYPE_UTF8MB4_BIN);
    ObCastCtx context(&owner,&params,modes[std::stoi(f[4])],collations[std::stoi(f[6])]); int cmp=77;
    const int ret=DatumValueComparator::compare(values.at(std::stoi(f[1])),values.at(std::stoi(f[2])),
        context,static_cast<SqlValueType>(std::stoi(f[3])),collations[std::stoi(f[6])],cmp);
    if(ret!=std::stoi(f[7]) || context.warning_!=std::stoi(f[8]) || cmp!=std::stoi(f[9])) {
      if(mismatches++<20) {
        ADD_FAILURE()<<line<<" actual ret="<<ret<<" warning="<<context.warning_<<" cmp="<<cmp;
      }
    }
    ++checked;
  }
  EXPECT_EQ(636540,checked); EXPECT_EQ(0,mismatches);
}

TEST(NativeValueCast, relational_equality_matches_frozen_direct_pairs) {
  ObArenaAllocator input_owner; std::vector<DatumValue> values; std::string line;
  std::ifstream input("unittest/share/data/typed_values_compare.golden"); ASSERT_TRUE(input.good());
  int checked=0;
  while(std::getline(input,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line);
    if(f[0]=="V") {
      const std::string wire=unhex(f.at(3)); int64_t pos=0; DatumValue value;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,input_owner,value.datum_,value.meta_));
      ASSERT_EQ(wire.size(),pos); values.push_back(value);
    } else if(f[0]=="P" && f[4]=="1") {
      DatumValue left=values.at(std::stoi(f[2])),right=values.at(std::stoi(f[3]));
      if(f[1]=="1") { left.set_collation_type(CS_TYPE_BINARY); right.set_collation_type(CS_TYPE_BINARY); }
      ObArenaAllocator owner; ObCastCtx context(&owner,nullptr,CM_WARN_ON_FAIL,CS_TYPE_INVALID);
      // Direct equality must preserve unordered mixed floating-point NaN,
      // rather than treating every zero three-way result as equal.
      context.warning_=OB_INVALID_ARGUMENT; bool equal=false;
      const int ret=DatumValueComparator::equal(left,right,context,ObDoubleType,CS_TYPE_INVALID,equal);
      EXPECT_EQ(std::stoi(f[7]),ret)<<line;
      EXPECT_EQ(std::stoi(f[8])!=0,equal)<<line;
      EXPECT_EQ(OB_INVALID_ARGUMENT,context.warning_)<<line;
      ++checked;
    }
  }
  EXPECT_GT(checked,1000);
}

int main(int argc,char **argv) {
  ::testing::InitGoogleTest(&argc,argv);
  if (ObCharset::init_charset()!=OB_SUCCESS) { return 1; }
  ObArenaAllocator constants;
  if (ObNumberConstValue::init(constants)!=OB_SUCCESS
      || wide::ObDecimalIntConstValue::init_const_values(constants)!=OB_SUCCESS) { return 2; }
  return RUN_ALL_TESTS();
}
