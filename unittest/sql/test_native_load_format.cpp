/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "common/ob_hex_utils_base.h"
#include "query/engine/cmd/ob_load_data_parser.h"
#include "lib/allocator/page_arena.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
class CountingAllocator final : public ObIAllocator {
public:
  explicit CountingAllocator(int64_t limit=INT64_MAX) : limit_(limit), calls_(0), sizes_(), ptrs_() {}
  ~CountingAllocator() { for (void *ptr : ptrs_) std::free(ptr); }
  void *alloc(int64_t size) override {
    sizes_.push_back(size);
    if (calls_++ >= limit_) return nullptr;
    void *ptr = std::malloc(size); if (ptr != nullptr) ptrs_.push_back(ptr); return ptr;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *ptr) override {
    for (auto it=ptrs_.begin();it!=ptrs_.end();++it) if (*it==ptr) { std::free(ptr);ptrs_.erase(it);return; }
  }
  int64_t limit_,calls_; std::vector<int64_t> sizes_; std::vector<void *> ptrs_;
};
int decode(const ObString &text, ObIAllocator &alloc, ObString &result, bool &valid) {
  return ObHexUtilsBase::unhex(text, alloc, result, valid);
}
void expect_bytes(const ObString &value, const std::string &expected) {
  ASSERT_EQ(expected.size(), static_cast<size_t>(value.length()));
  if (!expected.empty()) { ASSERT_NE(nullptr,value.ptr()); EXPECT_EQ(expected,std::string(value.ptr(),value.length())); }
}
}
TEST(NativeLoadFormat, unhex_accepts_empty_odd_even_and_binary_bytes) {
  struct Case { const char *hex; std::string expected; };
  const Case cases[] = {
    {"",std::string()}, {"0",std::string("\0",1)}, {"A",std::string("\x0a",1)},
    {"abc",std::string("\x0a\xbc",2)}, {"004100FF",std::string("\0A\0\xff",4)},
    {"0123456789abcdefABCDEF",std::string("\x01\x23\x45\x67\x89\xab\xcd\xef\xab\xcd\xef",11)}
  };
  for (const Case &c : cases) {
    CountingAllocator owner; ObString result; bool valid=false;
    ASSERT_EQ(OB_SUCCESS,decode(ObString::make_string(c.hex),owner,result,valid));
    ASSERT_TRUE(valid); ASSERT_NE(nullptr,result.ptr()); ASSERT_EQ(1,owner.calls_);
    EXPECT_EQ(std::max<int64_t>(1,(static_cast<int64_t>(std::strlen(c.hex))+1)/2),owner.sizes_[0]);
    expect_bytes(result,c.expected);
  }
}
TEST(NativeLoadFormat, malformed_and_embedded_zero_are_null_without_error) {
  const std::vector<std::string> invalid={"G", "0G", "1z34", "12 3", std::string("A\0",2)};
  for (const std::string &value : invalid) {
    CountingAllocator owner; ObString result; bool valid=true;
    ASSERT_EQ(OB_SUCCESS,decode(ObString(value.size(),value.data()),owner,result,valid));
    EXPECT_FALSE(valid);EXPECT_EQ(nullptr,result.ptr());EXPECT_EQ(1,owner.calls_);
  }
}
TEST(NativeLoadFormat, oom_sets_null_even_for_empty) {
  for (const char *input : {"", "1234", "Q"}) {
    CountingAllocator owner(0); ObString result; bool valid=true;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,decode(ObString::make_string(input),owner,result,valid));
    EXPECT_FALSE(valid);EXPECT_EQ(nullptr,result.ptr());EXPECT_EQ(1,owner.calls_);
    EXPECT_EQ(std::max<int64_t>(1,(static_cast<int64_t>(std::strlen(input))+1)/2),owner.sizes_[0]);
  }
}
TEST(NativeLoadFormat, load_format_preserves_empty_invalid_and_decoded_owned_strings) {
  CountingAllocator owner;
  ObExternalFileFormat format;
  format.csv_format_.field_term_str_=ObString::make_string("keep-field");
  format.origin_file_format_str_.origin_field_term_str_=ObString::make_string("keep-origin");
  std::string json=R"({"TYPE":"CSV","LINE_DELIMITER":"004100FF","FIELD_DELIMITER":"xyz","NULL_IF_EXETERNAL":["61","GG","","0062"],"FILE_EXTENSION":"","ORIGIN_LINE_DELIMITER":"F","ORIGIN_FIELD_DELIMITER":"0G","ORIGIN_ESCAPE":"00","ORIGIN_FIELD_OPTIONALLY_ENCLOSED_BY":"CDEF","ORIGIN_NULL_IF_EXETERNAL":""})";
  ASSERT_EQ(OB_SUCCESS,format.load_from_string(ObString(json.size(),json.data()),owner));
  json.assign(json.size(),'x');  // The parser's temporary JSON arena is already destroyed too.
  expect_bytes(format.csv_format_.line_term_str_,std::string("\0A\0\xff",4));
  expect_bytes(format.csv_format_.field_term_str_,"keep-field");
  ASSERT_EQ(4,format.csv_format_.null_if_.count());
  expect_bytes(format.csv_format_.null_if_.at(0),"a");
  expect_bytes(format.csv_format_.null_if_.at(1),"");ASSERT_NE(nullptr,format.csv_format_.null_if_.at(1).ptr());
  expect_bytes(format.csv_format_.null_if_.at(2),std::string("\0b",2));
  expect_bytes(format.csv_format_.null_if_.at(3),"");
  expect_bytes(format.csv_format_.file_extension_,"");ASSERT_NE(nullptr,format.csv_format_.file_extension_.ptr());
  expect_bytes(format.origin_file_format_str_.origin_line_term_str_,std::string("\x0f",1));
  expect_bytes(format.origin_file_format_str_.origin_field_term_str_,"keep-origin");
  expect_bytes(format.origin_file_format_str_.origin_field_escaped_str_,std::string("\0",1));
  expect_bytes(format.origin_file_format_str_.origin_field_enclosed_str_,std::string("\xcd\xef",2));
  expect_bytes(format.origin_file_format_str_.origin_null_if_str_,"");ASSERT_NE(nullptr,format.origin_file_format_str_.origin_null_if_str_.ptr());
}
TEST(NativeLoadFormat, format_first_decode_oom_does_not_publish) {
  CountingAllocator owner(0);ObExternalFileFormat format;
  format.csv_format_.line_term_str_=ObString::make_string("keep-line");
  const char *json=R"({"TYPE":"CSV","LINE_DELIMITER":""})";
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,format.load_from_string(ObString::make_string(json),owner));
  expect_bytes(format.csv_format_.line_term_str_,"keep-line");ASSERT_EQ(1,owner.calls_);EXPECT_EQ(1,owner.sizes_[0]);
}

TEST(NativeLoadFormat, second_decode_oom_keeps_first_published_value) {
  CountingAllocator owner(1);ObExternalFileFormat format;
  format.csv_format_.line_term_str_=ObString::make_string("old-line");
  format.csv_format_.field_term_str_=ObString::make_string("old-field");
  const char *json=R"({"TYPE":"CSV","LINE_DELIMITER":"41","FIELD_DELIMITER":"42"})";
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,format.load_from_string(ObString::make_string(json),owner));
  expect_bytes(format.csv_format_.line_term_str_,"A");expect_bytes(format.csv_format_.field_term_str_,"old-field");
  EXPECT_EQ(2,owner.calls_);
}
TEST(NativeLoadFormat, malformed_all_fields_preserve_previous_values) {
  CountingAllocator owner;ObExternalFileFormat format;
  format.csv_format_.line_term_str_=ObString::make_string("L");
  format.csv_format_.field_term_str_=ObString::make_string("F");
  format.csv_format_.file_extension_=ObString::make_string("E");
  format.origin_file_format_str_.origin_line_term_str_=ObString::make_string("1");
  format.origin_file_format_str_.origin_field_term_str_=ObString::make_string("2");
  format.origin_file_format_str_.origin_field_escaped_str_=ObString::make_string("3");
  format.origin_file_format_str_.origin_field_enclosed_str_=ObString::make_string("4");
  format.origin_file_format_str_.origin_null_if_str_=ObString::make_string("5");
  const char *json=R"({"TYPE":"CSV","LINE_DELIMITER":"GG","FIELD_DELIMITER":"GG","NULL_IF_EXETERNAL":["GG","GG"],"FILE_EXTENSION":"GG","ORIGIN_LINE_DELIMITER":"GG","ORIGIN_FIELD_DELIMITER":"GG","ORIGIN_ESCAPE":"GG","ORIGIN_FIELD_OPTIONALLY_ENCLOSED_BY":"GG","ORIGIN_NULL_IF_EXETERNAL":"GG"})";
  ASSERT_EQ(OB_SUCCESS,format.load_from_string(ObString::make_string(json),owner));
  expect_bytes(format.csv_format_.line_term_str_,"L");expect_bytes(format.csv_format_.field_term_str_,"F");
  expect_bytes(format.csv_format_.file_extension_,"E");
  ASSERT_EQ(2,format.csv_format_.null_if_.count());
  expect_bytes(format.csv_format_.null_if_.at(0),"");expect_bytes(format.csv_format_.null_if_.at(1),"");
  expect_bytes(format.origin_file_format_str_.origin_line_term_str_,"1");
  expect_bytes(format.origin_file_format_str_.origin_field_term_str_,"2");
  expect_bytes(format.origin_file_format_str_.origin_field_escaped_str_,"3");
  expect_bytes(format.origin_file_format_str_.origin_field_enclosed_str_,"4");
  expect_bytes(format.origin_file_format_str_.origin_null_if_str_,"5");
}
TEST(NativeLoadFormat, null_if_allocation_failures_keep_partial_publication) {
  for (int64_t allocation_limit : {int64_t(0),int64_t(2)}) {
    CountingAllocator owner(allocation_limit);ObExternalFileFormat format;
    const char *json=R"({"TYPE":"CSV","NULL_IF_EXETERNAL":["41","42"]})";
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,format.load_from_string(ObString::make_string(json),owner));
    if (allocation_limit==0) { EXPECT_EQ(0,format.csv_format_.null_if_.count()); }
    else {
      ASSERT_EQ(2,format.csv_format_.null_if_.count());
      expect_bytes(format.csv_format_.null_if_.at(0),"A");
      expect_bytes(format.csv_format_.null_if_.at(1),"");
    }
    EXPECT_EQ(allocation_limit+1,owner.calls_);
  }
}
#ifndef LOAD_FORMAT_LEGACY_ORACLE
TEST(NativeLoadFormat, high_byte_input_is_invalid) {
  CountingAllocator owner;const std::string input("\x80",1);ObString result;bool valid=true;
  EXPECT_EQ(OB_SUCCESS,ObHexUtilsBase::unhex(ObString(input.size(),input.data()),owner,result,valid));
  EXPECT_FALSE(valid);EXPECT_EQ(nullptr,result.ptr());
}
TEST(NativeLoadFormat, same_input_and_output_descriptor) {
  CountingAllocator owner;std::string input="0041";ObString result(input.size(),input.data());bool valid=false;
  ASSERT_EQ(OB_SUCCESS,ObHexUtilsBase::unhex(result,owner,result,valid));ASSERT_TRUE(valid);
  expect_bytes(result,std::string("\0A",2));
}
#endif
TEST(NativeLoadFormat, null_if_non_string_fails_after_first_value) {
  CountingAllocator owner;ObExternalFileFormat format;
  const char *json=R"({"TYPE":"CSV","NULL_IF_EXETERNAL":["41",2]})";
  EXPECT_EQ(OB_ERR_UNEXPECTED,format.load_from_string(ObString::make_string(json),owner));
  ASSERT_EQ(2,format.csv_format_.null_if_.count());
  expect_bytes(format.csv_format_.null_if_.at(0),"A");expect_bytes(format.csv_format_.null_if_.at(1),"");
}
TEST(NativeLoadFormat, repeated_empty_null_if_keeps_existing_array) {
  CountingAllocator owner;ObExternalFileFormat format;
  ASSERT_EQ(OB_SUCCESS,format.load_from_string(ObString::make_string(R"({"TYPE":"CSV","NULL_IF_EXETERNAL":["41"]})"),owner));
  ASSERT_EQ(OB_SUCCESS,format.load_from_string(ObString::make_string(R"({"TYPE":"CSV","NULL_IF_EXETERNAL":[]})"),owner));
  ASSERT_EQ(1,format.csv_format_.null_if_.count());expect_bytes(format.csv_format_.null_if_.at(0),"A");
}
