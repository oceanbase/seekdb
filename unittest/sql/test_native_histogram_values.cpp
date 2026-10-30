/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/optimizer/stat/ob_hybrid_hist_estimator.h"
#include "sql/optimizer/stat/ob_incremental_stat_estimator.h"
#include "sql/optimizer/stat/ob_topk_hist_estimator.h"
#include "sql/optimizer/stat/ob_stat_item.h"
#include "sql/optimizer/ob_opt_selectivity.h"
#include "sql/optimizer/ob_optimizer_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/executor/ob_execute_result.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#include "share/datum/typed_datum_compare.h"
#include "share/datum/typed_datum_murmur.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> result;std::istringstream in(line);std::string field;
  while(std::getline(in,field,'\t')){result.push_back(field);}return result;
}
std::string unhex(const std::string &s) {
  std::string out;if(s!="-"){for(size_t i=0;i<s.size();i+=2){out.push_back(std::stoi(s.substr(i,2),nullptr,16));}}return out;
}
std::string hex(const char *p,int64_t n) {
  const char*d="0123456789abcdef";std::string s;for(int64_t i=0;i<n;++i){uint8_t c=p[i];s+=d[c>>4];s+=d[c&15];}return s.empty()?"-":s;
}
template<class T>std::string bytes(const T &value) {
  std::string b(value.get_serialize_size()+64,'\0');int64_t p=0;
  EXPECT_EQ(OB_SUCCESS,value.serialize(&b[0],b.size(),p));b.resize(p);return b;
}
template<class T>std::string wire(const T &value) {const auto b=bytes(value);return hex(b.data(),b.size());}
std::string value_wire(const DatumValue &v) {
  int64_t n=0,p=0;EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(v.datum_,v.meta_,n));
  std::string b(n+64,'\0');EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_value(v.datum_,v.meta_,&b[0],b.size(),p));return hex(b.data(),p);
}
DatumValue integer(int64_t n,ObIAllocator &owner) {DatumValue v;EXPECT_EQ(OB_SUCCESS,v.set_int(n,owner));return v;}
const int64_t freq[]={37,19,13,11,7,5,3,2};
void init_topk(ObTopKFrequencyHistograms &h,bool merge) {
  h.set_window_size(2048);h.set_item_size(8);h.set_is_topk_hist_need_des_row(merge);
}
void build_topk(ObTopKFrequencyHistograms &h,const std::vector<DatumValue> &values,int group,int mode) {
  init_topk(h,mode!=1);
  if(mode==2){
    ObTopKFrequencyHistograms source;init_topk(source,true);
    for(int i=0;i<8;++i){ASSERT_EQ(OB_SUCCESS,source.add_top_k_frequency_item(values.at(group*8+i),freq[i]));}
    ASSERT_EQ(OB_SUCCESS,source.create_topk_fre_items());std::string b=bytes(source);ObArenaAllocator blob_owner;DatumValue blob;
    ASSERT_EQ(OB_SUCCESS,blob.set_string(ObLongTextType,ObString(b.size(),b.data()),blob_owner));
    ASSERT_EQ(OB_SUCCESS,h.merge_distribute_top_k_fre_items(blob));ASSERT_EQ(OB_SUCCESS,h.merge_distribute_top_k_fre_items(blob));
    ASSERT_EQ(OB_SUCCESS,h.shrink_memory_usage());
  }else{
    for(int i=0;i<8;++i){const auto &v=values.at(group*8+i);
      if(mode==0){ASSERT_EQ(OB_SUCCESS,h.add_top_k_frequency_item(v,freq[i]));}
      else {ObDatum d;char extended[DATUM_EXTEND_PAYLOAD_SIZE];ASSERT_EQ(OB_SUCCESS,v.to_row_datum(d,extended));uint64_t hash=0;
        ASSERT_EQ(OB_SUCCESS,hash_datum_value_murmur(v,0,hash));for(int j=0;j<freq[i];++j){ASSERT_EQ(OB_SUCCESS,h.add_top_k_frequency_item(hash,d));}}
    }
  }
  ASSERT_EQ(OB_SUCCESS,h.create_topk_fre_items(mode==1?&values.at(group*8).get_meta():nullptr));
}
void fill_hist(ObHistogram &h,const std::vector<DatumValue> &values,int group,ObIAllocator &owner) {
  h.set_type(FREQUENCY);h.set_sample_size(97);h.set_density(0.001);ASSERT_EQ(OB_SUCCESS,h.prepare_allocate_buckets(owner,8));
  int64_t sum=0;for(int i=0;i<8;++i){sum+=freq[i];ASSERT_EQ(OB_SUCCESS,h.add_bucket(ObHistBucket(values.at(group*8+i),freq[i],sum),owner));}
}
class SwitchAllocator:public ObIAllocator {
public:
 void *alloc(int64_t n)override{return reject_?nullptr:arena_.alloc(n);}
 void *alloc(int64_t n,const ObMemAttr&)override{return alloc(n);}
 void free(void*p)override{arena_.free(p);}
 bool reject_=false;ObArenaAllocator arena_;
};
}

TEST(NativeHistogramValues, immutable_old_build_wire_decode_selectivity_and_hash) {
  std::ifstream input("unittest/sql/data/native_histogram_values.golden");ASSERT_TRUE(input.good());
  ObArenaAllocator owner;std::vector<DatumValue> values;std::string line;int hc=0,tc=0,bc=0,lc=0,dc=0;
  ObGlobalHint hint;ObRawExprFactory factory(owner);ParamStore params;ObAddr addr;
  ObOptimizerContext opt(nullptr,nullptr,nullptr,nullptr,owner,&params,addr,hint,factory,nullptr,false);OptSelectivityCtx ctx(opt,nullptr,nullptr);
  while(std::getline(input,line)) {
    if(line.empty()||line[0]=='#'){continue;}const auto f=split(line);SCOPED_TRACE(line);
    if(f.at(0)=="V"){
      ASSERT_EQ(values.size(),std::stoi(f.at(1)));std::string b=unhex(f.at(4));DatumValue v;int64_t p=0;
      ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(b.data(),b.size(),p,owner,v.datum_,v.meta_));
      ASSERT_EQ(b.size(),p);values.push_back(v);EXPECT_EQ(f.at(4),value_wire(v));
    }else if(f[0]=="H"){
      int group=std::stoi(f[1]),buckets=std::stoi(f[2]);ObSEArray<BucketNode,16> pairs;
      for(int i=0;i<8;++i){ASSERT_EQ(OB_SUCCESS,pairs.push_back(BucketNode(values.at(group*8+i),freq[i])));}
      ObHybridHistograms h;int ret=h.build_hybrid_hist(pairs,buckets,97,8);ASSERT_EQ(std::stoi(f[3]),ret);
      if(ret==OB_SUCCESS){EXPECT_EQ(f[4],wire(h));auto b=unhex(f[4]);int64_t p=0;ObHybridHistograms decoded;
        ASSERT_EQ(OB_SUCCESS,decoded.deserialize(b.data(),b.size(),p));EXPECT_EQ(b.size(),p);
        std::fill(b.begin(),b.end(),'\x7f');EXPECT_EQ(f[4],wire(decoded));}
      ++hc;
    }else if(f[0]=="T"){
      ObTopKFrequencyHistograms h;build_topk(h,values,std::stoi(f[1]),std::stoi(f[2]));ASSERT_EQ(OB_SUCCESS,std::stoi(f[3]));EXPECT_EQ(f[4],wire(h));
      auto b=unhex(f[4]);int64_t p=0;ObTopKFrequencyHistograms decoded;
      ASSERT_EQ(OB_SUCCESS,decoded.deserialize(b.data(),b.size(),p));EXPECT_EQ(b.size(),p);
      std::fill(b.begin(),b.end(),'\x7f');EXPECT_EQ(f[4],wire(decoded));++tc;
    }else if(f[0]=="B"){
      ObHistogram h;fill_hist(h,values,std::stoi(f[1]),owner);int64_t idx=-99;bool equal=false;
      int ret=ObOptSelectivity::get_bucket_bound_idx(h,values.at(std::stoi(f[2])),idx,equal);
      EXPECT_EQ(std::stoi(f[3]),ret);EXPECT_EQ(std::stoll(f[4]),idx);EXPECT_EQ(std::stoi(f[5])!=0,equal);++bc;
    }else if(f[0]=="L"){
      ObHistogram h;fill_hist(h,values,std::stoi(f[1]),owner);double density=-1;
      int ret=ObOptSelectivity::get_less_pred_sel(ctx,h,values.at(std::stoi(f[2])),std::stoi(f[3])!=0,density);
      EXPECT_EQ(std::stoi(f[4]),ret);if(ret==OB_SUCCESS){EXPECT_DOUBLE_EQ(std::stod(f[5]),density);}++lc;
    }else if(f[0]=="D"){
      const auto &v=values.at(std::stoi(f[1]));ObDatum d;char extended[DATUM_EXTEND_PAYLOAD_SIZE];int ret=v.to_row_datum(d,extended);
      EXPECT_EQ(std::stoi(f[2]),ret);if(ret==OB_SUCCESS){EXPECT_EQ(std::stoull(f[3]),d.pack_);EXPECT_EQ(f[4],hex(d.ptr_,d.len_));}
      uint64_t hash=0;int hr=hash_datum_value_murmur(v,0,hash);EXPECT_EQ(std::stoi(f[5]),hr);if(hr==OB_SUCCESS){EXPECT_EQ(std::stoull(f[6]),hash);}++dc;
    }
  }
  EXPECT_EQ(95,values.size());EXPECT_EQ(18,hc);EXPECT_EQ(18,tc);EXPECT_EQ(120,bc);EXPECT_EQ(204,lc);EXPECT_EQ(95,dc);
}

TEST(NativeHistogramValues, publication_owns_after_hybrid_and_input_owners_are_destroyed) {
  ObArenaAllocator owner;ObHistogram published;std::vector<std::string> expected;
  {
    ObHybridHistograms hybrid;
    {
      ObArenaAllocator input;ObSEArray<BucketNode,16> pairs;
      for(int i=0;i<8;++i){DatumValue v;const std::string s=std::string("customer_")+std::to_string(i)+"_客户";
        ASSERT_EQ(OB_SUCCESS,v.set_varchar(ObString(s.size(),s.data()),input));v.set_collation_type(CS_TYPE_UTF8MB4_BIN);
        ASSERT_EQ(OB_SUCCESS,pairs.push_back(BucketNode(v,freq[i])));}
      ASSERT_EQ(OB_SUCCESS,hybrid.build_hybrid_hist(pairs,6,97,8));
      for(int64_t i=0;i<hybrid.get_buckets().count();++i){expected.push_back(value_wire(hybrid.get_buckets().at(i).endpoint_value_));}
      input.reuse();MEMSET(input.alloc(65536),0x7f,65536);
      for(int64_t i=0;i<hybrid.get_buckets().count();++i){EXPECT_EQ(expected.at(i),value_wire(hybrid.get_buckets().at(i).endpoint_value_));}
    }
    ASSERT_EQ(OB_SUCCESS,published.prepare_allocate_buckets(owner,hybrid.get_buckets().count()));
    published.set_bucket_cnt(hybrid.get_buckets().count());ASSERT_EQ(OB_SUCCESS,published.assign_buckets(hybrid.get_buckets(),owner));
  }
  EXPECT_EQ(expected.size(),published.get_bucket_size());
  for(int64_t i=0;i<published.get_bucket_size();++i){EXPECT_EQ(expected.at(i),value_wire(published.get(i).endpoint_value_));}
}

TEST(NativeHistogramValues, topk_clones_inputs_and_alternating_compaction_owners) {
  ObTopKFrequencyHistograms h;init_topk(h,true);
  {
    ObArenaAllocator input;
    for(int i=0;i<8;++i){DatumValue v;const std::string s=std::string("long_customer_")+std::to_string(i)+std::string(1000,'a'+i);
      ASSERT_EQ(OB_SUCCESS,v.set_varchar(ObString(s.size(),s.data()),input));v.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      ASSERT_EQ(OB_SUCCESS,h.add_top_k_frequency_item(v,freq[i]));}
    ASSERT_EQ(OB_SUCCESS,h.shrink_memory_usage());ASSERT_EQ(OB_SUCCESS,h.create_topk_fre_items());
  }
  const std::string expected=wire(h);
  for(int i=0;i<4;++i){ASSERT_EQ(OB_SUCCESS,h.shrink_memory_usage());ASSERT_EQ(OB_SUCCESS,h.create_topk_fre_items());EXPECT_EQ(expected,wire(h));}
}

TEST(NativeHistogramValues, endpoint_add_failure_does_not_publish_and_cache_payload_is_aligned) {
  ObArenaAllocator input;SwitchAllocator owner;ObOptColumnStat stat(owner);stat.set_table_id(1001);auto &h=stat.get_histogram();
  ASSERT_EQ(OB_SUCCESS,h.prepare_allocate_buckets(owner,2));DatumValue a,b;
  ASSERT_EQ(OB_SUCCESS,a.set_varchar(ObString("odd"),input));ASSERT_EQ(OB_SUCCESS,b.set_int(987654321,input));
  ASSERT_EQ(OB_SUCCESS,h.add_bucket(ObHistBucket(a,1,1),owner));owner.reject_=true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,h.add_bucket(ObHistBucket(b,1,2),owner));EXPECT_EQ(1,h.get_bucket_cnt());
  owner.reject_=false;ASSERT_EQ(OB_SUCCESS,h.add_bucket(ObHistBucket(b,1,2),owner));h.set_type(FREQUENCY);h.set_sample_size(2);
  std::vector<char> buffer(stat.size()+1);ObIKVCacheValue *value=nullptr;
  ASSERT_EQ(OB_SUCCESS,stat.deep_copy(buffer.data(),buffer.size(),value));auto *copied=static_cast<ObOptColumnStat *>(value);
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(&copied->get_histogram().get(0))%alignof(ObHistBucket));
  EXPECT_EQ(0,reinterpret_cast<uintptr_t>(copied->get_histogram().get(1).endpoint_value_.datum_.ptr_)%8);
  owner.arena_.reuse();input.reuse();MEMSET(input.alloc(65536),0x6f,65536);
  EXPECT_EQ("odd",std::string(copied->get_histogram().get(0).endpoint_value_.get_string().ptr(),3));
  EXPECT_EQ(987654321,copied->get_histogram().get(1).endpoint_value_.get_int());
}

TEST(NativeHistogramValues, raw_row_projection_validates_meta_and_retains_outrow) {
  ObArenaAllocator owner;DatumValue v;ASSERT_EQ(OB_SUCCESS,v.set_string(ObLongTextType,ObString("odd_payload"),owner));
  ObDatum row=v.datum_;row.set_outrow();DatumValue projected;
  ASSERT_EQ(OB_SUCCESS,ResultRowView::project_value(row,v.meta_,OBJ_DATUM_STRING,SqlTypeMeta(),projected,true));
  EXPECT_TRUE(projected.meta_.is_outrow());char extended[DATUM_EXTEND_PAYLOAD_SIZE];ObDatum rebuilt;
  ASSERT_EQ(OB_SUCCESS,projected.to_row_datum(rebuilt,extended));EXPECT_TRUE(rebuilt.is_outrow());
  ObDatum null_row;null_row.set_null();v.meta_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS,ResultRowView::project_value(null_row,v.meta_,OBJ_DATUM_STRING,SqlTypeMeta(),projected,true));
  EXPECT_TRUE(projected.is_null());EXPECT_EQ(CS_TYPE_UTF8MB4_GENERAL_CI,projected.get_collation_type());
  int64_t coefficient=1;ObDatum decimal;decimal.ptr_=reinterpret_cast<char*>(&coefficient);decimal.pack_=8;
  SqlTypeMeta bad_meta;bad_meta.set_decimal_int();bad_meta.set_scale(-1);
  EXPECT_EQ(OB_ERR_UNEXPECTED,ResultRowView::project_value(decimal,bad_meta,OBJ_DATUM_DECIMALINT,SqlTypeMeta(),projected,true));
}

TEST(NativeHistogramValues, raw_topk_projection_rejects_negative_decimal_scale) {
  ObTopKFrequencyHistograms h;init_topk(h,false);int64_t coefficient=1;
  ObDatum decimal;decimal.ptr_=reinterpret_cast<char*>(&coefficient);decimal.pack_=8;
  ASSERT_EQ(OB_SUCCESS,h.add_top_k_frequency_item(12345,decimal));
  SqlTypeMeta invalid;invalid.set_decimal_int(-1);
  EXPECT_EQ(OB_ERR_UNEXPECTED,h.create_topk_fre_items(&invalid));
}

TEST(NativeHistogramValues, topk_publication_owns_after_temporary_estimator_is_destroyed) {
  ObArenaAllocator owner;ObHistogram published;std::vector<std::string> expected;
  {
    ObTopKFrequencyHistograms source;init_topk(source,true);ObArenaAllocator input;
    for(int i=0;i<8;++i){DatumValue v;const std::string s="customer_"+std::to_string(i);
      ASSERT_EQ(OB_SUCCESS,v.set_varchar(ObString(s.size(),s.data()),input));v.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      ASSERT_EQ(OB_SUCCESS,source.add_top_k_frequency_item(v,freq[i]));}
    ASSERT_EQ(OB_SUCCESS,source.create_topk_fre_items());
    ASSERT_EQ(OB_SUCCESS,ObStatTopKHist::build_histogram_from_topk_items(owner,source.get_buckets(),8,97,97,8,published));
    for(int64_t i=0;i<published.get_bucket_size();++i){expected.push_back(value_wire(published.get(i).endpoint_value_));}
  }
  EXPECT_EQ(8,published.get_bucket_size());
  for(int64_t i=0;i<published.get_bucket_size();++i){EXPECT_EQ(expected.at(i),value_wire(published.get(i).endpoint_value_));}
}

TEST(NativeHistogramValues, histogram_assign_owns_endpoint_and_gather_metadata) {
  ObArenaAllocator owner;ObHistogram target;std::string expected;
  {
    ObArenaAllocator source;ObHistogram h;DatumValue v;ASSERT_EQ(OB_SUCCESS,v.set_varchar(ObString("odd_endpoint"),source));
    ASSERT_EQ(OB_SUCCESS,h.prepare_allocate_buckets(source,1));ASSERT_EQ(OB_SUCCESS,h.add_bucket(ObHistBucket(v,7,9),source));
    h.set_type(HYBIRD);h.set_sample_size(9);h.set_density(0.125);h.set_pop_frequency(7);h.set_pop_count(1);
    expected=value_wire(v);ASSERT_EQ(OB_SUCCESS,target.assign(h,owner));
    source.reuse();MEMSET(source.alloc(65536),0x4e,65536);
  }
  EXPECT_EQ(HYBIRD,target.get_type());EXPECT_EQ(9,target.get_sample_size());EXPECT_DOUBLE_EQ(0.125,target.get_density());
  EXPECT_EQ(7,target.get_pop_frequency());EXPECT_EQ(1,target.get_pop_count());EXPECT_EQ(expected,value_wire(target.get(0).endpoint_value_));
  SwitchAllocator rejected;rejected.reject_=true;ObHistogram other;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,other.assign(target,rejected));EXPECT_EQ(0,other.get_bucket_size());
  ObHistogram empty;ASSERT_EQ(OB_SUCCESS,target.assign(empty,owner));EXPECT_EQ(0,target.get_bucket_size());EXPECT_EQ(0,target.get_bucket_cnt());
}

namespace {
struct DeriveNativeHistogramTag {
  using type = int (*)(ObIArray<const ObHistogram *> &, ObIAllocator &, int64_t,
                       int64_t, int64_t, int64_t, ObHistogram &, bool &);
  friend type access(DeriveNativeHistogramTag);
};
template <class Tag, typename Tag::type Function> struct HistogramPrivateAccess {
  friend typename Tag::type access(Tag) { return Function; }
};
template struct HistogramPrivateAccess<DeriveNativeHistogramTag,
    &ObIncrementalStatEstimator::derive_global_histogram>;
}

TEST(NativeHistogramValues, incremental_merge_borrows_inputs_and_owns_published_endpoints) {
  ObArenaAllocator destination;
  ObHistogram merged;
  bool need_gather = false;
  {
    ObArenaAllocator source;
    DatumValue one = integer(1, source), two = integer(2, source);
    ObHistogram first, second;
    first.set_type(FREQUENCY); first.set_sample_size(5);
    second.set_type(FREQUENCY); second.set_sample_size(5);
    ASSERT_EQ(OB_SUCCESS, first.prepare_allocate_buckets(source, 2));
    ASSERT_EQ(OB_SUCCESS, second.prepare_allocate_buckets(source, 2));
    ASSERT_EQ(OB_SUCCESS, first.add_bucket(ObHistBucket(one, 3, 3), source));
    ASSERT_EQ(OB_SUCCESS, first.add_bucket(ObHistBucket(two, 2, 5), source));
    ASSERT_EQ(OB_SUCCESS, second.add_bucket(ObHistBucket(one, 1, 1), source));
    ASSERT_EQ(OB_SUCCESS, second.add_bucket(ObHistBucket(two, 4, 5), source));
    ObSEArray<const ObHistogram *, 4> parts;
    ASSERT_EQ(OB_SUCCESS, parts.push_back(&first));
    ASSERT_EQ(OB_SUCCESS, parts.push_back(&second));
    ASSERT_EQ(OB_SUCCESS, access(DeriveNativeHistogramTag{})(
        parts, destination, 4, 10, 10, 2, merged, need_gather));
  }
  EXPECT_FALSE(need_gather);
  ASSERT_EQ(FREQUENCY, merged.get_type());
  ASSERT_EQ(2, merged.get_bucket_cnt());
  EXPECT_EQ(1, merged.get(0).endpoint_value_.get_int());
  EXPECT_EQ(2, merged.get(1).endpoint_value_.get_int());
  EXPECT_EQ(4, merged.get(0).endpoint_repeat_count_);
  EXPECT_EQ(6, merged.get(1).endpoint_repeat_count_);
  EXPECT_EQ(4, merged.get(0).endpoint_num_);
  EXPECT_EQ(10, merged.get(1).endpoint_num_);
}

TEST(NativeHistogramValues, default_and_explicit_murmur_hash_keep_distinct_old_contracts) {
  // Exact serialized value and hashes from two independent old-library captures:
  // typed_values_compare.golden V62 (hash), native_histogram_values.golden V8/D8 (hash_murmur).
  const auto encoded = unhex("16022eff016100");
  ObArenaAllocator owner;DatumValue value;int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,DatumParameterCodec::decode_value(encoded.data(),encoded.size(),pos,owner,value.datum_,value.meta_));
  ASSERT_EQ(encoded.size(),pos);
  uint64_t default_hash=0,murmur_hash=0;
  ASSERT_EQ(OB_SUCCESS,hash_datum_value(value,0,default_hash));
  ASSERT_EQ(OB_SUCCESS,hash_datum_value_murmur(value,0,murmur_hash));
  EXPECT_EQ(4927921873964597621ULL,default_hash);
  EXPECT_EQ(510903276987443985ULL,murmur_hash);
  EXPECT_NE(default_hash,murmur_hash);
}
