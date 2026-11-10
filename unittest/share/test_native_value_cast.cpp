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
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(wire.data(),wire.size(),pos,input_owner,value.datum_,value.meta_));
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
    const Datum previous=result.datum_;
    const int ret=DatumValueCaster::to_type(static_cast<SqlValueType>(std::stoi(f[2])),context,source,owner,result);
    EXPECT_EQ(std::stoi(f[7]),ret);
    EXPECT_EQ(std::stoi(f[8]),context.warning_);
    EXPECT_EQ(std::stoull(f[9]),accuracy.get_accuracy());
    if (ret==OB_SUCCESS) {
      int64_t size=0,pos=0; ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::native_value_size(result.datum_,result.meta_,size));
      std::string wire(size,'\0');
      EXPECT_EQ(std::stoi(f[10]),DatumParameterCodec::encode_native_value(result.datum_,result.meta_,&wire[0],size,pos));
      EXPECT_EQ(size,pos); EXPECT_EQ(f[11],hex(wire));
    } else {
      ++failed; EXPECT_EQ(0,MEMCMP(&previous,&result.datum_,sizeof(previous))); EXPECT_EQ(7788,result.get_int());
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
  const Datum before=scalar.datum_; failure.remaining_=0;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,DatumValueCaster::to_type(ObNumberType,failing,input,failure,scalar));
  EXPECT_EQ(0,MEMCMP(&before,&scalar.datum_,sizeof(before))); EXPECT_EQ(-123.0,scalar.get_double()); EXPECT_EQ(before_accuracy,accuracy);
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
  EXPECT_NE(lob.datum_.get_reference(), lob_result.datum_.get_reference());
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
    const Datum previous=result.datum_;
    EXPECT_EQ(OB_ERR_UNEXPECTED,DatumValueCaster::to_type(ObDecimalIntType,context,input,destination,result));
    EXPECT_EQ(0,MEMCMP(&previous,&result.datum_,sizeof(previous))); EXPECT_EQ(7788,result.get_int());
    ObAccuracy accuracy; accuracy.set_precision(18); accuracy.set_scale(2);
    context.res_accuracy_=&accuracy;
    ASSERT_EQ(OB_SUCCESS,DatumValueCaster::to_type(ObDecimalIntType,context,input,destination,result));
  }
  EXPECT_TRUE(result.is_decimal_int()); EXPECT_EQ(2,result.get_scale());
  ASSERT_EQ(8,result.get_int_bytes());
  int64_t coefficient=0; MEMCPY(&coefficient,result.datum_.get_reference(),sizeof(coefficient));
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
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(wire.data(),wire.size(),pos,owner,value.datum_,value.meta_));
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
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(wire.data(),wire.size(),pos,owner,number.datum_,number.meta_));
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
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(wire.data(),wire.size(),pos,input_owner,value.datum_,value.meta_));
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
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(wire.data(),wire.size(),pos,input_owner,value.datum_,value.meta_));
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

TEST(NativeValueCast, integer_to_geometry_preserves_legacy_cast_modes_and_errors)
{
  ObArenaAllocator owner;
  const SqlValueType types[] = {
    ObTinyIntType, ObSmallIntType, ObMediumIntType, ObInt32Type, ObIntType
  };
  // All values fit every signed input width. Their decimal representation is
  // shorter than the four-byte SRID, so no external SRS provider is needed.
  const int64_t values[] = {0, 1, -1, 127};
  const ObCastMode modes[] = {
    CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT,
    CM_COLUMN_CONVERT | CM_WARN_ON_FAIL | CM_NULL_ON_WARN
  };
  for (SqlValueType type : types) {
    SCOPED_TRACE(static_cast<int>(type));
    for (int64_t value : values) {
      SCOPED_TRACE(value);
      for (ObCastMode mode : modes) {
        SCOPED_TRACE(mode);
        DatumValue input, output;
        ASSERT_EQ(OB_SUCCESS, input.set_int(type, value, owner));
        ASSERT_EQ(OB_SUCCESS, output.set_varchar(ObString::make_string("keep"), owner));
        const Datum before = output.datum_;
        const SqlTypeMeta before_meta = output.meta_;
        ObAccuracy accuracy(19, 2);
        const uint64_t before_accuracy = accuracy.get_accuracy();
        ObCastCtx context(&owner, nullptr, mode, CS_TYPE_BINARY, &accuracy);
        const int expected = CM_IS_COLUMN_CONVERT(mode)
            ? OB_ERR_CANT_CREATE_GEOMETRY_OBJECT : OB_ERR_GIS_INVALID_DATA;
        EXPECT_EQ(expected, DatumValueCaster::to_type(
            ObGeometryType, context, input, owner, output));
        EXPECT_EQ(OB_SUCCESS, context.warning_);
        EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
        EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));
        EXPECT_EQ(0, MEMCMP(&before_meta, &output.meta_, sizeof(before_meta)));
        EXPECT_EQ(ObString::make_string("keep"), output.get_string());
        EXPECT_EQ(type, input.get_type());
        EXPECT_EQ(value, input.get_int());
      }
    }
    DatumValue null_input, null_output;
    ASSERT_EQ(OB_SUCCESS, null_input.set_int(type, 0, owner));
    null_input.datum_.set_null();
    ASSERT_EQ(OB_SUCCESS, null_output.set_int(71, owner));
    ObCastCtx null_context(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObGeometryType, null_context, null_input, owner, null_output));
    EXPECT_TRUE(null_output.is_null());
    EXPECT_EQ(type, null_output.get_type());
  }

  DatumValue input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_int(0, owner));
  ASSERT_EQ(OB_SUCCESS, output.set_int(71, owner));
  // The old mapping to the GEOMETRY-field error applies only to a generic
  // GEOMETRY column, not an explicit POINT target encoded in the cast mode.
  ObCastMode point_mode = CM_NONE;
  CM_SET_GEOMETRY_POINT(point_mode);
  point_mode |= CM_COLUMN_CONVERT;
  ObCastCtx point_context(&owner, nullptr, point_mode, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_GIS_INVALID_DATA, DatumValueCaster::to_type(
      ObGeometryType, point_context, input, owner, output));
  EXPECT_EQ(71, output.get_int());

  ObCastCtx missing_owner(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObGeometryType, missing_owner, input, owner, output));
  EXPECT_EQ(71, output.get_int());

  ObCastCtx alias_context(&owner, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_CANT_CREATE_GEOMETRY_OBJECT, DatumValueCaster::to_type(
      ObGeometryType, alias_context, input, owner, input));
  EXPECT_EQ(ObIntType, input.get_type());
  EXPECT_EQ(0, input.get_int());
}

TEST(NativeValueCast, binary_wkb_to_geometry_owns_versioned_payload_and_preserves_subtype_cast)
{
  // The two exact successful UPDATE inputs from geometry_dml_mysql.test.
  const std::string point = unhex("000000000101000000000000000000f03f0000000000000840");
  const std::string point_z = unhex("0000000001e9030000000000000000f03f00000000000000400000000000000840");
  const std::string versioned_point = unhex("00000000410101000000000000000000f03f0000000000000840");
  const std::string versioned_point_z = unhex("000000004101e9030000000000000000f03f00000000000000400000000000000840");
  const std::string inputs[] = {point, point_z, versioned_point};
  const std::string expected[] = {versioned_point, versioned_point_z, versioned_point};
  const SqlValueType kinds[] = {ObHexStringType, ObVarcharType, ObCharType};
  const ObCastMode modes[] = {CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT};
  for (SqlValueType kind : kinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    for (int sample = 0; sample < 3; ++sample) {
      SCOPED_TRACE(sample);
      for (ObCastMode mode : modes) {
        SCOPED_TRACE(mode);
        ObArenaAllocator destination;
        DatumValue output;
        {
          ObArenaAllocator temporary;
          DatumValue input;
          ASSERT_EQ(OB_SUCCESS, input.set_string(kind,
              ObString(static_cast<int32_t>(inputs[sample].size()), inputs[sample].data()), temporary));
          input.meta_.set_collation_type(CS_TYPE_BINARY);
          ObCastCtx context(&temporary, nullptr, mode, CS_TYPE_BINARY);
          ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
              ObGeometryType, context, input, destination, output));
          EXPECT_EQ(OB_SUCCESS, context.warning_);
          EXPECT_EQ(kind, input.get_type());
          EXPECT_EQ(ObString(static_cast<int32_t>(inputs[sample].size()), inputs[sample].data()), input.get_string());
          EXPECT_NE(input.datum_.get_reference(), output.datum_.get_reference());
        }
        ASSERT_TRUE(output.is_geometry());
        ASSERT_TRUE(output.has_lob_header());
        ObString payload;
        ASSERT_EQ(OB_SUCCESS, output.get_string(payload));
        EXPECT_EQ(ObString(static_cast<int32_t>(expected[sample].size()), expected[sample].data()), payload);
        // The public reverse conversion independently exercises the in-row
        // LOB header and removes only the geometry version byte.
        DatumValue raw;
        ObCastCtx read_context(&destination, nullptr, CM_NONE, CS_TYPE_BINARY);
        ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
            ObVarcharType, read_context, output, destination, raw));
        const std::string &original = sample == 1 ? point_z : point;
        EXPECT_EQ(ObString(static_cast<int32_t>(original.size()), original.data()), raw.get_string());
      }
    }
  }

  ObArenaAllocator owner;
  DatumValue input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_string(ObHexStringType,
      ObString(static_cast<int32_t>(point.size()), point.data()), owner));
  input.meta_.set_collation_type(CS_TYPE_BINARY);
  ObCastMode point_mode = CM_NONE;
  CM_SET_GEOMETRY_POINT(point_mode);
  point_mode |= CM_COLUMN_CONVERT;
  ObCastCtx point_context(&owner, nullptr, point_mode, CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObGeometryType, point_context, input, owner, output));
  ASSERT_TRUE(output.is_geometry());
  ObString point_payload;
  ASSERT_EQ(OB_SUCCESS, output.get_string(point_payload));
  EXPECT_EQ(ObString(static_cast<int32_t>(versioned_point.size()), versioned_point.data()), point_payload);

  ObCastCtx alias_context(&owner, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObGeometryType, alias_context, input, owner, input));
  ASSERT_TRUE(input.is_geometry());
  ObString alias_payload;
  ASSERT_EQ(OB_SUCCESS, input.get_string(alias_payload));
  EXPECT_EQ(ObString(static_cast<int32_t>(versioned_point.size()), versioned_point.data()), alias_payload);
}

TEST(NativeValueCast, binary_wkb_geometry_failures_keep_output_and_null_semantics)
{
  ObArenaAllocator source_owner;
  const SqlValueType kinds[] = {ObHexStringType, ObVarcharType, ObCharType};
  const ObCastMode modes[] = {CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT,
      CM_COLUMN_CONVERT | CM_WARN_ON_FAIL | CM_NULL_ON_WARN};
  const std::string invalid[] = {std::string(), std::string("x"), unhex("0000000001")};
  for (SqlValueType kind : kinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    for (const std::string &bytes : invalid) {
      SCOPED_TRACE(hex(bytes));
      for (ObCastMode mode : modes) {
        SCOPED_TRACE(mode);
        DatumValue input, output;
        ASSERT_EQ(OB_SUCCESS, input.set_string(kind,
            ObString(static_cast<int32_t>(bytes.size()), bytes.data()), source_owner));
        input.meta_.set_collation_type(CS_TYPE_BINARY);
        ASSERT_EQ(OB_SUCCESS, output.set_int(71, source_owner));
        ObAccuracy accuracy(19, 2);
        const uint64_t before_accuracy = accuracy.get_accuracy();
        ObCastCtx context(&source_owner, nullptr, mode, CS_TYPE_BINARY, &accuracy);
        const int expected = CM_IS_COLUMN_CONVERT(mode)
            ? OB_ERR_CANT_CREATE_GEOMETRY_OBJECT : OB_ERR_GIS_INVALID_DATA;
        EXPECT_EQ(expected, DatumValueCaster::to_type(
            ObGeometryType, context, input, source_owner, output));
        EXPECT_EQ(ObIntType, output.get_type());
        EXPECT_EQ(71, output.get_int());
        EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
        EXPECT_EQ(OB_SUCCESS, context.warning_);
      }
    }
    DatumValue typed_null, output;
    ASSERT_EQ(OB_SUCCESS, typed_null.set_string(kind, ObString::make_string("unused"), source_owner));
    typed_null.datum_.set_null();
    ObCastCtx no_context_owner(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObGeometryType, no_context_owner, typed_null, source_owner, output));
    EXPECT_TRUE(output.is_null());
    EXPECT_EQ(kind, output.get_type());
  }

  const std::string point = unhex("000000000101000000000000000000f03f0000000000000840");
  DatumValue input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_string(ObHexStringType,
      ObString(static_cast<int32_t>(point.size()), point.data()), source_owner));
  input.meta_.set_collation_type(CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, output.set_int(71, source_owner));
  ObCastCtx no_owner(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObGeometryType, no_owner, input, source_owner, output));
  EXPECT_EQ(71, output.get_int());

  FailAfter destination;
  destination.remaining_ = 0;
  ObAccuracy accuracy(19, 2);
  const uint64_t before_accuracy = accuracy.get_accuracy();
  ObCastCtx context(&source_owner, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY, &accuracy);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumValueCaster::to_type(
      ObGeometryType, context, input, destination, output));
  EXPECT_EQ(ObIntType, output.get_type());
  EXPECT_EQ(71, output.get_int());
  EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
  destination.remaining_ = -1;
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObGeometryType, context, input, destination, output));
  EXPECT_TRUE(output.is_geometry());
}

namespace {
int make_native_inner_cast_input(SqlValueType kind, uint64_t numeric,
                                const ObString &text, ObIAllocator &owner,
                                DatumValue &output)
{
  int ret = OB_SUCCESS;
  ObString label = text;
  ObEnumSetInnerValue inner(numeric, label);
  const int64_t size = inner.get_serialize_size();
  char *bytes = static_cast<char *>(owner.alloc(size));
  int64_t pos = 0;
  if (bytes == nullptr) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(inner.serialize(bytes, size, pos))) {
  } else {
    Datum datum;
    SqlTypeMeta meta;
    if (kind == ObEnumInnerType) { meta.set_enum_inner(); }
    else { meta.set_set_inner(); }
    meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    if (OB_FAIL(datum.set_enumset_inner(bytes, static_cast<uint32_t>(pos)))) {
    } else {
      ret = output.assign_native_borrowed(datum, meta);
    }
  }
  return ret;
}
}

TEST(NativeValueCast, enumset_inner_restores_numeric_and_text_cast_families)
{
  const SqlValueType kinds[] = {ObEnumInnerType, ObSetInnerType};
  const SqlValueType numeric_targets[] = {
    ObIntType, ObUInt64Type, ObFloatType, ObDoubleType,
    ObNumberType, ObYearType, ObBitType, ObDecimalIntType
  };
  const SqlValueType text_targets[] = {
    ObDateTimeType, ObDateType, ObTimeType, ObVarcharType, ObMySQLDateType
  };
  for (SqlValueType kind : kinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    for (SqlValueType target : numeric_targets) {
      SCOPED_TRACE(static_cast<int>(target));
      ObArenaAllocator destination;
      DatumValue output;
      {
        ObArenaAllocator source_owner;
        DatumValue input;
        // The deliberately nonnumeric label distinguishes numeric extraction
        // from reparsing text, for all eight legacy numeric target families.
        ASSERT_EQ(OB_SUCCESS, make_native_inner_cast_input(kind, 2024,
            ObString::make_string("not-a-number"), source_owner, input));
        ObAccuracy accuracy(18, 2);
        ObCastCtx context(&source_owner, nullptr, CM_NONE, CS_TYPE_UTF8MB4_BIN, &accuracy);
        ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
            target, context, input, destination, output));
        EXPECT_EQ(OB_SUCCESS, context.warning_);
        EXPECT_EQ(kind, input.get_type());
        MEMSET(const_cast<void *>(input.datum_.get_reference()), 0xa5, input.datum_.length());
        source_owner.reset();
      }
      ASSERT_EQ(target, output.get_type());
      switch (target) {
        case ObIntType: EXPECT_EQ(2024, output.get_int()); break;
        case ObUInt64Type: EXPECT_EQ(2024U, output.get_uint64()); break;
        case ObFloatType: EXPECT_EQ(2024.0f, output.get_float()); break;
        case ObDoubleType: EXPECT_EQ(2024.0, output.get_double()); break;
        case ObNumberType:
          EXPECT_EQ(0, output.get_number().compare(static_cast<int64_t>(2024))); break;
        case ObYearType: EXPECT_EQ(124U, output.get_year()); break;
        case ObBitType: EXPECT_EQ(2024U, output.get_bit()); break;
        case ObDecimalIntType: {
          ASSERT_EQ(8, output.get_int_bytes());
          EXPECT_EQ(2, output.get_scale());
          int64_t coefficient = 0;
          MEMCPY(&coefficient, output.get_decimal_int(), sizeof(coefficient));
          EXPECT_EQ(202400, coefficient);
          break;
        }
        default: FAIL();
      }
    }
    for (SqlValueType target : text_targets) {
      SCOPED_TRACE(static_cast<int>(target));
      ObArenaAllocator destination;
      DatumValue output;
      const ObString text = target == ObTimeType ? ObString::make_string("12:34:56")
          : target == ObVarcharType ? ObString::make_string("enum-label")
          : ObString::make_string("1970-01-02 00:00:00");
      {
        ObArenaAllocator source_owner;
        DatumValue input;
        // This unrelated numeric index must not become the temporal/string value.
        ASSERT_EQ(OB_SUCCESS, make_native_inner_cast_input(kind, 99, text, source_owner, input));
        ObTimeZoneInfo timezone;
        timezone.set_offset(0);
        ObDataTypeCastParams params(&timezone, CS_TYPE_INVALID, CS_TYPE_INVALID, CS_TYPE_UTF8MB4_BIN);
        ObCastCtx context(&source_owner, &params, CM_NONE, CS_TYPE_UTF8MB4_BIN);
        ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
            target, context, input, destination, output));
        EXPECT_EQ(OB_SUCCESS, context.warning_);
        MEMSET(const_cast<void *>(input.datum_.get_reference()), 0xa5, input.datum_.length());
        source_owner.reset();
      }
      ASSERT_EQ(target, output.get_type());
      switch (target) {
        case ObDateTimeType: EXPECT_EQ(86400000000LL, output.get_datetime()); break;
        case ObDateType: EXPECT_EQ(1, output.get_date()); break;
        case ObTimeType: EXPECT_EQ(45296000000LL, output.get_time()); break;
        case ObVarcharType:
          EXPECT_EQ(text, output.get_string());
          EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, output.get_collation_type());
          EXPECT_EQ(CS_LEVEL_COERCIBLE, output.get_collation_level());
          break;
        case ObMySQLDateType:
          // ObMySQLDate packs year/month/day in 14/4/5 bits.
          EXPECT_EQ((1970 << 9) | (1 << 5) | 2, output.get_mysql_date().date_);
          break;
        default: FAIL();
      }
    }
  }
}

TEST(NativeValueCast, enumset_inner_identity_null_alias_and_failure_ownership)
{
  const SqlValueType kinds[] = {ObEnumInnerType, ObSetInnerType};
  const ObString label = ObString::make_string("retained-inner-label");
  for (SqlValueType kind : kinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    ObArenaAllocator destination;
    DatumValue owned;
    {
      ObArenaAllocator source_owner;
      DatumValue input;
      ASSERT_EQ(OB_SUCCESS, make_native_inner_cast_input(kind, 42, label, source_owner, input));
      ObCastCtx context(&source_owner, nullptr, CM_NONE, CS_TYPE_UTF8MB4_BIN);
      ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(kind, context, input, destination, owned));
      EXPECT_NE(input.datum_.get_reference(), owned.datum_.get_reference());
      MEMSET(const_cast<void *>(input.datum_.get_reference()), 0xa5, input.datum_.length());
      source_owner.reset();
    }
    ObEnumSetInnerValue decoded;
    ASSERT_EQ(OB_SUCCESS, owned.datum_.get_enumset_inner(decoded));
    EXPECT_EQ(42U, decoded.numberic_value_);
    EXPECT_EQ(label, decoded.string_value_);
    EXPECT_EQ(kind, owned.get_type());
    EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, owned.get_collation_type());

    ObArenaAllocator source_owner;
    DatumValue input, output;
    ASSERT_EQ(OB_SUCCESS, make_native_inner_cast_input(kind, 42, label, source_owner, input));
    ASSERT_EQ(OB_SUCCESS, output.set_int(71, source_owner));
    FailAfter failure;
    failure.remaining_ = 0;
    ObAccuracy accuracy(19, 2);
    const uint64_t before_accuracy = accuracy.get_accuracy();
    ObCastCtx failing(&source_owner, nullptr, CM_NONE, CS_TYPE_UTF8MB4_BIN, &accuracy);
    const Datum before = output.datum_;
    const SqlTypeMeta before_meta = output.meta_;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumValueCaster::to_type(
        ObVarcharType, failing, input, failure, output));
    EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));
    EXPECT_EQ(0, MEMCMP(&before_meta, &output.meta_, sizeof(before_meta)));
    EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
    failure.remaining_ = -1;
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObVarcharType, failing, input, failure, output));
    EXPECT_EQ(label, output.get_string());
    failure.remaining_ = 0;
    ObCastCtx scalar_context(&source_owner, nullptr, CM_NONE, CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObUInt64Type, scalar_context, input, failure, output));
    EXPECT_EQ(42U, output.get_uint64());

    DatumValue typed_null = input;
    typed_null.datum_.set_null();
    ObCastCtx no_owner(nullptr, nullptr, CM_NONE, CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObIntType, no_owner, typed_null, failure, output));
    EXPECT_TRUE(output.is_null());
    EXPECT_EQ(kind, output.get_type());

    // This legacy table entry called the DATETIME-only guard and already
    // failed; restoring legal edges must not silently admit MYSQL_DATETIME.
    ASSERT_EQ(OB_SUCCESS, output.set_int(71, source_owner));
    EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
        ObMySQLDateTimeType, scalar_context, input, destination, output));
    EXPECT_EQ(71, output.get_int());

    // A version-only compatibility image has an omitted length/body and
    // legally decodes to defaults. Retain the real complete header instead,
    // so its declared nonempty body is genuinely truncated.
    const uint32_t header_length = 1 + serialization::OB_SERIALIZE_SIZE_NEED_BYTES;
    ASSERT_GT(input.datum_.length(), header_length);
    Datum truncated;
    ASSERT_EQ(OB_SUCCESS, truncated.set_enumset_inner(
        static_cast<const char *>(input.datum_.get_reference()), header_length));
    DatumValue malformed(truncated, input.meta_);
    const int ret = DatumValueCaster::to_type(
        ObIntType, scalar_context, malformed, destination, output);
    EXPECT_NE(OB_SUCCESS, ret);
    EXPECT_NE(OB_NOT_SUPPORTED, ret); // The real decoder, rather than family denial, fails.
    EXPECT_EQ(OB_DESERIALIZE_ERROR, ret);
    EXPECT_EQ(71, output.get_int());

    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObVarcharType, scalar_context, input, destination, input));
    EXPECT_EQ(ObVarcharType, input.get_type());
    EXPECT_EQ(label, input.get_string());
  }
}

TEST(NativeValueCast, text_wkb_to_geometry_uses_payload_and_owns_result)
{
  const std::string point = unhex("000000000101000000000000000000f03f0000000000000840");
  const std::string versioned = unhex("00000000410101000000000000000000f03f0000000000000840");
  const SqlValueType kinds[] = {ObTinyTextType, ObLongTextType};
  const ObCastMode modes[] = {CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT};
  for (SqlValueType kind : kinds) {
    SCOPED_TRACE(static_cast<int>(kind));
    for (ObCastMode mode : modes) {
      SCOPED_TRACE(mode);
      ObArenaAllocator destination;
      DatumValue output;
      {
        ObArenaAllocator source_owner;
        DatumValue input;
        if (kind == ObTinyTextType) {
          ASSERT_EQ(OB_SUCCESS, input.set_string(kind,
              ObString(static_cast<int32_t>(point.size()), point.data()), source_owner));
          input.meta_.set_collation_type(CS_TYPE_BINARY);
          ASSERT_FALSE(input.has_lob_header());
        } else {
          // Use the real public cast to create an in-row LOB. Its locator
          // header must be removed before the bytes reach WKB validation.
          DatumValue string_input;
          ASSERT_EQ(OB_SUCCESS, string_input.set_string(ObVarcharType,
              ObString(static_cast<int32_t>(point.size()), point.data()), source_owner));
          string_input.meta_.set_collation_type(CS_TYPE_BINARY);
          ObCastCtx text_context(&source_owner, nullptr, CM_NONE, CS_TYPE_BINARY);
          ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
              kind, text_context, string_input, source_owner, input));
          ASSERT_TRUE(input.has_lob_header());
        }
        ASSERT_EQ(kind, input.get_type());
        ObString input_payload;
        ASSERT_EQ(OB_SUCCESS, input.get_string(input_payload));
        ASSERT_EQ(ObString(static_cast<int32_t>(point.size()), point.data()), input_payload);
        ObCastCtx context(&source_owner, nullptr, mode, CS_TYPE_BINARY);
        ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
            ObGeometryType, context, input, destination, output));
        EXPECT_EQ(OB_SUCCESS, context.warning_);
        EXPECT_NE(input.datum_.get_reference(), output.datum_.get_reference());
        MEMSET(const_cast<char *>(input_payload.ptr()), 0x5a, input_payload.length());
      }
      ASSERT_TRUE(output.is_geometry());
      ASSERT_TRUE(output.has_lob_header());
      ObString payload;
      ASSERT_EQ(OB_SUCCESS, output.get_string(payload));
      EXPECT_EQ(ObString(static_cast<int32_t>(versioned.size()), versioned.data()), payload);
      DatumValue raw;
      ObCastCtx read_context(&destination, nullptr, CM_NONE, CS_TYPE_BINARY);
      ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
          ObVarcharType, read_context, output, destination, raw));
      EXPECT_EQ(ObString(static_cast<int32_t>(point.size()), point.data()), raw.get_string());
    }
  }

  ObArenaAllocator owner;
  DatumValue aliased;
  ASSERT_EQ(OB_SUCCESS, aliased.set_string(ObTinyTextType,
      ObString(static_cast<int32_t>(point.size()), point.data()), owner));
  aliased.meta_.set_collation_type(CS_TYPE_BINARY);
  ObCastCtx context(&owner, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObGeometryType, context, aliased, owner, aliased));
  ObString payload;
  ASSERT_EQ(OB_SUCCESS, aliased.get_string(payload));
  EXPECT_EQ(ObString(static_cast<int32_t>(versioned.size()), versioned.data()), payload);
}

TEST(NativeValueCast, text_geometry_failure_null_and_owner_retry_preserve_output)
{
  ObArenaAllocator source_owner;
  const ObCastMode modes[] = {CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT,
      CM_COLUMN_CONVERT | CM_WARN_ON_FAIL | CM_NULL_ON_WARN};
  const std::string invalid[] = {std::string(), std::string("x"), unhex("0000000001")};
  for (const std::string &bytes : invalid) {
    SCOPED_TRACE(hex(bytes));
    for (ObCastMode mode : modes) {
      SCOPED_TRACE(mode);
      DatumValue input, output;
      ASSERT_EQ(OB_SUCCESS, input.set_string(ObTinyTextType,
          ObString(static_cast<int32_t>(bytes.size()), bytes.data()), source_owner));
      input.meta_.set_collation_type(CS_TYPE_BINARY);
      ASSERT_EQ(OB_SUCCESS, output.set_int(71, source_owner));
      const Datum before = output.datum_;
      const SqlTypeMeta before_meta = output.meta_;
      ObAccuracy accuracy(19, 2);
      const uint64_t before_accuracy = accuracy.get_accuracy();
      ObCastCtx context(&source_owner, nullptr, mode, CS_TYPE_BINARY, &accuracy);
      EXPECT_EQ(CM_IS_COLUMN_CONVERT(mode) ? OB_ERR_CANT_CREATE_GEOMETRY_OBJECT : OB_ERR_GIS_INVALID_DATA,
          DatumValueCaster::to_type(ObGeometryType, context, input, source_owner, output));
      EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));
      EXPECT_EQ(0, MEMCMP(&before_meta, &output.meta_, sizeof(before_meta)));
      EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
      EXPECT_EQ(OB_SUCCESS, context.warning_);
    }
  }
  for (SqlValueType kind : {ObTinyTextType, ObLongTextType}) {
    DatumValue input, output;
    input.meta_.set_type(kind);
    input.meta_.set_collation_type(CS_TYPE_BINARY);
    input.datum_.set_null();
    ObCastCtx no_owner(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObGeometryType, no_owner, input, source_owner, output));
    EXPECT_TRUE(output.is_null());
    EXPECT_EQ(kind, output.get_type());
    EXPECT_EQ(CS_TYPE_BINARY, output.get_collation_type());
  }

  const std::string point = unhex("000000000101000000000000000000f03f0000000000000840");
  DatumValue input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_string(ObTinyTextType,
      ObString(static_cast<int32_t>(point.size()), point.data()), source_owner));
  input.meta_.set_collation_type(CS_TYPE_BINARY);
  ASSERT_EQ(OB_SUCCESS, output.set_int(71, source_owner));
  ObCastCtx no_owner(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObGeometryType, no_owner, input, source_owner, output));
  EXPECT_EQ(71, output.get_int());

  FailAfter destination;
  destination.remaining_ = 0;
  ObAccuracy accuracy(19, 2);
  const uint64_t before_accuracy = accuracy.get_accuracy();
  ObCastCtx context(&source_owner, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY, &accuracy);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, DatumValueCaster::to_type(
      ObGeometryType, context, input, destination, output));
  EXPECT_EQ(ObIntType, output.get_type());
  EXPECT_EQ(71, output.get_int());
  EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
  destination.remaining_ = -1;
  ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
      ObGeometryType, context, input, destination, output));
  EXPECT_TRUE(output.is_geometry());
}

namespace {
void verify_scalar_geometry_error_contract(const DatumValue &input, int validation_error)
{
  SCOPED_TRACE(static_cast<int>(input.get_type()));
  ObArenaAllocator owner;
  const Datum input_before = input.datum_;
  const SqlTypeMeta input_meta_before = input.meta_;
  const ObCastMode modes[] = {CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT,
      CM_COLUMN_CONVERT | CM_WARN_ON_FAIL | CM_NULL_ON_WARN};
  for (ObCastMode mode : modes) {
    SCOPED_TRACE(mode);
    DatumValue output;
    ASSERT_EQ(OB_SUCCESS, output.set_varchar(ObString::make_string("keep"), owner));
    const Datum before = output.datum_;
    const SqlTypeMeta before_meta = output.meta_;
    ObAccuracy accuracy(19, 2);
    const uint64_t before_accuracy = accuracy.get_accuracy();
    ObCastCtx context(&owner, nullptr, mode, CS_TYPE_BINARY, &accuracy);
    context.warning_ = OB_ERR_UNEXPECTED;
    EXPECT_EQ(CM_IS_COLUMN_CONVERT(mode) ? OB_ERR_CANT_CREATE_GEOMETRY_OBJECT : validation_error,
        DatumValueCaster::to_type(ObGeometryType, context, input, owner, output));
    EXPECT_EQ(OB_SUCCESS, context.warning_);
    EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
    EXPECT_EQ(0, MEMCMP(&before, &output.datum_, sizeof(before)));
    EXPECT_EQ(0, MEMCMP(&before_meta, &output.meta_, sizeof(before_meta)));
    EXPECT_EQ(ObString::make_string("keep"), output.get_string());
    EXPECT_EQ(0, MEMCMP(&input_before, &input.datum_, sizeof(input_before)));
    EXPECT_EQ(0, MEMCMP(&input_meta_before, &input.meta_, sizeof(input_meta_before)));
  }

  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(71, owner));
  ObCastMode point_mode = CM_NONE;
  CM_SET_GEOMETRY_POINT(point_mode);
  point_mode |= CM_COLUMN_CONVERT;
  ObCastCtx point_context(&owner, nullptr, point_mode, CS_TYPE_BINARY);
  EXPECT_EQ(validation_error, DatumValueCaster::to_type(
      ObGeometryType, point_context, input, owner, output));
  EXPECT_EQ(71, output.get_int());

  ObCastCtx no_owner(nullptr, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_UNEXPECTED, DatumValueCaster::to_type(
      ObGeometryType, no_owner, input, owner, output));
  EXPECT_EQ(71, output.get_int());

  DatumValue aliased = input;
  ObCastCtx alias_context(&owner, nullptr, CM_COLUMN_CONVERT, CS_TYPE_BINARY);
  EXPECT_EQ(OB_ERR_CANT_CREATE_GEOMETRY_OBJECT, DatumValueCaster::to_type(
      ObGeometryType, alias_context, aliased, owner, aliased));
  EXPECT_EQ(0, MEMCMP(&input_before, &aliased.datum_, sizeof(input_before)));
  EXPECT_EQ(0, MEMCMP(&input_meta_before, &aliased.meta_, sizeof(input_meta_before)));

  DatumValue typed_null = input;
  typed_null.datum_.set_null();
  for (ObCastMode mode : modes) {
    ObCastCtx null_context(nullptr, nullptr, mode, CS_TYPE_BINARY);
    ASSERT_EQ(OB_SUCCESS, DatumValueCaster::to_type(
        ObGeometryType, null_context, typed_null, owner, output));
    EXPECT_TRUE(output.is_null());
    EXPECT_EQ(0, MEMCMP(&input_meta_before, &output.meta_, sizeof(input_meta_before)));
  }
}
}

TEST(NativeValueCast, numeric_geometry_preserves_each_source_formatter_and_errors)
{
  ObArenaAllocator owner;
  DatumValue input;
  const SqlValueType uint_kinds[] = {
      ObUTinyIntType, ObUSmallIntType, ObUMediumIntType, ObUInt32Type, ObUInt64Type};
  for (SqlValueType kind : uint_kinds) {
    ASSERT_EQ(OB_SUCCESS, input.set_uint(kind, 127, owner));
    // "127" cannot contain a four-byte SRID. In contrast, UINT64_MAX's
    // unsigned decimal text is long enough and needs an actual SRS provider.
    verify_scalar_geometry_error_contract(input, OB_ERR_GIS_INVALID_DATA);
  }
  ASSERT_EQ(OB_SUCCESS, input.set_uint(ObUInt64Type, UINT64_MAX, owner));
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);

  ASSERT_EQ(OB_SUCCESS, input.set_float(0.0f, owner));
  input.meta_.set_scale(0);
  verify_scalar_geometry_error_contract(input, OB_ERR_GIS_INVALID_DATA);
  ASSERT_EQ(OB_SUCCESS, input.set_float(-1.25f, owner));
  input.meta_.set_scale(2);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  const uint32_t float_bits[] = {UINT32_C(0x7f800000), UINT32_C(0xff800000), UINT32_C(0x7fc00000)};
  const int special_errors[] = {OB_ERR_GIS_INVALID_DATA, OB_NOT_INIT, OB_ERR_GIS_INVALID_DATA};
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(i);
    float value = 0;
    MEMCPY(&value, &float_bits[i], sizeof(value));
    ASSERT_EQ(OB_SUCCESS, input.set_float(value, owner));
    // The legacy spellings are "Inf", "-Inf" and "Nan" respectively.
    verify_scalar_geometry_error_contract(input, special_errors[i]);
  }

  ASSERT_EQ(OB_SUCCESS, input.set_double(0.0, owner));
  input.meta_.set_scale(0);
  verify_scalar_geometry_error_contract(input, OB_ERR_GIS_INVALID_DATA);
  ASSERT_EQ(OB_SUCCESS, input.set_double(-1.25, owner));
  input.meta_.set_scale(2);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  const uint64_t double_bits[] = {
      UINT64_C(0x7ff0000000000000), UINT64_C(0xfff0000000000000), UINT64_C(0x7ff8000000000000)};
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(i);
    double value = 0;
    MEMCPY(&value, &double_bits[i], sizeof(value));
    ASSERT_EQ(OB_SUCCESS, input.set_double(value, owner));
    verify_scalar_geometry_error_contract(input, special_errors[i]);
  }

  number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("0", owner));
  ASSERT_EQ(OB_SUCCESS, input.set_number(number, owner));
  input.meta_.set_scale(0);
  verify_scalar_geometry_error_contract(input, OB_ERR_GIS_INVALID_DATA);
  ASSERT_EQ(OB_SUCCESS, number.from("123.45", owner));
  ASSERT_EQ(OB_SUCCESS, input.set_number(number, owner));
  input.meta_.set_scale(2);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_number(ObUNumberType, number, owner));
  input.meta_.set_scale(2);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);

  ASSERT_EQ(OB_SUCCESS, input.set_uint(ObBitType, 7, owner));
  verify_scalar_geometry_error_contract(input, OB_ERR_GIS_INVALID_DATA);
  ASSERT_EQ(OB_SUCCESS, input.set_uint(ObBitType, UINT64_MAX, owner));
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
}

TEST(NativeValueCast, temporal_geometry_preserves_calendar_and_packed_domains)
{
  ObArenaAllocator owner;
  DatumValue input;
  // Each temporal formatter emits at least four nonzero ASCII bytes. These
  // inputs therefore reach the old missing-SRS-provider error, unlike an
  // incorrect shared integer formatter that would turn zero into "0".
  ASSERT_EQ(OB_SUCCESS, input.set_datetime(0, owner));
  input.meta_.set_scale(0);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_timestamp(0, owner));
  input.meta_.set_scale(0);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_date(0, owner));
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_time(0, owner));
  input.meta_.set_scale(0);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_time(-INT64_C(183845500000), owner));
  input.meta_.set_scale(6);
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_year(0, owner));
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ASSERT_EQ(OB_SUCCESS, input.set_year(124, owner));
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);

  ObMySQLDate date;
  date.year_ = 2024;
  date.month_ = 7;
  date.day_ = 8;
  ASSERT_EQ(OB_SUCCESS, input.set_mysql_date(date, owner));
  EXPECT_EQ(date, input.get_mysql_date());
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
  ObMySQLDateTime datetime;
  datetime.year_month_ = ObMySQLDateTime::year_month(2024, 7);
  datetime.day_ = 8;
  datetime.hour_ = 9;
  datetime.minute_ = 10;
  datetime.second_ = 11;
  datetime.microseconds_ = 123456;
  ASSERT_EQ(OB_SUCCESS, input.set_mysql_datetime(datetime, owner));
  input.meta_.set_scale(6);
  EXPECT_EQ(datetime, input.get_mysql_datetime());
  verify_scalar_geometry_error_contract(input, OB_NOT_INIT);
}

TEST(NativeValueCast, decimal_geometry_preserves_coefficient_width_scale_and_format_errors)
{
  ObArenaAllocator owner;
  struct Sample {
    int64_t coefficient;
    ObScale scale;
    int validation_error;
  };
  // Exact old wide formatter boundaries: "1", "-1", "0.01", "-1.23", "1000".
  const Sample samples[] = {
      {1, 0, OB_ERR_GIS_INVALID_DATA}, {-1, 0, OB_ERR_GIS_INVALID_DATA},
      {1, 2, OB_NOT_INIT}, {-123, 2, OB_NOT_INIT}, {1, -3, OB_NOT_INIT}};
  for (int32_t width : {4, 8, 16, 32, 64}) {
    SCOPED_TRACE(width);
    for (const Sample &sample : samples) {
      SCOPED_TRACE(sample.coefficient);
      SCOPED_TRACE(sample.scale);
      int512_t coefficient(sample.coefficient);
      DatumValue input;
      ASSERT_EQ(OB_SUCCESS, input.set_decimal_int(width, sample.scale,
          reinterpret_cast<const ObDecimalInt *>(&coefficient), owner));
      coefficient = 777;
      EXPECT_EQ(width, input.get_int_bytes());
      EXPECT_EQ(sample.scale, input.get_scale());
      verify_scalar_geometry_error_contract(input, sample.validation_error);
    }
  }
  DatumValue empty_coefficient;
  ASSERT_EQ(OB_SUCCESS, empty_coefficient.set_decimal_int(0, 2, nullptr, owner));
  verify_scalar_geometry_error_contract(empty_coefficient, OB_ERR_GIS_INVALID_DATA);

  // The old Decimal helper formats before SRS lookup and before the generic
  // column-geometry error remapping. A formatter error must stay intact.
  const int512_t coefficient = int512_t(1) << 500;
  DatumValue input, output;
  ASSERT_EQ(OB_SUCCESS, input.set_decimal_int(sizeof(coefficient), -128,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), owner));
  ASSERT_EQ(-128, input.get_scale());
  for (ObCastMode mode : {CM_NONE, CM_EXPLICIT_CAST, CM_COLUMN_CONVERT}) {
    SCOPED_TRACE(mode);
    ASSERT_EQ(OB_SUCCESS, output.set_int(71, owner));
    ObAccuracy accuracy(19, 2);
    const uint64_t before_accuracy = accuracy.get_accuracy();
    ObCastCtx context(&owner, nullptr, mode, CS_TYPE_BINARY, &accuracy);
    EXPECT_EQ(OB_SIZE_OVERFLOW, DatumValueCaster::to_type(
        ObGeometryType, context, input, owner, output));
    EXPECT_EQ(ObIntType, output.get_type());
    EXPECT_EQ(71, output.get_int());
    EXPECT_EQ(before_accuracy, accuracy.get_accuracy());
  }
}
