/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include "sql/das/iter/ob_das_mvi_lookup_iter.h"
#include "sql/das/iter/ob_das_scan_iter.h"
#include "sql/engine/ob_exec_context.h"
#include <gtest/gtest.h>
#include <array>
#include <algorithm>
#include <cstring>
#include <string>
using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
namespace oceanbase { namespace sql {
struct MviLookupTestAccess {
 static int configure(ObDASMVILookupIter &iter,const ObDASScanCtDef &index,const ObDASScanCtDef &data,
                      ObDASScanRtDef &runtime,ObDASScanIter &scan) {
  iter.index_ctdef_=&index;iter.lookup_ctdef_=&data;iter.lookup_rtdef_=&runtime;iter.data_table_iter_=&scan;
  lib::ContextParam param;param.set_mem_attr("MviLookupTest").set_properties(lib::USE_TL_PAGE_OPTIONAL);
  return CURRENT_CONTEXT->CREATE_CONTEXT(iter.lookup_memctx_,param);
 }
 static int save(ObDASMVILookupIter &iter) {return iter.save_rowkey();}
 static int first_lookup(ObDASMVILookupIter &iter,transaction::ObTxReadSnapshot &snapshot) {
  iter.snapshot_=&snapshot;
  return iter.ObDASLocalLookupIter::do_index_lookup();
 }
};
} }
namespace {
// Expose the actual close implementation; never replace its request cleanup.
class MviLookupScanProbe : public ObDASScanIter {
public:
 int first_scan_calls=0;
 bool validate_request=false;
 int do_table_scan() override {
  ++first_scan_calls;
  // Exercise the same public scan-request validation used by Storage before
  // opening its scan iterator; the lookup/init methods themselves are real.
  return !validate_request || get_scan_param().is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
 }
 int close_keys() {return inner_release();}
};
class MviLookupRejectAllocator : public ObIAllocator {
public:
 int calls=0;
 void *alloc(int64_t) override {++calls;return nullptr;}
 void *alloc(int64_t,const ObMemAttr &) override {++calls;return nullptr;}
 void free(void *) override {ADD_FAILURE()<<"failed publication must not free caller payloads";}
};
struct MviLookupFixture {
 ObArenaAllocator arena;
 ObExecContext exec;
 char *frames[1];
 ObEvalCtx *eval;
 ObExpr doc,dummy;
 ObDASScanCtDef index,data;
 ObDASScanRtDef runtime;
 ObTableScanParam param;
 MviLookupScanProbe *scan;
 ObDASMVILookupIter *lookup;
 MviLookupFixture():arena(),exec(arena),eval(nullptr),doc(),dummy(),index(arena),data(arena),runtime(),param(),scan(nullptr),lookup(nullptr) {
  frames[0]=static_cast<char *>(arena.alloc(2048));EXPECT_NE(nullptr,frames[0]);
  if(frames[0]==nullptr)return;
  MEMSET(frames[0],0,2048);exec.set_frames(frames);
  eval=new(arena.alloc(sizeof(ObEvalCtx)))ObEvalCtx(exec);runtime.eval_ctx_=eval;
  doc.frame_idx_=0;doc.datum_off_=64;doc.eval_info_off_=128;doc.res_buf_off_=256;doc.res_buf_len_=512;
  doc.type_=T_COLUMN_REF;doc.obj_meta_.set_varbinary();doc.datum_meta_.type_=ObVarcharType;
  doc.datum_meta_.cs_type_=CS_TYPE_BINARY;doc.max_length_=OB_DOC_ID_COLUMN_BYTE_LENGTH;doc.obj_datum_map_=OBJ_DATUM_STRING;
  new(frames[0]+64)ObDatum();
  EXPECT_EQ(OB_SUCCESS,index.rowkey_exprs_.init(1));EXPECT_EQ(OB_SUCCESS,index.result_output_.init(2));
  EXPECT_EQ(OB_SUCCESS,index.rowkey_exprs_.push_back(&dummy));
  EXPECT_EQ(OB_SUCCESS,index.result_output_.push_back(&dummy));EXPECT_EQ(OB_SUCCESS,index.result_output_.push_back(&doc));
  data.ref_table_id_=1000001;
  scan=new(arena.alloc(sizeof(MviLookupScanProbe)))MviLookupScanProbe();scan->set_scan_param(param);
  lookup=new(arena.alloc(sizeof(ObDASMVILookupIter)))ObDASMVILookupIter();
  EXPECT_EQ(OB_SUCCESS,MviLookupTestAccess::configure(*lookup,index,data,runtime,*scan));
 }
 ~MviLookupFixture() {
  // DAS iterators are arena owned in production. Release while their dynamic
  // type is live, then reclaim their storage with the arena; no base-destructor
  // virtual dispatch is needed.
  if(scan!=nullptr){EXPECT_EQ(OB_SUCCESS,scan->release());}
  if(lookup!=nullptr){EXPECT_EQ(OB_SUCCESS,lookup->release());}
 }
 void put(const std::string &value){doc.locate_expr_datum(*eval).set_string(value.data(),value.size());}
 int save(){return MviLookupTestAccess::save(*lookup);}
 int helper_with_owner(ObIAllocator &owner){
  ObSEArray<ObExpr *,1> exprs;const int ret=exprs.push_back(&doc);
  return ret==OB_SUCCESS?scan->set_scan_datum_rowkey(eval,exprs,&data,&owner,0):ret;
 }
};
}
TEST(MviDatumLookup, actual_mvi_save_publishes_native_keys_and_default_group_only) {
 MviLookupFixture f;std::string key(OB_DOC_ID_COLUMN_BYTE_LENGTH,'\xff');f.put(key);
 ASSERT_EQ(OB_SUCCESS,f.save());ASSERT_NE(nullptr,f.param.datum_lookup_keys_);
 EXPECT_TRUE(f.param.is_get_);EXPECT_TRUE(f.param.key_ranges_.empty());
 ASSERT_EQ(1,f.param.datum_lookup_keys_->count());const auto &published=f.param.datum_lookup_keys_->at(0);
 EXPECT_EQ(1,published.count_);EXPECT_EQ(0,published.group_id_);ASSERT_NE(nullptr,published.datums_);
}
TEST(MviDatumLookup, published_key_descriptors_survive_expression_advance_and_keep_duplicate_ids) {
 MviLookupFixture f;const std::string expected("0123456789abcdef",16);
 {std::string transient=expected;f.put(transient);ASSERT_EQ(OB_SUCCESS,f.save());std::fill(transient.begin(),transient.end(),'x');}
 f.doc.locate_expr_datum(*f.eval).set_null();
 ASSERT_NE(nullptr,f.param.datum_lookup_keys_);const auto *first=f.param.datum_lookup_keys_->at(0).datums_;
 ASSERT_NE(nullptr,first);
 f.put(expected);ASSERT_EQ(OB_SUCCESS,f.save());ASSERT_EQ(2,f.param.datum_lookup_keys_->count());
 EXPECT_EQ(first,f.param.datum_lookup_keys_->at(0).datums_);
 EXPECT_NE(first,f.param.datum_lookup_keys_->at(1).datums_);
 // Physical payload bytes/lifetime are checked through the independent
 // actual StorageRange oracle; this SQL test stays at its public request seam.
}
TEST(MviDatumLookup, actual_scan_close_clears_native_publication_before_next_lookup) {
 MviLookupFixture f;
 for(int round=0;round<32;++round){
  std::string key(16,static_cast<char>(round));f.put(key);ASSERT_EQ(OB_SUCCESS,f.save());
  ASSERT_NE(nullptr,f.param.datum_lookup_keys_);ASSERT_EQ(1,f.param.datum_lookup_keys_->count());
  EXPECT_EQ(1,f.param.datum_lookup_keys_->at(0).count_);
  EXPECT_EQ(0,f.param.datum_lookup_keys_->at(0).group_id_);
  ASSERT_EQ(OB_SUCCESS,f.scan->close_keys());EXPECT_EQ(nullptr,f.param.datum_lookup_keys_);EXPECT_TRUE(f.param.key_ranges_.empty());
 }
}
TEST(MviDatumLookup, null_doc_id_keeps_the_original_error_without_publishing_a_key) {
 MviLookupFixture f;f.doc.locate_expr_datum(*f.eval).set_null();
 EXPECT_EQ(OB_ERR_UNEXPECTED,f.save());EXPECT_EQ(nullptr,f.param.datum_lookup_keys_);
 EXPECT_TRUE(f.param.key_ranges_.empty());EXPECT_FALSE(f.param.is_get_);
}
TEST(MviDatumLookup, malformed_ctdef_invariants_cannot_select_the_old_value_path) {
 // These are explicit producer-invariant checks, not claims that every old
 // malformed-input error/acceptance case remains compatible.
 for(int invalid=0;invalid<4;++invalid){
  MviLookupFixture f;std::string key(16,'k');f.put(key);
  switch(invalid){
   case 0:f.doc.obj_meta_.set_int();break;
   case 1:f.doc.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);break;
   case 2:f.doc.locate_expr_datum(*f.eval).set_string(key.data(),15);break;
   case 3:f.doc.obj_datum_map_=OBJ_DATUM_EXTEND;break;
  }
  EXPECT_EQ(OB_ERR_UNEXPECTED,f.save());EXPECT_EQ(nullptr,f.param.datum_lookup_keys_);EXPECT_TRUE(f.param.key_ranges_.empty());
 }
}
TEST(MviDatumLookup, actual_native_helper_allocation_failure_preserves_request_publication) {
 MviLookupFixture f;const std::string key(16,'k');f.put(key);MviLookupRejectAllocator reject;
 EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,f.helper_with_owner(reject));EXPECT_EQ(1,reject.calls);
 EXPECT_EQ(nullptr,f.param.datum_lookup_keys_);EXPECT_FALSE(f.param.is_get_);EXPECT_TRUE(f.param.key_ranges_.empty());
 ASSERT_EQ(OB_SUCCESS,f.save());auto *keys=f.param.datum_lookup_keys_;const auto *datum=keys->at(0).datums_;
 EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,f.helper_with_owner(reject));EXPECT_EQ(2,reject.calls);
 EXPECT_EQ(keys,f.param.datum_lookup_keys_);EXPECT_EQ(1,keys->count());EXPECT_EQ(datum,keys->at(0).datums_);
 EXPECT_TRUE(f.param.is_get_);EXPECT_TRUE(f.param.key_ranges_.empty());
}
TEST(MviDatumLookup, first_aux_lookup_initialization_preserves_native_point_keys) {
 MviLookupFixture f;
 // Actual auxiliary CtDefs default to false: generate_doc_id_lookup_ctdef
 // supplies the doc-id column but never changes that flag.
 ASSERT_FALSE(f.data.is_get_);
 f.data.schema_version_=1;
 f.runtime.timeout_ts_=INT64_MAX;
 ASSERT_EQ(OB_SUCCESS,f.runtime.init_pd_op(f.exec,f.data));
 transaction::ObTxReadSnapshot snapshot;snapshot.init_none_read();
 f.lookup->set_tablet_id(ObTabletID(1000001));
 // Production binds the child scan to the lookup's own request, rather than
 // the isolated request used by the earlier save-only tests.
 ObTableScanParam &request=f.lookup->get_lookup_param();
 f.scan->set_scan_param(request);f.scan->validate_request=true;
 const std::string key(OB_DOC_ID_COLUMN_BYTE_LENGTH,'k');f.put(key);
 ASSERT_EQ(OB_SUCCESS,f.save());ASSERT_NE(nullptr,request.datum_lookup_keys_);
 ASSERT_TRUE(request.is_get_);ASSERT_TRUE(request.key_ranges_.empty());
 // Real base first-lookup execution calls init_scan_param after the actual
 // MVI save. Before the fix that overwrites true with CtDef's false, so the
 // storage-facing request validator returns OB_INVALID_ARGUMENT.
 ASSERT_EQ(OB_SUCCESS,MviLookupTestAccess::first_lookup(*f.lookup,snapshot));
 EXPECT_EQ(1,f.scan->first_scan_calls);
 EXPECT_TRUE(request.is_get_);EXPECT_TRUE(request.is_valid());
 ASSERT_EQ(1,request.datum_lookup_keys_->count());
 EXPECT_EQ(1,request.datum_lookup_keys_->at(0).count_);
}
TEST(MviDatumLookup, first_legacy_aux_lookup_keeps_ctdef_scan_mode) {
 MviLookupFixture f;
 ASSERT_FALSE(f.data.is_get_);
 f.data.schema_version_=1;f.runtime.timeout_ts_=INT64_MAX;
 ASSERT_EQ(OB_SUCCESS,f.runtime.init_pd_op(f.exec,f.data));
 transaction::ObTxReadSnapshot snapshot;snapshot.init_none_read();
 f.lookup->set_tablet_id(ObTabletID(1000001));
 ObTableScanParam &request=f.lookup->get_lookup_param();
 f.scan->set_scan_param(request);f.scan->validate_request=true;
 ObNewRange range;range.set_whole_range();
 ASSERT_EQ(OB_SUCCESS,request.key_ranges_.push_back(range));
 ASSERT_EQ(OB_SUCCESS,MviLookupTestAccess::first_lookup(*f.lookup,snapshot));
 EXPECT_EQ(1,f.scan->first_scan_calls);
 EXPECT_EQ(nullptr,request.datum_lookup_keys_);
 EXPECT_FALSE(request.is_get_);EXPECT_TRUE(request.is_valid());
 EXPECT_EQ(1,request.key_ranges_.count());
}
int main(int argc,char **argv){::testing::InitGoogleTest(&argc,argv);if(ObCharset::init_charset()!=OB_SUCCESS)return 1;return RUN_ALL_TESTS();}
