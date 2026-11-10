/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/optimizer/stat/ob_opt_column_stat.h"
#include "sql/optimizer/stat/ob_opt_column_stat_cache.h"
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
#include <limits>
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
  int64_t n=0,pos=0;EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::native_value_size(v.datum_,v.meta_,n));
  std::vector<char> buf(n+64);EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_native_value(v.datum_,v.meta_,buf.data(),buf.size(),pos));
  return hex(buf.data(),pos);
}
std::string min_wire(const ObOptColumnStat &stat) {
  alignas(8) char scratch[64]; DatumValue value;
  EXPECT_EQ(OB_SUCCESS,stat.export_min_value(value,scratch,sizeof(scratch)));
  return value_wire(value);
}
ObHistBucket hist_bucket(const DatumValue &value,int64_t repeat,int64_t endpoint) {
  ObHistBucket result(repeat,endpoint);
  EXPECT_EQ(OB_SUCCESS,result.set_endpoint_borrowed(value));
  return result;
}
void same_meta(const SqlTypeMeta &expected,const SqlTypeMeta &actual) {
  EXPECT_EQ(expected.get_type(),actual.get_type());
  EXPECT_EQ(expected.get_collation_level(),actual.get_collation_level());
  EXPECT_EQ(expected.get_collation_type(),actual.get_collation_type());
  EXPECT_EQ(expected.get_scale(),actual.get_scale());
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
  SqlType source_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(value.meta_, ObAccuracy(), source_type));
  alignas(8) char physical_scratch[64]; ObDatum physical;
  Datum source_value;
  ASSERT_EQ(OB_SUCCESS,value.export_legacy(physical,physical_scratch,sizeof(physical_scratch)));
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(source_type).import_legacy(physical, source_value));
  ASSERT_EQ(OB_SUCCESS, osg.update_column_stat_info(
      &source_value, source_type, value.meta_, nullptr));
  std::vector<char> expected(ObOptColumnStat::NUM_LLC_BUCKET, 0);
  ASSERT_EQ(OB_SUCCESS, oceanbase::sql::ObAggregateProcessor::llc_add_value(
      expected_hash, expected.data(), expected.size()));
  EXPECT_EQ(1, stat.get_num_not_null());
  EXPECT_EQ(expected.size(), stat.get_llc_bitmap_size());
  EXPECT_EQ(0, MEMCMP(expected.data(), stat.get_llc_bitmap(), expected.size()));
  osg.col_stat_ = nullptr;
}
int update_osg_literal(ObOptOSGColumnStat &osg, const DatumValue &value,
                       const ObAccuracy &accuracy = ObAccuracy()) {
  SqlType declared;
  int ret = SqlType::from_legacy(value.meta_, accuracy, declared);
  alignas(8) char scratch[64]; ObDatum physical;
  Datum semantic;
  if (ret == OB_SUCCESS) { ret = value.export_legacy(physical, scratch, sizeof(scratch)); }
  if (ret == OB_SUCCESS) { ret = DatumCopyPlan(declared).import_legacy(physical, semantic); }
  const ObDatumBasicFuncs *functions = ObDatumFuncs::get_basic_func(
      value.get_type(), value.get_collation_type(), value.get_scale(), value.has_lob_header());
  if (ret == OB_SUCCESS && functions == nullptr) { ret = OB_NOT_SUPPORTED; }
  if (ret == OB_SUCCESS) {
    ret = osg.update_column_stat_info(&semantic, declared, value.meta_, functions->null_first_cmp_);
  }
  return ret;
}
class SwitchAllocator : public ObIAllocator {
public:
  void *alloc(int64_t n) override { ++calls_; return reject_?nullptr:arena_.alloc(n); }
  void *alloc(int64_t n,const ObMemAttr &) override { return alloc(n); }
  void free(void *p) override { arena_.free(p); }
  int64_t calls_=0;
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
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_native_value(b.data(),b.size(),p,owner,value.datum_,value.meta_));
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
  DatumValue one=integer(1,input),two;
  ASSERT_EQ(OB_SUCCESS,two.set_varchar(ObString("replacement"),input));
  two.set_collation_type(CS_TYPE_UTF8MB4_BIN);init(stat,one,one);init(other,two,two);
  owner.reject_=true;EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,stat.set_min_value(two));EXPECT_EQ(1,stat.get_min_datum().get_int());
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,stat.assign(other));EXPECT_EQ(1,stat.get_min_datum().get_int());
}

TEST(NativeColumnStats, global_extrema_flush_owns_source_and_propagates_compare_error) {
  ObArenaAllocator source,output;ObGlobalMinEval minimum;ObGlobalMaxEval maximum;
  DatumValue a=integer(30,source),b=integer(-4,source);
  ASSERT_EQ(OB_SUCCESS,minimum.add(a));ASSERT_EQ(OB_SUCCESS,minimum.add(b));
  ASSERT_EQ(OB_SUCCESS,maximum.add(a));ASSERT_EQ(OB_SUCCESS,maximum.add(b));
  ASSERT_EQ(OB_SUCCESS,minimum.flush(&output));ASSERT_EQ(OB_SUCCESS,maximum.flush(&output));
  source.reuse();std::memset(source.alloc(65536),0x5f,65536);
  EXPECT_EQ(-4,minimum.get_datum().get_int());EXPECT_EQ(30,maximum.get_datum().get_int());
  DatumValue invalid;ASSERT_EQ(OB_SUCCESS,invalid.set_varchar(ObString("bad"),output));invalid.set_collation_type(CS_TYPE_INVALID);
  EXPECT_NE(OB_SUCCESS,minimum.add(invalid));EXPECT_EQ(-4,minimum.get_datum().get_int());
  // A reference winner is borrowed by add(), then owned only after flush().
  ObArenaAllocator strings; SwitchAllocator retained;
  DatumValue alpha,zebra;
  ASSERT_EQ(OB_SUCCESS,alpha.set_varchar(ObString("alpha"),strings));
  ASSERT_EQ(OB_SUCCESS,zebra.set_varchar(ObString("zebra"),strings));
  alpha.set_collation_type(CS_TYPE_UTF8MB4_BIN);zebra.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObGlobalMinEval text_min;ObGlobalMaxEval text_max;
  ASSERT_EQ(OB_SUCCESS,text_min.add(zebra));ASSERT_EQ(OB_SUCCESS,text_min.add(alpha));
  ASSERT_EQ(OB_SUCCESS,text_max.add(alpha));ASSERT_EQ(OB_SUCCESS,text_max.add(zebra));
  EXPECT_EQ(alpha.get_string_ptr(),text_min.get_datum().get_reference());
  EXPECT_EQ(zebra.get_string_ptr(),text_max.get_datum().get_reference());
  retained.reject_=true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,text_min.flush(&retained));
  EXPECT_EQ(alpha.get_string_ptr(),text_min.get_datum().get_reference());
  same_meta(alpha.meta_,text_min.get_meta());
  retained.reject_=false;
  ASSERT_EQ(OB_SUCCESS,text_min.flush(&retained));ASSERT_EQ(OB_SUCCESS,text_max.flush(&retained));
  EXPECT_NE(alpha.get_string_ptr(),text_min.get_datum().get_reference());
  EXPECT_NE(zebra.get_string_ptr(),text_max.get_datum().get_reference());
  ObGlobalColumnStat global;
  ASSERT_EQ(OB_SUCCESS,global.set_min_native(text_min.get_datum(),text_min.get_meta(),output));
  ASSERT_EQ(OB_SUCCESS,global.set_max_native(text_max.get_datum(),text_max.get_meta(),output));
  strings.reuse();std::memset(strings.alloc(65536),0x61,65536);
  EXPECT_EQ(ObString("alpha"),text_min.get_datum().get_string());
  EXPECT_EQ(ObString("zebra"),text_max.get_datum().get_string());
  EXPECT_EQ(ObString("alpha"),global.get_min_datum().get_string());
  EXPECT_EQ(ObString("zebra"),global.get_max_datum().get_string());
  same_meta(alpha.meta_,global.get_min_meta());same_meta(zebra.meta_,global.get_max_meta());
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
  ASSERT_EQ(OB_SUCCESS,update_osg_literal(osg,a));
  ASSERT_EQ(OB_SUCCESS,update_osg_literal(osg,b));
  ASSERT_EQ(OB_SUCCESS,osg.publish_min_max_values());
  source.reuse();std::memset(source.alloc(65536),0x2f,65536);
  EXPECT_EQ(-30,stat.get_min_datum().get_int());EXPECT_EQ(70,stat.get_max_datum().get_int());
  // The column statistic is a stack fixture owned independently of osg.
  osg.col_stat_=nullptr;
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
  ASSERT_EQ(OB_SUCCESS,hist.add_bucket(hist_bucket(endpoint,1,1),source));
  ASSERT_EQ(OB_SUCCESS,endpoint.set_int(19,source));
  ASSERT_EQ(OB_SUCCESS,hist.add_bucket(hist_bucket(endpoint,3,4),source));
  std::vector<char> buffer(stat.size());ObIKVCacheValue *base=nullptr;
  ASSERT_EQ(OB_SUCCESS,stat.deep_copy(buffer.data(),buffer.size(),base));auto *cached=static_cast<ObOptColumnStat *>(base);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(cached->get_min_datum().get_reference())&7);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(cached->get_max_datum().get_reference())&7);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(&cached->get_histogram().get(0))%alignof(ObHistBucket));
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(cached->get_histogram().get(0).get_endpoint_datum().get_string().ptr())&7);
  source.reuse();std::memset(source.alloc(65536),0x7a,65536);
  EXPECT_EQ(ObString("abc"),cached->get_min_datum().get_string());
  EXPECT_EQ(ObString("abcde"),cached->get_max_datum().get_string());
  EXPECT_EQ(ObString("odd-end"),cached->get_histogram().get(0).get_endpoint_datum().get_string());
  EXPECT_EQ(19,cached->get_histogram().get(1).get_endpoint_datum().get_int());
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
  const std::string expected=min_wire(stat);
  std::vector<char> buffer(stat.size());ObIKVCacheValue *base=nullptr;
  ASSERT_EQ(OB_SUCCESS,stat.deep_copy(buffer.data(),buffer.size(),base));auto *cached=static_cast<ObOptColumnStat *>(base);
  alignas(8) char scratch[64]; DatumValue minimum;
  ASSERT_EQ(OB_SUCCESS,cached->export_min_value(minimum,scratch,sizeof(scratch)));
  auto *owned_outer=reinterpret_cast<const DatumParameterArray *>(minimum.get_ext());
  auto *owned_child=owned_outer->data_[0].get_array<DatumParameterArray>();
  EXPECT_NE(outer,owned_outer);EXPECT_NE(child,owned_child);
  const uintptr_t first=reinterpret_cast<uintptr_t>(buffer.data()),last=first+buffer.size();
  for(const void *p:{static_cast<const void *>(owned_outer),static_cast<const void *>(owned_outer->data_),
                     static_cast<const void *>(owned_child),static_cast<const void *>(owned_child->data_),
                     static_cast<const void *>(&owned_child->data_[0].datum_),owned_child->data_[1].datum_.get_reference()}) {
    EXPECT_GE(reinterpret_cast<uintptr_t>(p),first);EXPECT_LT(reinterpret_cast<uintptr_t>(p),last);
    EXPECT_EQ(0,reinterpret_cast<uintptr_t>(p)&7);
  }
  source.reuse();std::memset(source.alloc(65536),0x4e,65536);
  EXPECT_EQ(2,owned_outer->count_);EXPECT_EQ(2,owned_child->count_);EXPECT_EQ(41,owned_child->data_[0].get_int());
  EXPECT_EQ(ObString("abc"),owned_child->data_[1].get_string());EXPECT_EQ(expected,min_wire(*cached));
}


TEST(NativeColumnStats, inline_integer_payload_operations_do_not_allocate)
{
  ObArenaAllocator source;
  SwitchAllocator owner;
  // Constructors own the LLC bitmap. Count only the subsequent value operations.
  ObOptColumnStat stat(owner),decoded(owner);
  DatumValue low=integer(std::numeric_limits<int64_t>::min(),source),high;
  ASSERT_EQ(OB_SUCCESS,high.set_uint64(std::numeric_limits<uint64_t>::max(),source));
  owner.calls_=0;owner.reject_=true;
  ASSERT_EQ(OB_SUCCESS,stat.set_min_value(low));
  ASSERT_EQ(OB_SUCCESS,stat.set_max_value(high));
  EXPECT_EQ(0,owner.calls_);
  EXPECT_EQ(0,stat.get_min_datum().length());EXPECT_EQ(0,stat.get_max_datum().length());
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),stat.get_min_datum().get_int());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),stat.get_max_datum().get_uint());
  same_meta(low.meta_,stat.get_min_meta());same_meta(high.meta_,stat.get_max_meta());
  ASSERT_EQ(OB_SUCCESS,decoded.set_min_native(stat.get_min_datum(),stat.get_min_meta()));
  ASSERT_EQ(OB_SUCCESS,decoded.set_max_native(stat.get_max_datum(),stat.get_max_meta()));
  stat.set_llc_bitmap_size(0);
  std::vector<char> buffer(stat.get_serialize_size()+64);int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,stat.serialize(buffer.data(),buffer.size(),pos));
  int64_t read=0;
  ASSERT_EQ(OB_SUCCESS,decoded.deserialize(buffer.data(),pos,read));
  EXPECT_EQ(pos,read);EXPECT_EQ(0,owner.calls_);
  same_meta(low.meta_,decoded.get_min_meta());same_meta(high.meta_,decoded.get_max_meta());
  ObGlobalMinEval minimum;ObGlobalMaxEval maximum;
  ASSERT_EQ(OB_SUCCESS,minimum.add_native(stat.get_min_datum(),stat.get_min_meta()));
  ASSERT_EQ(OB_SUCCESS,maximum.add_native(stat.get_max_datum(),stat.get_max_meta()));
  ASSERT_EQ(OB_SUCCESS,minimum.flush(&owner));ASSERT_EQ(OB_SUCCESS,maximum.flush(&owner));
  EXPECT_EQ(0,owner.calls_);
  // Generic wrapper copies retain inline integers without any old 8-byte
  // payload allocation. An actual Reference remains an owned-copy OOM seam.
  DatumValue output=integer(123,source);
  ASSERT_EQ(OB_SUCCESS,stat.copy_min_value(output,owner));
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),output.get_int());
  EXPECT_EQ(0,owner.calls_);
  ObOptColumnStat reference_stat(source);
  DatumValue reference;
  ASSERT_EQ(OB_SUCCESS,reference.set_varchar(ObString::make_string("owned stats endpoint"),source));
  reference.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,reference_stat.set_min_value(reference));
  ASSERT_GT(reference_stat.get_min_datum().length(),0);
  output=integer(123,source);
  const Datum previous_output=output.datum_;
  const SqlTypeMeta previous_meta=output.meta_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,reference_stat.copy_min_value(output,owner));
  EXPECT_EQ(1,owner.calls_);
  EXPECT_EQ(0,MEMCMP(&previous_output,&output.datum_,sizeof(previous_output)));
  EXPECT_EQ(0,MEMCMP(&previous_meta,&output.meta_,sizeof(previous_meta)));
  EXPECT_EQ(123,output.get_int());
  source.reuse();std::memset(source.alloc(65536),0x73,65536);
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),decoded.get_min_datum().get_int());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),decoded.get_max_datum().get_uint());
  EXPECT_EQ(std::numeric_limits<int64_t>::min(),minimum.get_datum().get_int());
  EXPECT_EQ(std::numeric_limits<uint64_t>::max(),maximum.get_datum().get_uint());
}

TEST(NativeColumnStats, generic_reference_metadata_and_actual_decimal_width_survive_owner_reset)
{
  ObArenaAllocator source,owner;
  ObOptColumnStat stat(owner);
  stat.set_table_id(998);stat.set_partition_id(11);stat.set_column_id(16);
  int64_t questionmark=37;
  ObDatum old_unknown;old_unknown.ptr_=reinterpret_cast<char *>(&questionmark);old_unknown.set_int(questionmark);
  SqlTypeMeta unknown_meta;unknown_meta.set_unknown();
  Datum borrowed;
  ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::borrow_schema_native_value(old_unknown,unknown_meta,borrowed));
  EXPECT_EQ(sizeof(questionmark),borrowed.length());EXPECT_EQ(&questionmark,borrowed.get_reference());
  ASSERT_EQ(OB_SUCCESS,stat.set_min_native(borrowed,unknown_meta));
  EXPECT_NE(&questionmark,stat.get_min_datum().get_reference());
  Datum unchanged;unchanged.set_int(123);
  ObDatum malformed=old_unknown;malformed.len_=7;
  EXPECT_EQ(OB_INVALID_ARGUMENT,DatumParameterCodec::borrow_schema_native_value(malformed,unknown_meta,unchanged));
  EXPECT_EQ(123,unchanged.get_int());
  questionmark=99;
  alignas(8) char scratch[64];DatumValue exported;
  ASSERT_EQ(OB_SUCCESS,stat.export_min_value(exported,scratch,sizeof(scratch)));
  EXPECT_EQ(37,exported.get_unknown());same_meta(unknown_meta,exported.meta_);

  for(int32_t width:{0,4,8,16,32,64}) {
    SCOPED_TRACE(width);
    alignas(8) char coefficient[64];
    for(int i=0;i<64;++i){coefficient[i]=static_cast<char>(i+1);}
    DatumValue decimal;
    ASSERT_EQ(OB_SUCCESS,decimal.set_decimal_int(width,5,
        reinterpret_cast<const ObDecimalInt *>(coefficient),source));
    const std::string expected=value_wire(decimal);
    ASSERT_EQ(OB_SUCCESS,stat.set_min_value(decimal));
    EXPECT_EQ(width,stat.get_min_datum().length());same_meta(decimal.meta_,stat.get_min_meta());
    std::vector<char> cache(stat.size());ObIKVCacheValue *base=nullptr;
    ASSERT_EQ(OB_SUCCESS,stat.deep_copy(cache.data(),cache.size(),base));
    const auto *cached=static_cast<const ObOptColumnStat *>(base);
    if(width>0){EXPECT_NE(decimal.datum_.get_reference(),stat.get_min_datum().get_reference());}
    source.reuse();std::memset(source.alloc(65536),0x45,65536);
    ASSERT_EQ(OB_SUCCESS,stat.export_min_value(exported,scratch,sizeof(scratch)));
    EXPECT_EQ(width,exported.datum_.length());EXPECT_EQ(expected,value_wire(exported));
    EXPECT_EQ(expected,min_wire(*cached));
    if(width>0){EXPECT_EQ(0,MEMCMP(coefficient,exported.datum_.get_reference(),width));}
  }
  Datum null_value;SqlTypeMeta declared;declared.set_decimal_int(7);
  declared.set_collation_level(CS_LEVEL_COERCIBLE);declared.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS,stat.set_min_native(null_value,declared));
  EXPECT_TRUE(stat.get_min_datum().is_null());same_meta(declared,stat.get_min_meta());
  SqlTypeMeta cleared=declared;cleared.set_null();
  stat.clear_min_value();same_meta(cleared,stat.get_min_meta());
  ObGlobalColumnStat global;
  ASSERT_EQ(OB_SUCCESS,global.export_min_value(exported,scratch,sizeof(scratch)));EXPECT_TRUE(exported.is_min_value());
  ASSERT_EQ(OB_SUCCESS,global.export_max_value(exported,scratch,sizeof(scratch)));EXPECT_TRUE(exported.is_max_value());
  ASSERT_EQ(OB_SUCCESS,global.set_min_native(null_value,declared,owner));
  same_meta(declared,global.get_min_meta());global.clear_min_value();same_meta(cleared,global.get_min_meta());
}

TEST(NativeColumnStats, real_cache_handles_keep_native_bounds_and_endpoints_after_erase)
{
  // This fixture exclusively initializes the actual bounded global cache.
  // Define its guard first so all cache handles and the cache die before teardown.
  struct CacheLifetime {
    bool owned=false;
    ~CacheLifetime(){if(owned){ObKVGlobalCache::get_instance().destroy();}}
  } lifetime;
  ASSERT_EQ(OB_SUCCESS,ObKVGlobalCache::get_instance().init(1024,64L*1024*1024,2L*1024*1024));
  lifetime.owned=true;
  ObOptColumnStatCache cache;
  ASSERT_EQ(OB_SUCCESS,cache.init("NativeStatR"));
  ObOptColumnStatHandle first,copied,moved,hit,replacement;
  ObOptColumnStat::Key key(1001,0,16);
  ObArenaAllocator source,stat_owner;
  DatumValue low,high=integer(71,source);
  ASSERT_EQ(OB_SUCCESS,low.set_varchar(ObString("cache endpoint"),source));
  low.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObOptColumnStat stat(stat_owner);init(stat,low,high,16);
  ObHistogram &hist=stat.get_histogram();
  hist.set_type(ObHistType::FREQUENCY);hist.set_sample_size(2);hist.set_bucket_cnt(0);hist.set_density(0.5);
  ASSERT_EQ(OB_SUCCESS,hist.prepare_allocate_buckets(stat_owner,1));
  ASSERT_EQ(OB_SUCCESS,hist.add_bucket(hist_bucket(low,2,2),stat_owner));
  const std::string expected=wire(stat);
  ASSERT_EQ(OB_SUCCESS,cache.put_and_fetch_row(key,stat,first));ASSERT_NE(nullptr,first.stat_);
  ASSERT_EQ(OB_SUCCESS,copied.assign(first));moved.move_from(copied);EXPECT_EQ(nullptr,copied.stat_);
  ASSERT_EQ(OB_SUCCESS,cache.get_row(key,hit));ASSERT_NE(nullptr,hit.stat_);
  EXPECT_NE(stat.get_min_datum().get_reference(),first.stat_->get_min_datum().get_reference());
  source.reuse();std::memset(source.alloc(65536),0x62,65536);
  stat.reset();stat_owner.reuse();std::memset(stat_owner.alloc(65536),0x63,65536);
  EXPECT_EQ(expected,wire(*first.stat_));EXPECT_EQ(expected,wire(*moved.stat_));EXPECT_EQ(expected,wire(*hit.stat_));
  EXPECT_EQ(ObString("cache endpoint"),first.stat_->get_histogram().get(0).get_endpoint_datum().get_string());
  ObOptColumnStat newer;DatumValue one=integer(1,source);init(newer,one,one);
  ASSERT_EQ(OB_SUCCESS,cache.put_and_fetch_row(key,newer,replacement));
  EXPECT_EQ(1,replacement.stat_->get_min_datum().get_int());EXPECT_EQ(expected,wire(*first.stat_));
  ASSERT_EQ(OB_SUCCESS,cache.erase(key));
  EXPECT_EQ(expected,wire(*first.stat_));EXPECT_EQ(expected,wire(*moved.stat_));EXPECT_EQ(1,replacement.stat_->get_min_datum().get_int());
  ObOptColumnStatHandle missing;EXPECT_EQ(OB_ENTRY_NOT_EXIST,cache.get_row(key,missing));
  EXPECT_EQ(nullptr,missing.stat_);
}


TEST(NativeColumnStats, osg_inline_extrema_and_scalar_copy_need_no_payload_allocation)
{
  ObArenaAllocator owner;
  ObOptColumnStat stat(owner);
  ObOptOSGColumnStat osg(owner); osg.col_stat_ = &stat;
  for (int64_t number : {int64_t(7), INT64_MIN, INT64_MAX, int64_t(-3)}) {
    DatumValue value = integer(number, owner);
    ASSERT_EQ(OB_SUCCESS, update_osg_literal(osg, value, ObAccuracy(19, 0)));
  }
  EXPECT_EQ(0, osg.inner_min_allocator_.used());
  EXPECT_EQ(0, osg.inner_max_allocator_.used());
  EXPECT_EQ(INT64_MIN, osg.min_val_.datum_.get_int());
  EXPECT_EQ(INT64_MAX, osg.max_val_.datum_.get_int());
  EXPECT_EQ(4, stat.get_num_not_null());
  DatumValue null_integer; null_integer.meta_.set_int(); null_integer.datum_.set_null();
  ASSERT_EQ(OB_SUCCESS, update_osg_literal(osg, null_integer, ObAccuracy(19, 0)));
  EXPECT_EQ(1, stat.get_num_null());
  EXPECT_EQ(INT64_MIN, osg.min_val_.datum_.get_int());
  EXPECT_EQ(INT64_MAX, osg.max_val_.datum_.get_int());

  SwitchAllocator reject; reject.reject_ = true;
  ObMinMaxValEval copied(T_MIN_VAL);
  ASSERT_EQ(OB_SUCCESS, copied.deep_copy(osg.min_val_, reject));
  EXPECT_EQ(0, reject.calls_);
  EXPECT_EQ(INT64_MIN, copied.datum_.get_int());
  ASSERT_EQ(OB_SUCCESS, osg.publish_min_max_values());
  EXPECT_EQ(INT64_MIN, stat.get_min_datum().get_int());
  EXPECT_EQ(INT64_MAX, stat.get_max_datum().get_int());
}

TEST(NativeColumnStats, osg_reference_capture_merge_and_copy_outlive_both_source_owners)
{
  ObArenaAllocator input, source_owner, copy_owner, merge_owner;
  ObOptColumnStat source_stat(source_owner), copied_stat(copy_owner), merged_stat(merge_owner);
  ObOptOSGColumnStat source(source_owner), copied(copy_owner), merged(merge_owner);
  source.col_stat_ = &source_stat; copied.col_stat_ = &copied_stat; merged.col_stat_ = &merged_stat;
  DatumValue high, low;
  ASSERT_EQ(OB_SUCCESS, high.set_varchar(ObString::make_string("zebra"), input));
  ASSERT_EQ(OB_SUCCESS, low.set_varchar(ObString::make_string("alpha"), input));
  high.set_collation_type(CS_TYPE_UTF8MB4_BIN); low.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, update_osg_literal(source, high));
  ASSERT_EQ(OB_SUCCESS, update_osg_literal(source, low));
  EXPECT_NE(low.datum_.get_reference(), source.min_val_.datum_.get_reference());
  EXPECT_NE(high.datum_.get_reference(), source.max_val_.datum_.get_reference());
  ASSERT_EQ(OB_SUCCESS, copied.deep_copy(source));
  ASSERT_EQ(OB_SUCCESS, merged.merge_column_stat(source));
  EXPECT_NE(source.min_val_.datum_.get_reference(), copied.min_val_.datum_.get_reference());
  EXPECT_NE(source.max_val_.datum_.get_reference(), merged.max_val_.datum_.get_reference());
  MEMSET(const_cast<void *>(low.datum_.get_reference()), 0xA5, low.datum_.length());
  MEMSET(const_cast<void *>(high.datum_.get_reference()), 0xA5, high.datum_.length());
  input.reset();
  source.reset();
  ASSERT_EQ(OB_SUCCESS, copied.publish_min_max_values());
  ASSERT_EQ(OB_SUCCESS, merged.publish_min_max_values());
  EXPECT_EQ(ObString::make_string("alpha"), copied_stat.get_min_datum().get_string());
  EXPECT_EQ(ObString::make_string("zebra"), copied_stat.get_max_datum().get_string());
  EXPECT_EQ(ObString::make_string("alpha"), merged_stat.get_min_datum().get_string());
  EXPECT_EQ(ObString::make_string("zebra"), merged_stat.get_max_datum().get_string());
  same_meta(low.meta_, copied_stat.get_min_meta());
  same_meta(high.meta_, merged_stat.get_max_meta());
}

TEST(NativeColumnStats, osg_reference_copy_failure_preserves_previous_native_slot)
{
  ObArenaAllocator input, owner;
  ObOptColumnStat stat(owner);
  ObOptOSGColumnStat osg(owner); osg.col_stat_ = &stat;
  DatumValue text;
  ASSERT_EQ(OB_SUCCESS, text.set_varchar(ObString::make_string("reference owned by minmax"), input));
  text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, update_osg_literal(osg, text));
  ObMinMaxValEval destination(T_MIN_VAL);
  destination.datum_.set_int(91); destination.meta_.set_int(); destination.has_datum_ = true;
  const SqlTypeMeta before_meta = destination.meta_;
  SwitchAllocator reject; reject.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, destination.deep_copy(osg.min_val_, reject));
  EXPECT_EQ(1, reject.calls_);
  EXPECT_EQ(91, destination.datum_.get_int());
  same_meta(before_meta, destination.meta_);
  EXPECT_TRUE(destination.has_datum_);
  EXPECT_EQ(nullptr, destination.cmp_func_);
  EXPECT_EQ(ObString::make_string("reference owned by minmax"), osg.min_val_.datum_.get_string());
}


TEST(NativeColumnStats, osg_semantic_inline_decimal_is_retained_as_owned_physical_width)
{
  ObArenaAllocator input, owner;
  ObOptColumnStat stat(owner);
  ObOptOSGColumnStat osg(owner); osg.col_stat_ = &stat;
  int32_t coefficient = -42;
  DatumValue physical_value;
  ASSERT_EQ(OB_SUCCESS, physical_value.set_decimal_int(sizeof(coefficient), 2,
      reinterpret_cast<const ObDecimalInt *>(&coefficient), input));
  SqlType declared;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(physical_value.meta_, ObAccuracy(9, 2), declared));
  alignas(8) char scratch[64]; ObDatum physical;
  Datum semantic;
  ASSERT_EQ(OB_SUCCESS, physical_value.export_legacy(physical, scratch, sizeof(scratch)));
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(declared).import_legacy(physical, semantic));
  EXPECT_EQ(0, semantic.length());
  EXPECT_EQ(-42, semantic.get_decimal_int32());
  ASSERT_EQ(OB_SUCCESS, osg.update_column_stat_info(
      &semantic, declared, physical_value.meta_, nullptr));
  EXPECT_EQ(sizeof(coefficient), osg.min_val_.datum_.length());
  EXPECT_EQ(sizeof(coefficient), osg.max_val_.datum_.length());
  EXPECT_NE(physical_value.datum_.get_reference(), osg.min_val_.datum_.get_reference());
  EXPECT_NE(osg.min_val_.datum_.get_reference(), osg.max_val_.datum_.get_reference());
  EXPECT_EQ(0, MEMCMP(&coefficient, osg.min_val_.datum_.get_reference(), sizeof(coefficient)));
  same_meta(physical_value.meta_, osg.min_val_.meta_);
  input.reset(); semantic.set_int(999);
  ASSERT_EQ(OB_SUCCESS, osg.publish_min_max_values());
  EXPECT_EQ(sizeof(coefficient), stat.get_min_datum().length());
  EXPECT_EQ(sizeof(coefficient), stat.get_max_datum().length());
  EXPECT_EQ(0, MEMCMP(&coefficient, stat.get_min_datum().get_reference(), sizeof(coefficient)));
  EXPECT_EQ(0, MEMCMP(&coefficient, stat.get_max_datum().get_reference(), sizeof(coefficient)));
}
