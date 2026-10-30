/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include "sql/executor/ob_execute_result.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "sql/session/ob_sql_session_info.h"
#include "query/engine/expr/ob_expr_operator.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
std::vector<std::string> split(const std::string &line) {
  std::vector<std::string> columns; size_t start=0;
  while (true) {size_t end=line.find('\t',start); if(end==std::string::npos){columns.push_back(line.substr(start));break;}columns.push_back(line.substr(start,end-start));start=end+1;}
  return columns;
}
std::string unhex(const std::string &hex) {
  if(hex=="-"){return {};}
  const char *digits="0123456789abcdef";std::string out;
  if(hex.size()%2){ADD_FAILURE()<<"odd hex";return {};}
  for(size_t i=0;i<hex.size();i+=2){const char*h=std::strchr(digits,hex[i]),*l=std::strchr(digits,hex[i+1]);if(h==nullptr||l==nullptr){ADD_FAILURE()<<"invalid hex";return {};}out.push_back((h-digits)*16+l-digits);}
  return out;
}
std::string wire(const DatumValue &value) {
  int64_t size=0,pos=0;EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::value_size(value.datum_,value.meta_,size));
  std::string bytes(size,'\0');EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_value(value.datum_,value.meta_,bytes.data(),bytes.size(),pos));EXPECT_EQ(size,pos);return bytes;
}
struct OldRowTag {using type=ObNewRow ObExecuteResult::*;friend type result_member(OldRowTag);};
template<typename Tag,typename Tag::type Member>struct ResultPrivateAccess {friend typename Tag::type result_member(Tag){return Member;}};
template struct ResultPrivateAccess<OldRowTag,&ObExecuteResult::row_>;
class ResultOwner final : public ObIAllocator {
public:
  bool fail=false;ObArenaAllocator arena;
  void *alloc(int64_t bytes) override{return fail?nullptr:arena.alloc(bytes);}
  void *alloc(int64_t bytes,const ObMemAttr&) override{return alloc(bytes);}
  void free(void *p) override{arena.free(p);}
};
class ScalarResultProducer final : public ObOperator {
public:
  int calls=0;bool null_second=false;
  ScalarResultProducer(ObExecContext &ctx,const ObOpSpec &spec):ObOperator(ctx,spec,nullptr){}
  int get_next_row() override {
    if(calls++>=2){return OB_ITER_END;}
    ObExpr &expr=*get_spec().output_.at(0);ObEvalCtx &ctx=get_eval_ctx();
    ObDatum &value=expr.locate_expr_datum(ctx);value.ptr_=expr.get_rev_buf(ctx);
    if(null_second&&calls==2){value.set_null();}else{value.set_int(42+calls);}
    expr.get_eval_info(ctx).evaluated_=true;return OB_SUCCESS;
  }
  int inner_get_next_row() override {return get_next_row();}
  void destroy() override {ObOperator::destroy();}
};
struct ResultFrame {
  ResultOwner owner;ObExecContext exec;ObExpr expr;char *frames[1];ObOpSpec spec;ScalarResultProducer producer;ObExecuteResult result;
  ResultFrame():exec(owner),spec(owner,PHY_EXPR_VALUES),producer(exec,spec) {
    // ObEvalCtx captures frames in the operator constructor, so populate both
    // contexts explicitly before invoking the actual execution result method.
    frames[0]=static_cast<char*>(owner.alloc(2048));EXPECT_NE(nullptr,frames[0]);MEMSET(frames[0],0,2048);exec.set_frames(frames);producer.get_eval_ctx().frames_=frames;
    expr.frame_idx_=0;expr.datum_off_=64;expr.eval_info_off_=128;expr.res_buf_off_=256;expr.res_buf_len_=512;expr.type_=T_COLUMN_REF;
    expr.obj_meta_.set_int();expr.obj_meta_.set_scale(7);expr.obj_datum_map_=OBJ_DATUM_8BYTE_DATA;expr.datum_meta_.type_=ObIntType;
    new(frames[0]+64)ObDatum();EXPECT_EQ(OB_SUCCESS,spec.output_.init(1));EXPECT_EQ(OB_SUCCESS,spec.output_.push_back(&expr));result.set_static_engine_root(&producer);
  }
};
}
TEST(SessionNativeResult,matches_actual_immutable_old_adapter_wire_and_errors) {
  std::ifstream input("unittest/sql/data/session_result_values.golden");ASSERT_TRUE(input.good());std::string line;int checked=0;
  while(std::getline(input,line)) {
    if(line.empty()||line[0]=='#'){continue;}
    auto cols=split(line);ASSERT_EQ(9,cols.size());SCOPED_TRACE(cols[0]);
    std::string source=unhex(cols[6]),meta_bytes=unhex(cols[3]),previous_bytes=unhex(cols[4]);ASSERT_EQ(sizeof(SqlTypeMeta),meta_bytes.size());ASSERT_EQ(sizeof(SqlTypeMeta),previous_bytes.size());
    SqlTypeMeta meta,previous;MEMCPY(&meta,meta_bytes.data(),sizeof(meta));MEMCPY(&previous,previous_bytes.data(),sizeof(previous));
    ObDatum datum;datum.ptr_=source.data();datum.pack_=std::strtoull(cols[5].c_str(),nullptr,10);
    DatumValue output;int ret=ResultRowView::project_value(datum,meta,static_cast<SqlValueDatumMapType>(std::atoi(cols[2].c_str())),previous,output,std::atoi(cols[1].c_str())!=0);
    EXPECT_EQ(std::atoi(cols[7].c_str()),ret);if(ret==OB_SUCCESS){EXPECT_EQ(unhex(cols[8]),wire(output));}++checked;
  }
  EXPECT_EQ(34,checked);
}
TEST(SessionNativeResult,static_native_advance_never_materializes_legacy_cells_and_preserves_null_scale) {
  ResultFrame frame;frame.producer.null_second=true;ResultRowView view;DatumValue value;
  ASSERT_EQ(OB_SUCCESS,frame.result.get_next_native_result_row(frame.exec,view));ASSERT_TRUE(view.is_datum_row());ASSERT_EQ(OB_SUCCESS,view.get_value(0,value));EXPECT_EQ(43,value.get_int());EXPECT_EQ(7,value.get_scale());EXPECT_EQ(nullptr,(frame.result.*result_member(OldRowTag{})).cells_);
  ASSERT_EQ(OB_SUCCESS,frame.result.get_next_native_result_row(frame.exec,view));ASSERT_EQ(OB_SUCCESS,view.get_value(0,value));EXPECT_TRUE(value.is_null());EXPECT_EQ(7,value.get_scale());EXPECT_EQ(CS_TYPE_BINARY,value.get_collation_type());
  EXPECT_EQ(OB_ITER_END,frame.result.get_next_native_result_row(frame.exec,view));EXPECT_FALSE(view.is_datum_row());EXPECT_EQ(nullptr,(frame.result.*result_member(OldRowTag{})).cells_);
}
TEST(SessionNativeResult,metadata_oom_does_not_advance_or_publish_a_row) {
  ResultFrame frame;frame.owner.fail=true;ResultRowView view;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,frame.result.get_next_native_result_row(frame.exec,view));EXPECT_EQ(0,frame.producer.calls);EXPECT_FALSE(view.is_datum_row());
  frame.owner.fail=false;EXPECT_EQ(OB_SUCCESS,frame.result.get_next_native_result_row(frame.exec,view));EXPECT_EQ(1,frame.producer.calls);
}
TEST(SessionNativeResult,invalid_decimal_scale_is_reported_before_publishing_native_row) {
  ResultFrame frame;frame.expr.obj_meta_.set_decimal_int();frame.expr.obj_meta_.set_scale(-1);frame.expr.obj_datum_map_=OBJ_DATUM_DECIMALINT;ResultRowView view;
  EXPECT_EQ(OB_ERR_UNEXPECTED,frame.result.get_next_native_result_row(frame.exec,view));EXPECT_FALSE(view.is_datum_row());EXPECT_EQ(nullptr,(frame.result.*result_member(OldRowTag{})).cells_);
}
TEST(SessionNativeResult,explicit_owner_snapshot_survives_producer_reuse_and_oom_preserves_output) {
  ObArenaAllocator owner;ResultOwner rejecting;rejecting.fail=true;std::string source="customer_name";ObDatum input;input.set_string(source.data(),source.size());SqlTypeMeta meta;meta.set_varchar();meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);meta.set_collation_level(CS_LEVEL_IMPLICIT);DatumValue borrowed,owned;
  ASSERT_EQ(OB_SUCCESS,ResultRowView::project_value(input,meta,OBJ_DATUM_STRING,SqlTypeMeta(),borrowed));ASSERT_EQ(OB_SUCCESS,clone_typed_value(borrowed,owned,owner));source.assign(source.size(),'x');EXPECT_EQ(std::string("customer_name"),std::string(owned.get_string().ptr(),owned.get_string().length()));
  const std::string before=wire(owned);EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,clone_typed_value(borrowed,owned,rejecting));EXPECT_EQ(before,wire(owned));
}
TEST(SessionNativeResult,null_and_error_projection_do_not_modify_input_or_failed_output) {
  int64_t n=42;ObDatum input;input.ptr_=reinterpret_cast<char*>(&n);input.pack_=sizeof(n);SqlTypeMeta meta;meta.set_decimal_int();meta.set_scale(-1);ObArenaAllocator owner;DatumValue output;ASSERT_EQ(OB_SUCCESS,output.set_int(77,owner));const std::string before=wire(output);
  EXPECT_EQ(OB_ERR_UNEXPECTED,ResultRowView::project_value(input,meta,OBJ_DATUM_DECIMALINT,SqlTypeMeta(),output));EXPECT_EQ(before,wire(output));EXPECT_EQ(42,n);
}

TEST(SessionNativeResult,switch_matches_actual_old_temporal_null_and_raw_metadata) {
  ObArenaAllocator owner;
  ObSQLSessionInfo session;ASSERT_EQ(OB_SUCCESS,session.test_init(1,1,nullptr));ASSERT_EQ(OB_SUCCESS,session.load_default_sys_variable(false,false));
  ObExecContext exec(owner);exec.set_my_session(&session);ObSQLSessionInfo::ExecCtxSessionRegister registration(session,&exec);
  ObExprCtx context;context.calc_buf_=&owner;context.my_session_=&session;context.exec_ctx_=&exec;
  std::ifstream input("unittest/sql/data/session_switch_values.golden");ASSERT_TRUE(input.good());std::string line;int checked=0;
  while(std::getline(input,line)) {
    auto cols=split(line);ASSERT_GE(cols.size(),3);SCOPED_TRACE(cols[0]);DatumValue source;
    if(cols[0]=="int_raw_scale"||cols[0]=="int_no_ctx") {ASSERT_EQ(OB_SUCCESS,source.set_int(918,owner));source.set_scale(7);}
    else if(cols[0]=="null_discards_source_scale"||cols[0]=="null_no_ctx") {source.set_null();source.set_scale(7);}
    else if(cols[0]=="datetime"||cols[0]=="datetime_no_ctx") {ASSERT_EQ(OB_SUCCESS,source.set_datetime(1792996496123456,owner));source.set_scale(6);}
    else if(cols[0]=="date") {ASSERT_EQ(OB_SUCCESS,source.set_date(20752,owner));source.set_scale(6);}
    else if(cols[0]=="time") {ASSERT_EQ(OB_SUCCESS,source.set_time(-1234567,owner));source.set_scale(6);}
    else if(cols[0]=="year") {ASSERT_EQ(OB_SUCCESS,source.set_year(126,owner));source.set_scale(6);}
    else if(cols[0]=="varchar_raw_meta") {ASSERT_EQ(OB_SUCCESS,source.set_varchar(ObString::make_string("customer"),owner));source.set_collation_type(CS_TYPE_UTF8MB4_BIN);source.set_scale(3);}
    else {FAIL()<<"unexpected fixture";}
    ObSessionVariable output;int ret=std::atoi(cols[1].c_str())?ObVariableSetExecutor::switch_to_session_variable(context,source,output):ObVariableSetExecutor::switch_to_session_variable(source,output);
    EXPECT_EQ(std::atoi(cols[2].c_str()),ret);
    if(ret==OB_SUCCESS) {
      ASSERT_EQ(5,cols.size());ObSessionValMap map;ASSERT_EQ(OB_SUCCESS,map.init(4096,64,nullptr));ASSERT_EQ(OB_SUCCESS,map.set_refactored(ObString::make_string("variable"),output));
      std::string bytes(map.get_serialize_size(),'\0');int64_t pos=0;ASSERT_EQ(OB_SUCCESS,map.serialize(bytes.data(),bytes.size(),pos));EXPECT_EQ(unhex(cols[4]),bytes);
    }
    ++checked;
  }
  EXPECT_EQ(10,checked);
}
TEST(SessionNativeResult,public_setter_owns_payload_before_source_arena_is_destroyed) {
  ObSQLSessionInfo session;ASSERT_EQ(OB_SUCCESS,session.get_user_var_val_map().init(4096,16,nullptr));
  {
    ObArenaAllocator source_owner;DatumValue source;ASSERT_EQ(OB_SUCCESS,source.set_varchar(ObString::make_string("saved_after_source_destroy"),source_owner));source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS,ObVariableSetExecutor::set_user_variable(source,ObString::make_string("saved"),&session));
  }
  DatumValue stored;ASSERT_EQ(OB_SUCCESS,session.get_user_variable_value(ObString::make_string("saved"),stored));EXPECT_EQ(std::string("saved_after_source_destroy"),std::string(stored.get_string().ptr(),stored.get_string().length()));
}

TEST(SessionNativeResult,native_row_adapter_reserves_scalar_slots_and_keeps_values_until_next_advance) {
  ResultFrame frame;
  const ObNewRow *row=nullptr;
  ASSERT_EQ(OB_SUCCESS,frame.result.get_next_row(frame.exec,row));
  ASSERT_NE(nullptr,row);ASSERT_EQ(1,row->count_);EXPECT_EQ(43,row->cells_[0].get_int());
  EXPECT_EQ(7,row->cells_[0].get_scale());
  DatumValue *cells=row->cells_;
  frame.expr.locate_expr_datum(frame.producer.get_eval_ctx()).set_int(999);
  EXPECT_EQ(43,row->cells_[0].get_int());
  ASSERT_EQ(OB_SUCCESS,frame.result.get_next_row(frame.exec,row));
  EXPECT_EQ(cells,row->cells_);EXPECT_EQ(44,row->cells_[0].get_int());
  EXPECT_EQ(OB_ITER_END,frame.result.get_next_row(frame.exec,row));EXPECT_EQ(nullptr,row);
}

TEST(SessionNativeResult,native_row_adapter_oom_and_uninitialized_root_do_not_advance_or_publish) {
  ResultFrame frame;frame.owner.fail=true;
  const ObNewRow *row=nullptr;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,frame.result.get_next_row(frame.exec,row));
  EXPECT_EQ(0,frame.producer.calls);EXPECT_EQ(nullptr,row);
  frame.owner.fail=false;
  EXPECT_EQ(OB_SUCCESS,frame.result.get_next_row(frame.exec,row));EXPECT_EQ(1,frame.producer.calls);
  ObExecuteResult uninitialized;
  EXPECT_EQ(OB_NOT_INIT,uninitialized.get_next_row(frame.exec,row));EXPECT_EQ(nullptr,row);
}

TEST(SessionNativeResult,native_row_view_applies_projector_and_rejects_invalid_indices_without_output_changes) {
  ObArenaAllocator owner;DatumValueBuffer values(owner);
  ASSERT_EQ(OB_SUCCESS,values.reserve(2));
  ASSERT_EQ(OB_SUCCESS,values.set_int(0,61));ASSERT_EQ(OB_SUCCESS,values.set_int(1,67));
  int32_t projection[]={1,0,1};ObNewRow row(values.data(),2);
  row.projector_=projection;row.projector_size_=3;
  ResultRowView view;view.set_legacy(&row);DatumValue output;
  ASSERT_EQ(OB_SUCCESS,view.get_value(0,output));EXPECT_EQ(67,output.get_int());
  ASSERT_EQ(OB_SUCCESS,view.get_value(1,output));EXPECT_EQ(61,output.get_int());
  ASSERT_EQ(OB_SUCCESS,view.get_value(2,output));EXPECT_EQ(67,output.get_int());
  EXPECT_EQ(OB_SIZE_OVERFLOW,view.get_value(-1,output));EXPECT_EQ(67,output.get_int());
  EXPECT_EQ(OB_SIZE_OVERFLOW,view.get_value(3,output));EXPECT_EQ(67,output.get_int());
  projection[0]=2;EXPECT_EQ(OB_ERR_UNEXPECTED,view.get_value(0,output));EXPECT_EQ(67,output.get_int());
  ResultRowView unopened;EXPECT_EQ(OB_NOT_INIT,unopened.get_value(0,output));EXPECT_EQ(67,output.get_int());
}
