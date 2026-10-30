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

#include "share/datum/datum_value_set.h"
#include "common/datum/datum_parameter_codec.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
using namespace oceanbase::common;
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
struct ConstantHasher {
  int operator()(const DatumValue &,uint64_t &hash) const { hash=0; return OB_SUCCESS; }
};
struct ErrorHasher {
  int operator()(const DatumValue &,uint64_t &) const { return OB_ERR_UNEXPECTED; }
};
}
TEST(DatumValueSet, per_column_dedup_matches_independent_legacy_hash_and_equality_fixture) {
  std::ifstream input("unittest/share/data/typed_values_compare.golden"); ASSERT_TRUE(input.good());
  ObArenaAllocator owner; std::string line; std::vector<DatumValue> values;
  std::vector<uint64_t> hashes;
  std::map<std::pair<int,int>,bool> equality;
  std::map<std::pair<int,int>,std::vector<int>> groups;
  while(std::getline(input,line)) {
    if(line.empty() || line[0]=='#') { continue; }
    const auto f=fields(line);
    if(f[0]=="V") {
      DatumValue value; const std::string bytes=unhex(f[3]); int64_t pos=0;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,owner,value.datum_,value.meta_));
      ASSERT_EQ(bytes.size(),pos); ASSERT_EQ(values.size(),std::stoi(f[1]));
      values.push_back(value); ASSERT_EQ(OB_SUCCESS,std::stoi(f[4])); hashes.push_back(std::stoull(f[5]));
      if(std::stoi(f[1])!=59) { // the old pair fixture excludes its aborting empty DECIMAL
        groups[{value.get_type(),value.get_collation_type()}].push_back(std::stoi(f[1]));
      }
    } else if(f[0]=="P" && f[1]=="0" && f[4]=="1") {
      ASSERT_EQ(OB_SUCCESS,std::stoi(f[7])); equality[{std::stoi(f[2]),std::stoi(f[3])}]=std::stoi(f[8])!=0;
    }
  }
  ASSERT_EQ(104,values.size()); int checked=0,duplicates=0;
  for(const auto &group:groups) {
    DatumValueSet<> set; ASSERT_EQ(OB_SUCCESS,set.create(5,"NativeSetTest","NativeSetTest"));
    std::vector<int> seen;
    for(int index:group.second) {
      bool expected_inserted=true;
      for(int previous:seen) {
        if(hashes.at(previous)==hashes.at(index)) {
          const auto pair=equality.find({previous,index});
          ASSERT_NE(equality.end(),pair);
          if(pair->second) { expected_inserted=false; break; }
        }
      }
      bool inserted=false;
      ASSERT_EQ(OB_SUCCESS,set.insert(values.at(index),inserted));
      EXPECT_EQ(expected_inserted,inserted)<<index;
      if(expected_inserted) { seen.push_back(index); } else { ++duplicates; }
      EXPECT_EQ(seen.size(),set.size()); ++checked;
    }
    for(auto it=group.second.rbegin();it!=group.second.rend();++it) {
      bool inserted=true; ASSERT_EQ(OB_SUCCESS,set.insert(values.at(*it),inserted)); EXPECT_FALSE(inserted)<<*it;
    }
  }
  EXPECT_EQ(103,checked); EXPECT_GT(duplicates,0);
}
TEST(DatumValueSet, collisions_and_entry_relocation_keep_distinct_values_and_deduplicate_repeats) {
  ObArenaAllocator owner; DatumValueSet<ConstantHasher> set;
  ASSERT_EQ(OB_SUCCESS,set.create(1,"NativeSetTest","NativeSetTest"));
  std::vector<DatumValue> values;
  for(int64_t i=0;i<64;++i) {
    DatumValue value; ASSERT_EQ(OB_SUCCESS,value.set_int(i,owner)); values.push_back(value);
    bool inserted=false; ASSERT_EQ(OB_SUCCESS,set.insert(value,inserted)); EXPECT_TRUE(inserted);
  }
  EXPECT_EQ(64,set.size());
  for(auto it=values.rbegin();it!=values.rend();++it) {
    bool inserted=true; ASSERT_EQ(OB_SUCCESS,set.insert(*it,inserted)); EXPECT_FALSE(inserted);
  }
  EXPECT_EQ(64,set.size());
}
TEST(DatumValueSet, comparison_and_hash_failures_leave_the_set_unchanged) {
  ObArenaAllocator owner; DatumValue number,text;
  ASSERT_EQ(OB_SUCCESS,number.set_int(1,owner));
  ASSERT_EQ(OB_SUCCESS,text.set_varchar(ObString::make_string("one"),owner));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  DatumValueSet<ConstantHasher> set; ASSERT_EQ(OB_SUCCESS,set.create(1,"NativeSetTest","NativeSetTest"));
  bool inserted=false; ASSERT_EQ(OB_SUCCESS,set.insert(number,inserted)); ASSERT_TRUE(inserted);
  EXPECT_EQ(OB_NOT_SUPPORTED,set.insert(text,inserted)); EXPECT_FALSE(inserted); EXPECT_EQ(1,set.size());
  ASSERT_EQ(OB_SUCCESS,set.insert(number,inserted)); EXPECT_FALSE(inserted);
  DatumValueSet<ErrorHasher> bad_hash;
  EXPECT_EQ(OB_NOT_INIT,bad_hash.insert(number,inserted));
  ASSERT_EQ(OB_SUCCESS,bad_hash.create(1,"NativeSetTest","NativeSetTest"));
  EXPECT_EQ(OB_ERR_UNEXPECTED,bad_hash.insert(number,inserted)); EXPECT_FALSE(inserted); EXPECT_EQ(0,bad_hash.size());
}
TEST(DatumValueSet, case_insensitive_and_binary_keys_use_their_own_collation) {
  ObArenaAllocator owner;
  for(ObCollationType cs:{CS_TYPE_UTF8MB4_GENERAL_CI,CS_TYPE_UTF8MB4_BIN}) {
    DatumValueSet<ConstantHasher> set; ASSERT_EQ(OB_SUCCESS,set.create(1,"NativeSetTest","NativeSetTest"));
    int expected=0;
    for(const char *s:{"a","A","b","a"}) {
      DatumValue value; ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString::make_string(s),owner)); value.set_collation_type(cs);
      bool inserted=false; ASSERT_EQ(OB_SUCCESS,set.insert(value,inserted)); expected+=inserted;
    }
    EXPECT_EQ(cs==CS_TYPE_UTF8MB4_GENERAL_CI?2:3,expected); EXPECT_EQ(expected,set.size());
  }
}
TEST(DatumValueSet, destroy_and_recreate_clear_entries_and_report_not_initialized) {
  ObArenaAllocator owner; DatumValue value;
  ASSERT_EQ(OB_SUCCESS,value.set_int(42,owner));
  DatumValueSet<> set; bool inserted=false;
  ASSERT_EQ(OB_SUCCESS,set.create(1,"NativeSetTest","NativeSetTest"));
  ASSERT_TRUE(set.created()); ASSERT_EQ(OB_SUCCESS,set.insert(value,inserted)); ASSERT_TRUE(inserted);
  ASSERT_EQ(OB_SUCCESS,set.destroy()); EXPECT_FALSE(set.created()); EXPECT_EQ(0,set.size());
  EXPECT_EQ(OB_NOT_INIT,set.insert(value,inserted));
  ASSERT_EQ(OB_SUCCESS,set.create(1,"NativeSetTest","NativeSetTest"));
  ASSERT_EQ(OB_SUCCESS,set.insert(value,inserted)); EXPECT_TRUE(inserted); EXPECT_EQ(1,set.size());
}
int main(int argc,char **argv) {
  if(ObCharset::init_charset()!=OB_SUCCESS) { return 2; }
  ::testing::InitGoogleTest(&argc,argv); return RUN_ALL_TESTS();
}
