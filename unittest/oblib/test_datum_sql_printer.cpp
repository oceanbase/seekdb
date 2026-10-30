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

#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_sql_print.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "SQL literal tests must not depend on the legacy value definition"
#endif
#include <gtest/gtest.h>
#include "lib/container/ob_se_array.h"
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
namespace {
std::string unhex(const std::string &input) {
  std::string out;
  for (size_t i=0; i<input.size(); i+=2) { out+=static_cast<char>(std::stoul(input.substr(i,2),nullptr,16)); }
  return out;
}
std::vector<std::string> columns(const std::string &line) {
  std::vector<std::string> out;
  size_t start=0, end=0;
  while ((end=line.find('\t',start))!=std::string::npos) { out.push_back(line.substr(start,end-start)); start=end+1; }
  out.push_back(line.substr(start)); return out;
}
}
TEST(DatumSqlPrinter, matches_frozen_production_outputs_without_legacy_values) {
  std::ifstream fixture("unittest/oblib/data/datum_sql_print.golden"); ASSERT_TRUE(fixture.good());
  ObArenaAllocator owner;
  std::map<int,DatumParameter> values;
  std::string line; int records=0;
  while (std::getline(fixture,line)) {
    if (line.empty() || line[0]=='#') { continue; }
    const auto fields=columns(line);
    ASSERT_GE(fields.size(),3);
    const int id=std::stoi(fields[1]);
    if (fields[0]=="S") {
      std::string wire=unhex(fields[2]); DatumParameter value; int64_t pos=0;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,owner,value.datum_,value.get_meta()));
      ASSERT_EQ(wire.size(),pos); ASSERT_TRUE(values.emplace(id,value).second);
    } else {
      ASSERT_EQ("P",fields[0]); ASSERT_EQ(6,fields.size());
      const int mode=std::stoi(fields[2]); const int expected=std::stoi(fields[3]);
      const int64_t expected_pos=std::stoll(fields[4]); const std::string text=unhex(fields[5]);
      ASSERT_EQ(expected_pos,text.size()); ASSERT_TRUE(values.count(id));
      ObValuePrintParams params;
      params.skip_escape_=(mode&1)!=0; params.print_const_expr_type_=(mode&2)!=0; params.beginning_space_=(mode&4)!=0;
      ObTimeZoneInfo zone; zone.set_offset(8*60*60);
      if ((mode&8)!=0) { params.cs_type_=CS_TYPE_BINARY; }
      params.character_hex_safe_represent_=(mode&16)!=0;
      if ((mode&32)!=0) { params.tz_info_=&zone; }
      char buffer[16384]; std::memset(buffer,0,sizeof(buffer)); int64_t pos=0;
      SCOPED_TRACE(id);
      SCOPED_TRACE(mode);
      EXPECT_EQ(expected,values.at(id).print_sql_literal(buffer,sizeof(buffer),pos,params));
      ASSERT_GE(pos,0); ASSERT_LE(pos,sizeof(buffer)); EXPECT_EQ(expected_pos,pos);
      EXPECT_EQ(text,std::string(buffer,pos)); ++records;
    }
  }
  EXPECT_EQ(62,values.size()); EXPECT_EQ(3968,records);
}
TEST(DatumSqlPrinter, rejects_invalid_buffers_and_invalid_return_sentinel) {
  ObArenaAllocator owner; DatumParameter value;
  ASSERT_EQ(OB_SUCCESS,value.set_int(42,owner));
  char bytes[32]; int64_t pos=0;
  EXPECT_EQ(OB_INVALID_ARGUMENT,value.print_sql_literal(nullptr,sizeof(bytes),pos));
  EXPECT_EQ(OB_SIZE_OVERFLOW,value.print_sql_literal(bytes,0,pos));
  pos=-1; EXPECT_EQ(OB_INVALID_ARGUMENT,value.print_sql_literal(bytes,sizeof(bytes),pos));
  pos=sizeof(bytes); EXPECT_EQ(OB_SIZE_OVERFLOW,value.print_sql_literal(bytes,sizeof(bytes),pos));
  pos=0; value.set_type(ObMaxType);
  EXPECT_EQ(OB_INVALID_ARGUMENT,value.print_sql_literal(bytes,sizeof(bytes),pos)); EXPECT_EQ(0,pos);
}
TEST(DatumSqlPrinter, fixed_point_uses_raw_value_scale_instead_of_parameter_accuracy) {
  ObArenaAllocator owner; DatumParameter value;
  int64_t raw=12345;
  value.datum_.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt*>(&raw),sizeof(raw));
  value.get_meta().set_type(ObDecimalIntType); value.get_meta().set_scale(2);
  ObAccuracy declaration; declaration.set_scale(5); declaration.set_precision(12); value.set_accuracy(declaration);
  char buffer[128]; int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,value.print_sql_literal(buffer,sizeof(buffer),pos));
  EXPECT_EQ("123.45",std::string(buffer,pos)); EXPECT_EQ(5,value.get_scale());
}
TEST(DatumSqlPrinter, every_captured_value_respects_small_output_buffer_guards) {
  std::ifstream fixture("unittest/oblib/data/datum_sql_print.golden"); ASSERT_TRUE(fixture.good());
  std::string line; ObArenaAllocator owner; int values=0;
  while (std::getline(fixture,line)) {
    if (line.compare(0,2,"S\t")!=0) { continue; }
    const auto fields=columns(line); const std::string wire=unhex(fields[2]);
    DatumParameter value; int64_t decoded=0;
    ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),decoded,owner,value.datum_,value.get_meta()));
    for (int capacity=1; capacity<=32; ++capacity) {
      std::vector<char> bytes(capacity+32,'~'); int64_t pos=0;
      SCOPED_TRACE(fields[1]);
      SCOPED_TRACE(capacity);
      value.print_sql_literal(bytes.data()+16,capacity,pos);
      EXPECT_GE(pos,0); EXPECT_LE(pos,capacity);
      EXPECT_EQ(std::string(16,'~'),std::string(bytes.data(),16));
      EXPECT_EQ(std::string(16,'~'),std::string(bytes.data()+16+capacity,16));
    }
    ++values;
  }
  EXPECT_EQ(62,values);
}
TEST(DatumSqlPrinter, timezone_charset_and_safe_character_flags_have_explicit_expected_sql) {
  ObArenaAllocator owner; DatumParameter value;
  ASSERT_EQ(OB_SUCCESS,value.set_timestamp(0,owner)); value.get_meta().set_scale(0);
  ObTimeZoneInfo zone; zone.set_offset(8*60*60); ObValuePrintParams params(&zone);
  char buffer[1024]; int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,value.print_sql_literal(buffer,sizeof(buffer),pos,params));
  EXPECT_EQ("'1970-01-01 08:00:00'",std::string(buffer,pos));
  ASSERT_EQ(OB_SUCCESS,ObCharset::init_charset());
  // This tree supports UTF8MB4 and BINARY only; exercise the real
  // cross-charset branch using these supported source/connection charsets.
  const char utf8[]={'A',static_cast<char>(0xc3),static_cast<char>(0xa9)};
  ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString(sizeof(utf8),utf8),owner));
  value.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN); pos=0;
  params.cs_type_=CS_TYPE_BINARY;
  ASSERT_EQ(OB_SUCCESS,value.print_sql_literal(buffer,sizeof(buffer),pos,params));
  // Independent legacy capture id61/mode8 is 27 41 e9 27.
  EXPECT_EQ(std::string("'A")+static_cast<char>(0xe9)+"'",std::string(buffer,pos));
  ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString::make_string("a"),owner));
  value.get_meta().set_collation_type(CS_TYPE_UTF8MB4_BIN);
  params.character_hex_safe_represent_=true; pos=0;
  ASSERT_EQ(OB_SUCCESS,value.print_sql_literal(buffer,sizeof(buffer),pos,params));
  EXPECT_EQ("CONVERT(x'61' USING utf8mb4) COLLATE utf8mb4_bin",std::string(buffer,pos));
}

TEST(DatumSqlPrinter, default_formatters_match_independent_legacy_outputs) {
  std::ifstream fixture("unittest/oblib/data/datum_default_print.golden"); ASSERT_TRUE(fixture.good());
  ObArenaAllocator owner; std::map<int,DatumParameter> values;
  ObSEArray<ObString,4> names;
  ASSERT_EQ(OB_SUCCESS,names.push_back(ObString::make_string("red")));
  ASSERT_EQ(OB_SUCCESS,names.push_back(ObString::make_string("a'b")));
  ASSERT_EQ(OB_SUCCESS,names.push_back(ObString::make_string("blue")));
  std::string line;int captured=0;
  while(std::getline(fixture,line)) {
    if(line.empty()||line[0]=='#')continue;
    const auto f=columns(line);ASSERT_GE(f.size(),3);const int id=std::stoi(f[1]);
    if(f[0]=="S") {
      const std::string wire=unhex(f[2]);DatumParameter value;int64_t pos=0;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(wire.data(),wire.size(),pos,owner,value.datum_,value.get_meta()));
      ASSERT_EQ(wire.size(),pos);ASSERT_TRUE(values.emplace(id,value).second);
    } else {
      ASSERT_TRUE(values.count(id));char buffer[16384]={0};int64_t pos=0;int ret=OB_SUCCESS;
      int expected;int64_t expected_pos;std::string expected_text;
      if(f[0]=="P") {
        ASSERT_EQ(7,f.size());const int mode=std::stoi(f[2]),plain=std::stoi(f[3]);
        ObTimeZoneInfo zone;zone.set_offset(8*60*60);ObValuePrintParams params;
        params.use_memcpy_=(mode&1)!=0;params.binary_string_print_hex_=(mode&2)!=0;
        params.binary_string_print_base64_=(mode&4)!=0;params.refine_range_max_value_=(mode&8)!=0;
        params.print_const_expr_type_=(mode&16)!=0;params.beginning_space_=(mode&16)!=0;
        params.print_null_string_value_=(mode&16)!=0;params.sql_value_type_=ObIntType;
        if(mode&32)params.cs_type_=CS_TYPE_BINARY;if(mode&64)params.tz_info_=&zone;
        pos=3;std::memcpy(buffer,"pr:",3);
        ret=plain?values.at(id).value_view().print_plain_str_literal(buffer,sizeof(buffer),pos,params)
                 :values.at(id).value_view().print_varchar_literal(buffer,sizeof(buffer),pos,params);
        expected=std::stoi(f[4]);expected_pos=std::stoll(f[5]);expected_text=unhex(f[6]);
      } else {
        ASSERT_EQ("E",f[0]);ASSERT_GE(f.size(),6);const int quoted=std::stoi(f[2]);
        ret=quoted?values.at(id).value_view().print_varchar_literal(names,buffer,sizeof(buffer),pos)
                  :values.at(id).value_view().print_plain_str_literal(names,buffer,sizeof(buffer),pos);
        expected=std::stoi(f[3]);expected_pos=std::stoll(f[4]);expected_text=unhex(f[5]);
      }
      SCOPED_TRACE(f[1]);
      SCOPED_TRACE(f[0]);
      SCOPED_TRACE(f[2]);
      EXPECT_EQ(expected,ret);EXPECT_EQ(expected_pos,pos);EXPECT_EQ(expected_text,std::string(buffer,pos));++captured;
    }
  }
  EXPECT_EQ(131,values.size());EXPECT_EQ(28456,captured);
}

TEST(DatumSqlPrinter, default_formatters_keep_small_output_canaries) {
  std::ifstream fixture("unittest/oblib/data/datum_default_print.golden");
  ASSERT_TRUE(fixture.good());
  ObArenaAllocator owner;
  std::string line;
  int values = 0;
  while (std::getline(fixture, line)) {
    if (line.compare(0, 2, "S\t") != 0) { continue; }
    const auto fields = columns(line);
    const std::string wire = unhex(fields[2]);
    DatumParameter value;
    int64_t decoded = 0;
    ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(wire.data(), wire.size(), decoded,
        owner, value.datum_, value.get_meta()));
    for (int capacity = 0; capacity <= 32; ++capacity) {
      for (int plain = 0; plain < 2; ++plain) {
        std::vector<char> bytes(capacity + 32, '~');
        int64_t pos = 0;
        SCOPED_TRACE(fields[1]);
        SCOPED_TRACE(capacity);
        SCOPED_TRACE(plain);
        if (plain) {
          value.value_view().print_plain_str_literal(bytes.data() + 16, capacity, pos);
        } else {
          value.value_view().print_varchar_literal(bytes.data() + 16, capacity, pos);
        }
        EXPECT_GE(pos, 0);
        EXPECT_LE(pos, capacity);
        EXPECT_EQ(std::string(16, '~'), std::string(bytes.data(), 16));
        EXPECT_EQ(std::string(16, '~'), std::string(bytes.data() + 16 + capacity, 16));
      }
    }
    ++values;
  }
  EXPECT_EQ(131, values);
}

int main(int argc,char **argv) {
  testing::InitGoogleTest(&argc,argv);
  if (OB_SUCCESS != ObCharset::init_charset()) { return 2; }
  return RUN_ALL_TESTS();
}
