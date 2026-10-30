/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/optimizer/stat/ob_opt_column_stat.h"
#include "sql/optimizer/stat/ob_stat_item.h"
#include "sql/optimizer/stat/ob_opt_osg_column_stat.h"
#include "sql/optimizer/stat/ob_dbms_stats_copy_table_stats.h"
#include "sql/optimizer/stat/ob_opt_stat_sql_service.h"
#include "sql/engine/aggregate/ob_aggregate_processor.h"
#include "common/lob/lob_types.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "share/datum/typed_datum_compare.h"
#include "share/datum/typed_datum_murmur.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
namespace {
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result; std::istringstream in(line); std::string field;
  while (std::getline(in, field, '\t')) { result.push_back(field); } return result;
}
std::string unhex(const std::string &s) {
  std::string out;
  if (s != "-") { for (size_t i=0;i<s.size();i+=2) { out.push_back(std::stoi(s.substr(i,2),nullptr,16)); } }
  return out;
}
std::string hex(const char *p,int64_t n) {
  const char *digits="0123456789abcdef"; std::string out;
  for(int64_t i=0;i<n;++i) { const uint8_t b=p[i];out+=digits[b>>4];out+=digits[b&15]; }
  return out.empty()?"-":out;
}
std::string wire(const ObOptColumnStat &stat) {
  std::vector<char> buf(stat.get_serialize_size()+64);int64_t pos=0;
  EXPECT_EQ(OB_SUCCESS,stat.serialize(buf.data(),buf.size(),pos));return hex(buf.data(),pos);
}
std::string value_wire(const DatumValue &v) {
  int64_t n=0,pos=0;EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(v.datum_,v.meta_,n));
  std::vector<char> buf(n+64);EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_value(v.datum_,v.meta_,buf.data(),buf.size(),pos));
  return hex(buf.data(),pos);
}
void init(ObOptColumnStat &s,const DatumValue &lo,const DatumValue &hi,int bitmap=0) {
  s.set_table_id(998);s.set_partition_id(11);s.set_column_id(16);s.set_stat_level(TABLE_LEVEL);
  s.set_num_distinct(3);s.set_num_null(2);s.set_num_not_null(8);s.set_avg_len(12);s.set_total_col_len(96);
  ASSERT_EQ(OB_SUCCESS,s.set_min_value(lo));ASSERT_EQ(OB_SUCCESS,s.set_max_value(hi));s.set_llc_bitmap_size(bitmap);
  for(int i=0;i<bitmap;++i){s.get_llc_bitmap()[i]=char((i*17+3)&0xff);}
}
DatumValue integer(int64_t n,ObIAllocator &owner) { DatumValue v;EXPECT_EQ(OB_SUCCESS,v.set_int(n,owner));return v; }
DatumValue text_lob(const ObString &payload, ObIAllocator &owner) {
  ObMemLobExternFlags flags(false);
  const uint32_t locator_size = ObLobLocatorV2::calc_locator_full_len(
      flags, 0, payload.length(), 0, true);
  char *locator_data = static_cast<char *>(owner.alloc(locator_size));
  EXPECT_NE(nullptr, locator_data);
  if (nullptr == locator_data) { return DatumValue(); }
  MEMSET(locator_data, 0, locator_size);
  ObLobLocatorV2 locator(locator_data, locator_size);
  ObString empty_rowkey;
  EXPECT_EQ(OB_SUCCESS, locator.fill(TEMP_FULL_LOB, flags, empty_rowkey,
                                     nullptr, payload.length(), 0, 0, true));
  EXPECT_EQ(OB_SUCCESS, locator.set_payload_data(payload));
  DatumValue value;
  value.datum_.set_string(locator_data, locator_size);
  value.meta_.set_type(ObLongTextType);
  value.meta_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  value.meta_.set_has_lob_header();
  return value;
}
DatumValue outrow_lob(SqlValueType type, ObIAllocator &owner) {
  const uint32_t locator_size = sizeof(ObMemLobCommon);
  char *locator_data = static_cast<char *>(owner.alloc(locator_size));
  EXPECT_NE(nullptr, locator_data);
  if (nullptr == locator_data) { return DatumValue(); }
  MEMSET(locator_data, 0, locator_size);
  ObMemLobCommon *header = new (locator_data) ObMemLobCommon(TEMP_FULL_LOB, true);
  header->set_has_inrow_data(false);
  DatumValue value;
  value.datum_.set_string(locator_data, locator_size);
  value.meta_.set_type(type);
  value.meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  value.meta_.set_has_lob_header();
  return value;
}
void expect_osg_hash_matches_native_datum_hash(const DatumValue &value) {
  uint64_t expected_hash = 0;
  if (value.meta_.is_json() || value.meta_.is_geometry()) {
    // Out-row JSON and Geometry values intentionally contribute the zero seed.
    expected_hash = 0;
  } else {
    ASSERT_EQ(OB_SUCCESS, hash_datum_value_murmur(value, 0, expected_hash));
  }
  ObArenaAllocator owner;
  ObOptColumnStat stat(owner);
  ObOptOSGColumnStat osg(owner);
  osg.col_stat_ = &stat;
  ASSERT_EQ(OB_SUCCESS, osg.update_column_stat_info(
      &value.datum_, value.meta_, nullptr));
  std::vector<char> expected(ObOptColumnStat::NUM_LLC_BUCKET, 0);
  ASSERT_EQ(OB_SUCCESS, oceanbase::sql::ObAggregateProcessor::llc_add_value(
      expected_hash, expected.data(), expected.size()));
  EXPECT_EQ(1, stat.get_num_not_null());
  EXPECT_EQ(expected.size(), stat.get_llc_bitmap_size());
  EXPECT_EQ(0, MEMCMP(expected.data(), stat.get_llc_bitmap(), expected.size()));
  osg.col_stat_ = nullptr;
}
class SwitchAllocator : public ObIAllocator {
public:
  void *alloc(int64_t n) override { return reject_?nullptr:arena_.alloc(n); }
  void *alloc(int64_t n,const ObMemAttr &) override { return alloc(n); }
  void free(void *p) override { arena_.free(p); }
  bool reject_=false;
  ObArenaAllocator arena_;
};
}

TEST(NativeColumnStats, immutable_old_wire_compare_merge_and_bounds) {
  std::ifstream input("unittest/sql/data/native_column_stats.golden");ASSERT_TRUE(input.good());
  ObArenaAllocator owner;std::vector<DatumValue> values;std::string line;int stat_count=0,pair_count=0,merge_count=0,bound_count=0;
  while(std::getline(input,line)) {
    if(line.empty()||line[0]=='#'){continue;}const auto f=fields(line);SCOPED_TRACE(line);
    if(f[0]=="V") {
      ASSERT_EQ(values.size(),std::stoi(f[1]));const auto b=unhex(f[3]);int64_t p=0;DatumValue value;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(b.data(),b.size(),p,owner,value.datum_,value.meta_));
      ASSERT_EQ(b.size(),p);values.push_back(value);
    } else if(f[0]=="L") {
      SqlTypeMeta output_meta;output_meta.set_varchar();output_meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
      ObString printed;const int ret=ObOptStatSqlService::get_valid_value_str(values.at(std::stoi(f[1])),output_meta,owner,printed,ObValuePrintParams());
      EXPECT_EQ(std::stoi(f[2]),ret);if(ret==OB_SUCCESS){EXPECT_EQ(f[3],hex(printed.ptr(),printed.length()));}
    } else if(f[0]=="S") {
      ObOptColumnStat stat(owner);init(stat,values.at(std::stoi(f[1])),values.at(std::stoi(f[1])),std::stoi(f[2]));
      EXPECT_EQ(f[3],wire(stat));const auto b=unhex(f[3]);int64_t p=0;ObOptColumnStat decoded(owner);
      ASSERT_EQ(OB_SUCCESS,decoded.deserialize(b.data(),b.size(),p));EXPECT_EQ(b.size(),p);EXPECT_EQ(f[3],wire(decoded));++stat_count;
    } else if(f[0]=="P") {
      int cmp=0;int ret=compare_datum_values(values.at(std::stoi(f[1])),values.at(std::stoi(f[2])),cmp);
      if(std::stoi(f[3])==OB_SUCCESS){EXPECT_EQ(OB_SUCCESS,ret);EXPECT_EQ(std::stoi(f[4]),cmp);}
      else {EXPECT_NE(OB_SUCCESS,ret);}++pair_count;
    } else if(f[0]=="M") {
      ObOptColumnStat a(owner),b(owner);init(a,values.at(std::stoi(f[1])),values.at(std::stoi(f[1])));
      init(b,values.at(std::stoi(f[2])),values.at(std::stoi(f[2])));EXPECT_EQ(std::stoi(f[3]),a.merge_column_stat(b));
      EXPECT_EQ(f[4],wire(a));++merge_count;
    } else if(f[0]=="T") {
      const auto b=unhex(f[1]);ObOptColumnStat stat(owner);int64_t pos=0;
      int ret=stat.deserialize(b.data(),b.size(),pos);EXPECT_EQ(std::stoi(f[2]),ret);EXPECT_EQ(std::stoll(f[3]),pos);
      if(ret==OB_SUCCESS){EXPECT_EQ(f[4],wire(stat));}
    } else if(f[0]=="B") {
      CopyTableStatHelper helper(&owner);ObCopyPartInfo info;info.part_lower_bound_=integer(-20,owner);
      info.part_upper_bound_=integer(20,owner);info.pre_part_upper_bound_=integer(-30,owner);
      info.min_res_type_=static_cast<ObCopyResType>(std::stoi(f[2]));info.max_res_type_=info.min_res_type_;
      DatumValue source=integer(15,owner),result=integer(999,owner);
      int ret=std::stoi(f[1])?helper.copy_min_val(source,result,&info):helper.copy_max_val(source,result,&info);
      EXPECT_EQ(std::stoi(f[3]),ret);EXPECT_EQ(f[4],value_wire(result));++bound_count;
    }
  }
  EXPECT_EQ(28,values.size());EXPECT_EQ(56,stat_count);EXPECT_EQ(784,pair_count);EXPECT_GT(merge_count,200);EXPECT_EQ(14,bound_count);
}

TEST(NativeColumnStats, values_survive_source_arena_reuse_and_cache_copy) {
  ObArenaAllocator source,destination;DatumValue text,number;
  ASSERT_EQ(OB_SUCCESS,text.set_varchar(ObString("customer_客户"),source));text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  number=integer(42,source);ObOptColumnStat stat(destination);init(stat,text,number,16);
  const std::string expected=wire(stat);ObOptColumnStat assigned(destination),deep(destination);
  ASSERT_EQ(OB_SUCCESS,assigned.assign(stat));ASSERT_EQ(OB_SUCCESS,deep.deep_copy(stat));
  std::vector<char> cache(stat.size());ObIKVCacheValue *base=nullptr;
  ASSERT_EQ(OB_SUCCESS,stat.deep_copy(cache.data(),cache.size(),base));auto *cached=static_cast<ObOptColumnStat *>(base);
  EXPECT_EQ(expected,wire(*cached));source.reuse();std::memset(source.alloc(65536),0x7f,65536);
  EXPECT_EQ(expected,wire(stat));EXPECT_EQ(expected,wire(assigned));EXPECT_EQ(expected,wire(deep));
  destination.reuse();std::memset(destination.alloc(65536),0x6f,65536);
  EXPECT_EQ(expected,wire(*cached));
}

TEST(NativeColumnStats, failed_owned_setter_preserves_value_and_assign_reports_error) {
  ObArenaAllocator input;SwitchAllocator owner;ObOptColumnStat stat(owner),other(input);
  DatumValue one=integer(1,input),two=integer(2,input);init(stat,one,one);init(other,two,two);
  owner.reject_=true;EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,stat.set_min_value(two));EXPECT_EQ(1,stat.get_min_value().get_int());
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,stat.assign(other));EXPECT_EQ(1,stat.get_min_value().get_int());
}

TEST(NativeColumnStats, global_extrema_flush_owns_source_and_propagates_compare_error) {
  ObArenaAllocator source,output;ObGlobalMinEval minimum;ObGlobalMaxEval maximum;
  DatumValue a=integer(30,source),b=integer(-4,source);
  ASSERT_EQ(OB_SUCCESS,minimum.add(a));ASSERT_EQ(OB_SUCCESS,minimum.add(b));
  ASSERT_EQ(OB_SUCCESS,maximum.add(a));ASSERT_EQ(OB_SUCCESS,maximum.add(b));
  ASSERT_EQ(OB_SUCCESS,minimum.flush(&output));ASSERT_EQ(OB_SUCCESS,maximum.flush(&output));
  source.reuse();std::memset(source.alloc(65536),0x5f,65536);
  EXPECT_EQ(-4,minimum.get().get_int());EXPECT_EQ(30,maximum.get().get_int());
  DatumValue invalid;ASSERT_EQ(OB_SUCCESS,invalid.set_varchar(ObString("bad"),output));invalid.set_collation_type(CS_TYPE_INVALID);
  EXPECT_NE(OB_SUCCESS,minimum.add(invalid));EXPECT_EQ(-4,minimum.get().get_int());
}

TEST(NativeColumnStats, persisted_hex_is_owned_after_input_is_destroyed) {
  ObArenaAllocator input,output;DatumValue value;ASSERT_EQ(OB_SUCCESS,value.set_varchar(ObString("stored-value"),input));
  value.set_collation_type(CS_TYPE_UTF8MB4_BIN);ObString encoded;
  ASSERT_EQ(OB_SUCCESS,ObOptStatSqlService::get_value_binary_hex_str(value,input,encoded));
  DatumValue restored;ASSERT_EQ(OB_SUCCESS,ObOptStatSqlService::hex_str_to_value(encoded.ptr(),encoded.length(),output,restored));
  const std::string expected=value_wire(value);input.reuse();std::memset(input.alloc(65536),0x3f,65536);
  EXPECT_EQ(expected,value_wire(restored));
}

TEST(NativeColumnStats, osg_publication_is_native_and_owns_payload) {
  ObArenaAllocator owner,source;ObOptOSGColumnStat osg(owner);
  ObOptColumnStat stat(owner);osg.col_stat_=&stat;
  DatumValue a=integer(-30,source),b=integer(70,source);
  osg.min_val_.datum_=&a.datum_;osg.min_val_.meta_=a.meta_;
  osg.max_val_.datum_=&b.datum_;osg.max_val_.meta_=b.meta_;
  ASSERT_EQ(OB_SUCCESS,osg.publish_min_max_values());
  source.reuse();std::memset(source.alloc(65536),0x2f,65536);
  EXPECT_EQ(-30,stat.get_min_value().get_int());EXPECT_EQ(70,stat.get_max_value().get_int());
  // The test Datum descriptors live on this stack; detach before reset() because
  // source arena contents have intentionally been invalidated.
  osg.min_val_.datum_=nullptr;osg.max_val_.datum_=nullptr;osg.col_stat_=nullptr;
}

TEST(NativeColumnStats, osg_datum_hash_matches_native_llc_bitmap) {
  ObArenaAllocator input;
  DatumValue integer_value = integer(37, input);
  expect_osg_hash_matches_native_datum_hash(integer_value);

  DatumValue text_value = text_lob(ObString("long text sample"), input);
  expect_osg_hash_matches_native_datum_hash(text_value);

  DatumValue json_outrow = outrow_lob(ObJsonType, input);
  expect_osg_hash_matches_native_datum_hash(json_outrow);

  DatumValue geometry_outrow = outrow_lob(ObGeometryType, input);
  expect_osg_hash_matches_native_datum_hash(geometry_outrow);
}

TEST(NativeColumnStats, odd_payloads_and_nonempty_histogram_cache_copy_are_aligned_and_owned) {
  ObArenaAllocator source;DatumValue minimum,maximum;
  ASSERT_EQ(OB_SUCCESS,minimum.set_varchar(ObString("abc"),source));minimum.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,maximum.set_varchar(ObString("abcde"),source));maximum.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObOptColumnStat stat(source);init(stat,minimum,maximum,16);
  ObHistogram &hist=stat.get_histogram();hist.set_type(FREQUENCY);hist.set_sample_size(4);hist.set_bucket_cnt(0);
  ASSERT_EQ(OB_SUCCESS,hist.prepare_allocate_buckets(source,2));
  DatumValue endpoint;ASSERT_EQ(OB_SUCCESS,endpoint.set_varchar(ObString("odd-end"),source));
  endpoint.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,hist.add_bucket(ObHistBucket(endpoint,1,1),source));
  ASSERT_EQ(OB_SUCCESS,endpoint.set_int(19,source));
  ASSERT_EQ(OB_SUCCESS,hist.add_bucket(ObHistBucket(endpoint,3,4),source));
  std::vector<char> buffer(stat.size());ObIKVCacheValue *base=nullptr;
  ASSERT_EQ(OB_SUCCESS,stat.deep_copy(buffer.data(),buffer.size(),base));auto *cached=static_cast<ObOptColumnStat *>(base);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(cached->get_min_value().datum_.ptr_)&7);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(cached->get_max_value().datum_.ptr_)&7);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(&cached->get_histogram().get(0))%alignof(ObHistBucket));
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(cached->get_histogram().get(0).endpoint_value_.get_string_ptr())&7);
  source.reuse();std::memset(source.alloc(65536),0x7a,65536);
  EXPECT_EQ(ObString("abc"),cached->get_min_value().get_string());
  EXPECT_EQ(ObString("abcde"),cached->get_max_value().get_string());
  EXPECT_EQ(ObString("odd-end"),cached->get_histogram().get(0).endpoint_value_.get_string());
  EXPECT_EQ(19,cached->get_histogram().get(1).endpoint_value_.get_int());
}

TEST(NativeColumnStats, nested_sql_array_cache_clone_owns_descriptors_and_leaves) {
  ObArenaAllocator source;DatumParameterArray *child=DatumParameterArray::alloc(source,2);
  ASSERT_NE(nullptr,child);ASSERT_EQ(OB_SUCCESS,child->data_[0].set_int(41,source));
  ASSERT_EQ(OB_SUCCESS,child->data_[1].set_varchar(ObString("abc"),source));
  child->data_[1].set_collation_type(CS_TYPE_UTF8MB4_BIN);
  DatumParameterArray *outer=DatumParameterArray::alloc(source,2);ASSERT_NE(nullptr,outer);
  ASSERT_EQ(OB_SUCCESS,outer->data_[0].set_extend(reinterpret_cast<int64_t>(child),T_EXT_SQL_ARRAY,source));
  ASSERT_EQ(OB_SUCCESS,outer->data_[1].set_int(-1,source));
  DatumParameter parameter;ASSERT_EQ(OB_SUCCESS,parameter.set_extend(reinterpret_cast<int64_t>(outer),T_EXT_SQL_ARRAY,source));
  ObOptColumnStat stat(source);init(stat,parameter.value_view(),parameter.value_view());
  const std::string expected=value_wire(stat.get_min_value());
  std::vector<char> buffer(stat.size());ObIKVCacheValue *base=nullptr;
  ASSERT_EQ(OB_SUCCESS,stat.deep_copy(buffer.data(),buffer.size(),base));auto *cached=static_cast<ObOptColumnStat *>(base);
  auto *owned_outer=reinterpret_cast<const DatumParameterArray *>(cached->get_min_value().get_int());
  auto *owned_child=owned_outer->data_[0].get_array<DatumParameterArray>();
  EXPECT_NE(outer,owned_outer);EXPECT_NE(child,owned_child);
  const uintptr_t first=reinterpret_cast<uintptr_t>(buffer.data()),last=first+buffer.size();
  for(const void *p:{static_cast<const void *>(owned_outer),static_cast<const void *>(owned_outer->data_),
                     static_cast<const void *>(owned_child),static_cast<const void *>(owned_child->data_),
                     static_cast<const void *>(owned_child->data_[0].datum_.ptr_),static_cast<const void *>(owned_child->data_[1].datum_.ptr_)}) {
    EXPECT_GE(reinterpret_cast<uintptr_t>(p),first);EXPECT_LT(reinterpret_cast<uintptr_t>(p),last);
    EXPECT_EQ(0,reinterpret_cast<uintptr_t>(p)&7);
  }
  source.reuse();std::memset(source.alloc(65536),0x4e,65536);
  EXPECT_EQ(2,owned_outer->count_);EXPECT_EQ(2,owned_child->count_);EXPECT_EQ(41,owned_child->data_[0].get_int());
  EXPECT_EQ(ObString("abc"),owned_child->data_[1].get_string());EXPECT_EQ(expected,value_wire(cached->get_min_value()));
}
