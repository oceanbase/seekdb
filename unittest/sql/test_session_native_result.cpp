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
#include "sql/engine/expr/ob_expr_output_pack.h"
#include "sql/engine/cmd/ob_variable_set_executor.h"
#include "sql/session/ob_sql_session_info.h"
#include "query/engine/expr/ob_expr_operator.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "share/datum/number_cast_constants.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "common/udt/ob_array_fixed_size.h"
#include "common/lob/lob_types.h"
#include "share/ob_i_lob_read_service.h"
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
  int64_t size=0,pos=0;EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::native_value_size(value.datum_,value.meta_,size));
  std::string bytes(size,'\0');EXPECT_EQ(OB_SUCCESS,DatumParameterCodec::encode_native_value(value.datum_,value.meta_,bytes.data(),bytes.size(),pos));EXPECT_EQ(size,pos);return bytes;
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
  int calls=0;bool null_first=false;bool null_second=false;
  ScalarResultProducer(ObExecContext &ctx,const ObOpSpec &spec):ObOperator(ctx,spec,nullptr){}
  int get_next_row() override {
    if(calls++>=2){return OB_ITER_END;}
    ObEvalCtx &ctx=get_eval_ctx();
    for(int64_t i=0;i<get_spec().output_.count();++i) {
      ObExpr &expr=*get_spec().output_.at(i);Datum &value=expr.locate_expr_datum(ctx);
      if((null_first&&calls==1)||(null_second&&calls==2)){value.set_null();}
      else{value.set_int(42+calls+10*i);}
      expr.get_eval_info(ctx).evaluated_=true;
    }
    return OB_SUCCESS;
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
    EXPECT_EQ(OB_SUCCESS,expr.prepare_scalar_type(expr.obj_meta_,ObAccuracy(20,7),owner));new(frames[0]+64)Datum();EXPECT_EQ(OB_SUCCESS,spec.output_.init(1));EXPECT_EQ(OB_SUCCESS,spec.output_.push_back(&expr));result.set_static_engine_root(&producer);
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
TEST(SessionNativeResult,first_null_uses_initial_column_metadata_and_rebinding_resets_history) {
  ResultFrame frame;
  frame.producer.null_first = true;
  ASSERT_EQ(7, frame.expr.get_sql_type().legacy_meta().get_scale());
  ASSERT_EQ(7, frame.expr.get_sql_type().scale());
  ResultRowView view;
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(99, frame.owner));
  output.set_scale(11);
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_native_result_row(frame.exec, view));
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(-1, output.get_scale());
  EXPECT_EQ(CS_TYPE_BINARY, output.get_collation_type());
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_native_result_row(frame.exec, view));
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, output));
  EXPECT_EQ(44, output.get_int());
  EXPECT_EQ(7, output.get_scale());
  frame.producer.calls = 0;
  frame.result.set_static_engine_root(&frame.producer);
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_native_result_row(frame.exec, view));
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(-1, output.get_scale());
  EXPECT_EQ(nullptr, (frame.result.*result_member(OldRowTag{})).cells_);
}
TEST(SessionNativeResult,unconsumed_rows_update_each_column_without_reusing_the_callers_metadata) {
  ObExpr other;
  ResultFrame frame;
  other.frame_idx_ = 0;
  other.datum_off_ = 80;
  other.eval_info_off_ = 136;
  other.res_buf_off_ = 768;
  other.res_buf_len_ = 512;
  other.type_ = T_COLUMN_REF;
  other.obj_meta_.set_int();
  other.obj_meta_.set_scale(3);
  other.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
  other.datum_meta_.type_ = ObIntType;
  ASSERT_EQ(OB_SUCCESS, other.prepare_scalar_type(other.obj_meta_, ObAccuracy(20,3), frame.owner));
  new (frame.frames[0] + other.datum_off_) Datum();
  frame.spec.output_.reset();
  ASSERT_EQ(OB_SUCCESS, frame.spec.output_.init(2));
  ASSERT_EQ(OB_SUCCESS, frame.spec.output_.push_back(&frame.expr));
  ASSERT_EQ(OB_SUCCESS, frame.spec.output_.push_back(&other));
  frame.producer.null_second = true;
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_native_result_row(frame.exec, view));
  // Deliberately do not read either value from the first row.
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_native_result_row(frame.exec, view));
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, output.set_int(99, frame.owner));
  output.set_scale(11);
  ASSERT_EQ(OB_SUCCESS, view.get_value(1, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(3, output.get_scale());
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(7, output.get_scale());
  ASSERT_EQ(OB_SUCCESS, view.get_value(1, output));
  EXPECT_TRUE(output.is_null());
  EXPECT_EQ(3, output.get_scale());
  EXPECT_EQ(CS_TYPE_BINARY, output.get_collation_type());
  EXPECT_EQ(nullptr, (frame.result.*result_member(OldRowTag{})).cells_);
}
TEST(SessionNativeResult,inline_view_borrows_the_authoritative_native_frame_until_reset) {
  ResultFrame frame;
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_result_row(frame.exec, view));
  Datum &native = frame.expr.locate_expr_datum(frame.producer.get_eval_ctx());
  native.set_int(INT64_MIN);
  ASSERT_TRUE(view.is_datum_row());
  ASSERT_TRUE(view.is_inline_datum_row());
  Datum value;
  const SqlType *value_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, view.get_inline_value(0, value, value_type));
  EXPECT_EQ(native.get_int(), value.get_int());
  EXPECT_EQ(&frame.expr.get_sql_type(), value_type);
  EXPECT_EQ(INT64_MIN, value.get_int());
  DatumValue old_view;
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, old_view));
  EXPECT_EQ(INT64_MIN, old_view.get_int());
  EXPECT_EQ(OB_SIZE_OVERFLOW, view.get_inline_value(1, value, value_type));
  EXPECT_EQ(INT64_MIN, value.get_int());
  EXPECT_EQ(&frame.expr.get_sql_type(), value_type);
  // The row remains borrowed, while each getter copies into caller storage.
  // A fresh read observes row reuse; the preceding caller capture stays intact.
  native.set_int(99);
  EXPECT_TRUE(view.inline_row_valid());
  EXPECT_EQ(INT64_MIN, value.get_int());
  ASSERT_EQ(OB_SUCCESS, view.get_inline_value(0, value, value_type));
  EXPECT_EQ(99, value.get_int());
  view.set_legacy(nullptr);
  EXPECT_EQ(OB_STATE_NOT_MATCH, view.get_inline_value(0, value, value_type));
  EXPECT_EQ(OB_NOT_INIT, view.get_value(0, old_view));
  EXPECT_EQ(INT64_MIN, old_view.get_int());
}
TEST(SessionNativeResult,inline_typed_null_and_fallback_setters_do_not_retain_old_slots) {
  ResultFrame frame;
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_result_row(frame.exec, view));
  frame.expr.locate_expr_datum(frame.producer.get_eval_ctx()).set_null();
  Datum value;
  const SqlType *value_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, view.get_inline_value(0, value, value_type));
  EXPECT_TRUE(value.is_null());
  EXPECT_EQ(ObIntType, value_type->kind());
  alignas(8) char scratch[64];
  view.set_datums(frame.spec.output_, frame.producer.get_eval_ctx(), 0, false, scratch);
  EXPECT_TRUE(view.is_inline_datum_row());
  ASSERT_EQ(OB_SUCCESS, view.get_inline_value(0, value, value_type));
  EXPECT_TRUE(value.is_null());
  view.set_legacy(nullptr);
  EXPECT_FALSE(view.is_datum_row());
  EXPECT_EQ(0, view.count());
  EXPECT_EQ(OB_STATE_NOT_MATCH, view.get_inline_value(0, value, value_type));
}
TEST(SessionNativeResult,native_get_value_borrows_stable_compatibility_scratch_and_preserves_metadata) {
  ResultFrame frame;
  ResultRowView view;
  ASSERT_EQ(OB_SUCCESS, frame.result.get_next_result_row(frame.exec, view));
  Datum &native = frame.expr.locate_expr_datum(frame.producer.get_eval_ctx());
  native.set_int(INT64_MAX);
  DatumValue borrowed;
  frame.owner.fail = true;
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, borrowed));
  EXPECT_EQ(INT64_MAX, borrowed.get_int());
  EXPECT_EQ(7, borrowed.get_scale());
  EXPECT_EQ(0, borrowed.datum_.length());
  native.set_int(61);
  // The physical wrapper owns its scalar bits inline, independently of the frame.
  EXPECT_EQ(INT64_MAX, borrowed.get_int());
  ASSERT_EQ(OB_SUCCESS, view.get_value(0, borrowed));
  EXPECT_EQ(0, borrowed.datum_.length());
  EXPECT_EQ(61, borrowed.get_int());
  view.set_legacy(nullptr);
  EXPECT_EQ(OB_NOT_INIT, view.get_value(0, borrowed));
  EXPECT_EQ(61, borrowed.get_int());
}
TEST(SessionNativeResult,unknown_decimal_scale_is_reported_before_publishing_native_row) {
  ResultFrame frame;frame.expr.obj_meta_.set_decimal_int();frame.expr.obj_meta_.set_scale(-1);frame.expr.obj_datum_map_=OBJ_DATUM_DECIMALINT;ASSERT_EQ(OB_SUCCESS,frame.expr.prepare_scalar_type(frame.expr.obj_meta_,ObAccuracy(20,-1),frame.owner));ASSERT_FALSE(frame.expr.get_sql_type().has_scale());ResultRowView view;
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


namespace {
struct OutputPackConvertTag {
  using type = int (*)(DatumValue &, ObIAllocator &, const ObSQLSessionInfo &);
  friend type result_member(OutputPackConvertTag);
};
struct OutputPackRowTag {
  using type = int (*)(const ObExpr &, ObEvalCtx &, ObSQLSessionInfo *,
      ObIAllocator &, ObOutputPackInfo *,
      oceanbase::share::schema::ObSchemaGetterGuard *,
      oceanbase::obmysql::MYSQL_PROTOCOL_TYPE,
      ObIArray<oceanbase::obmysql::ObMySQLCellValue> &);
  friend type result_member(OutputPackRowTag);
};
template struct ResultPrivateAccess<OutputPackConvertTag,
    &ObExprOutputPack::convert_string_value_charset>;
template struct ResultPrivateAccess<OutputPackRowTag,
    &ObExprOutputPack::build_row_values>;
}

TEST(SessionNativeResult, output_pack_charset_conversion_preserves_typed_null_and_empty_values)
{
  using namespace oceanbase::obmysql;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObArenaAllocator owner;
  ResultOwner reject;
  reject.fail = true;
  const ObDataTypeCastParams cast_params = ObBasicSessionInfo::create_dtc_params(&session);
  const std::string utf8("\xC3\xA9\0Z", 4);
  for (const ObCharsetType charset : {CHARSET_UTF8MB4, CHARSET_BINARY}) {
    SCOPED_TRACE(static_cast<int>(charset));
    ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(
        oceanbase::share::SYS_VAR_CHARACTER_SET_RESULTS,
        static_cast<int64_t>(ObCharset::get_default_collation(charset))));
    ObCharsetType actual_charset = CHARSET_INVALID;
    ASSERT_EQ(OB_SUCCESS, session.get_character_set_results(actual_charset));
    ASSERT_EQ(charset, actual_charset);
    for (const SqlValueType type : {ObVarcharType, ObCharType}) {
      SCOPED_TRACE(static_cast<int>(type));
      for (const ObCollationType source_collation : {CS_TYPE_UTF8MB4_BIN, CS_TYPE_BINARY}) {
        SCOPED_TRACE(static_cast<int>(source_collation));
        DatumValue null_value;
        null_value.meta_.set_type(type);
        null_value.meta_.set_collation_type(source_collation);
        null_value.meta_.set_collation_level(CS_LEVEL_COERCIBLE);
        null_value.meta_.set_scale(0);
        const SqlTypeMeta original_meta = null_value.meta_;
        ASSERT_EQ(OB_SUCCESS, result_member(OutputPackConvertTag())(
            null_value, reject, session));
        EXPECT_TRUE(null_value.is_null());
        EXPECT_EQ(original_meta, null_value.meta_);
        EXPECT_EQ(original_meta.get_scale(), null_value.meta_.get_scale());
        for (const auto protocol : {TEXT, BINARY}) {
          ObMySQLCellValue cell;
          ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_cell_value(
              null_value, protocol, reject, cell, cast_params, nullptr, session));
          EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, cell.kind_);
        }
        DatumValue empty;
        empty.meta_ = original_meta;
        ASSERT_EQ(OB_SUCCESS, empty.datum_.set_string(nullptr, 0));
        ASSERT_EQ(OB_SUCCESS, result_member(OutputPackConvertTag())(empty, reject, session));
        EXPECT_FALSE(empty.is_null());
        EXPECT_EQ(0, empty.get_string().length());
        for (const auto protocol : {TEXT, BINARY}) {
          ObMySQLCellValue cell;
          ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_cell_value(
              empty, protocol, reject, cell, cast_params, nullptr, session));
          EXPECT_EQ(ObMySQLCellValueKind::LENENC_BYTES, cell.kind_);
          EXPECT_EQ(0, cell.get_bytes_len());
        }
        DatumValue text;
        text.meta_ = original_meta;
        ASSERT_EQ(OB_SUCCESS, text.datum_.set_string(utf8.data(), utf8.size()));
        ASSERT_EQ(OB_SUCCESS, result_member(OutputPackConvertTag())(text, owner, session));
        EXPECT_FALSE(text.is_null());
        EXPECT_EQ(utf8, std::string(text.get_string_ptr(), text.get_string_len()));
        EXPECT_EQ(original_meta.get_collation_type(), text.get_collation_type());
      }
    }
  }
}

TEST(SessionNativeResult, output_pack_row_builder_keeps_outer_join_null_distinct_from_empty)
{
  using namespace oceanbase::obmysql;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObArenaAllocator owner;
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  alignas(8) char frame[512]{};
  char *frames[] = {frame};
  exec.set_frames(frames);
  ObEvalCtx eval(exec);
  ObExpr pack, arguments[5];
  ObExpr *argument_ptrs[5];
  for (int i = 0; i < 5; ++i) { argument_ptrs[i] = &arguments[i]; }
  pack.args_ = argument_ptrs;
  pack.arg_cnt_ = 5;
  ObOutputPackInfo info(owner, T_OP_OUTPUT_PACK);
  ASSERT_EQ(OB_SUCCESS, info.param_fields_.reserve(4));
  ObAccuracy string_accuracy;
  string_accuracy.set_full_length(64, LS_BYTE);
  string_accuracy.set_length_semantics(LS_BYTE);
  string_accuracy.set_scale(0);
  for (int i = 1; i < 5; ++i) {
    ObExpr &arg = arguments[i];
    arg.frame_idx_ = 0;
    arg.datum_off_ = i * 64;
    if (i == 4) {
      arg.obj_meta_.set_int();
      arg.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      ASSERT_EQ(OB_SUCCESS, arg.prepare_scalar_type(arg.obj_meta_, ObAccuracy(20, 0), owner));
    } else {
      arg.obj_meta_.set_varchar();
      arg.obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      arg.obj_meta_.set_collation_level(CS_LEVEL_IMPLICIT);
      arg.obj_meta_.set_scale(0);
      arg.obj_datum_map_ = OBJ_DATUM_STRING;
      ASSERT_EQ(OB_SUCCESS, arg.prepare_scalar_type(arg.obj_meta_, string_accuracy, owner));
    }
    new (frame + arg.datum_off_) Datum();
    ObField field;
    field.type_meta_ = arg.obj_meta_;
    field.accuracy_ = i == 4 ? ObAccuracy(20, 0) : string_accuracy;
    ASSERT_EQ(OB_SUCCESS, info.param_fields_.push_back(field));
  }
  ASSERT_EQ(OB_SUCCESS, arguments[2].locate_expr_datum(eval).set_string(nullptr, 0));
  const std::string utf8("\xC3\xA9\0Z", 4);
  ASSERT_EQ(OB_SUCCESS, arguments[3].locate_expr_datum(eval).set_string(utf8.data(), utf8.size()));
  for (const ObCharsetType charset : {CHARSET_UTF8MB4, CHARSET_BINARY}) {
    SCOPED_TRACE(static_cast<int>(charset));
    ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(
        oceanbase::share::SYS_VAR_CHARACTER_SET_RESULTS,
        static_cast<int64_t>(ObCharset::get_default_collation(charset))));
    for (const auto protocol : {TEXT, BINARY}) {
      SCOPED_TRACE(static_cast<int>(protocol));
      ObSEArray<ObMySQLCellValue, 4> cells;
      ASSERT_EQ(OB_SUCCESS, result_member(OutputPackRowTag())(
          pack, eval, &session, owner, &info, nullptr, protocol, cells));
      ASSERT_EQ(4, cells.count());
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, cells.at(0).kind_);
      EXPECT_EQ(ObMySQLCellValueKind::LENENC_BYTES, cells.at(1).kind_);
      EXPECT_EQ(0, cells.at(1).get_bytes_len());
      EXPECT_EQ(ObMySQLCellValueKind::LENENC_BYTES, cells.at(2).kind_);
      EXPECT_EQ(utf8, std::string(cells.at(2).get_bytes(), cells.at(2).get_bytes_len()));
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, cells.at(3).kind_);
      EXPECT_TRUE(arguments[1].locate_expr_datum(eval).is_null());
      EXPECT_TRUE(arguments[4].locate_expr_datum(eval).is_null());
      EXPECT_EQ(utf8, std::string(arguments[3].locate_expr_datum(eval).get_string().ptr(),
                                 arguments[3].locate_expr_datum(eval).get_string().length()));
    }
  }
}


namespace {
// The input is a real inline LOB. No external LOB read is allowed by this test;
// these transport methods satisfy the actual execution-context service port.
class OutputPackNoIoLobService final : public ObILobReadService {
public:
  int calls_ = 0;
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, SqlValueType, ObCollationType,
      ObLobLocatorV2 &, ObIAllocator *, ObString &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *, uint32_t) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
      ObString &, ObTextStringIterState &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObString &, ObTextStringIterState &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  void free_lob_query_iter(ObLobTextIterCtx &) override { ++calls_; }
};
class OutputPackInlineLobContext final : public ObILobAccessContext { public: ~OutputPackInlineLobContext() = default; };
struct OutputPackLobContextTag {
  using type = ObILobAccessContext *ObExecContext::*;
  friend type access(OutputPackLobContextTag);
};
template<class Tag, typename Tag::type Member> struct OutputPackTestAccess {
  friend typename Tag::type access(Tag) { return Member; }
};
template struct OutputPackTestAccess<OutputPackLobContextTag, &ObExecContext::lob_access_ctx_>;
class OutputPackInlineContextGuard {
public:
  OutputPackInlineContextGuard(ObExecContext &exec, ObILobAccessContext &context) : exec_(exec) {
    exec_.*access(OutputPackLobContextTag{}) = &context;
  }
  ~OutputPackInlineContextGuard() { exec_.*access(OutputPackLobContextTag{}) = nullptr; }
private: ObExecContext &exec_;
};
}

TEST(SessionNativeResult, output_pack_collection_null_and_present_array_use_distinct_paths)
{
  using namespace oceanbase::obmysql;
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  OutputPackNoIoLobService service;
  OutputPackInlineLobContext context;
  exec.set_lob_read_service(&service);
  OutputPackInlineContextGuard context_guard(exec, context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
  uint16_t id = 0;
  ASSERT_EQ(OB_SUCCESS, exec.get_subschema_id_by_type_string(
      ObString::make_string("ARRAY(INT)"), id));
  ASSERT_FALSE(ob_is_reserved_subschema_id(id));
  ObSubSchemaValue definition;
  ASSERT_EQ(OB_SUCCESS, exec.get_sqludt_meta_by_subschema_id(id, definition));
  ASSERT_EQ(OB_SUBSCHEMA_COLLECTION_TYPE, definition.type_);
  const auto *collection = static_cast<const ObSqlCollectionInfo *>(definition.value_);
  ASSERT_NE(nullptr, collection);
  ASSERT_NE(nullptr, collection->collection_meta_);
  auto *array_type = static_cast<ObCollectionArrayType *>(collection->collection_meta_);
  ObIArrayType *array = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(owner, *array_type, array, false));
  ASSERT_EQ(OB_SUCCESS, array->init());
  // The real ARRAY(INT) parser declares 32-bit elements.
  auto *numbers = dynamic_cast<ObArrayFixedSize<int32_t> *>(array);
  ASSERT_NE(nullptr, numbers);
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(7));
  ASSERT_EQ(OB_SUCCESS, numbers->push_back(-9));
  ASSERT_EQ(OB_SUCCESS, numbers->push_null());
  ASSERT_EQ(OB_SUCCESS, array->init());
  std::string raw(array->get_raw_binary_len(), '\0');
  ASSERT_FALSE(raw.empty());
  ASSERT_EQ(OB_SUCCESS, array->get_raw_binary(&raw[0], raw.size()));
  ObLobCommon header;
  std::string lob(sizeof(header) + raw.size(), '\0');
  MEMCPY(&lob[0], &header, sizeof(header));
  MEMCPY(&lob[sizeof(header)], raw.data(), raw.size());

  alignas(8) char frame[256]{};
  char *frames[] = {frame};
  exec.set_frames(frames);
  ObEvalCtx eval(exec);
  ObExpr pack, arguments[3];
  ObExpr *argument_ptrs[] = {&arguments[0], &arguments[1], &arguments[2]};
  pack.args_ = argument_ptrs;
  pack.arg_cnt_ = 3;
  ObOutputPackInfo info(owner, T_OP_OUTPUT_PACK);
  ASSERT_EQ(OB_SUCCESS, info.param_fields_.reserve(2));
  ObAccuracy accuracy;
  accuracy.set_length(LENGTH_UNKNOWN_YET);
  SqlTypeMeta meta;
  meta.set_type(ObCollectionSQLType);
  meta.set_subschema_id(id);
  meta.set_has_lob_header();
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_sql_type(exec, meta, accuracy, id, owner, type));
  ASSERT_NE(nullptr, type);
  for (int i = 1; i <= 2; ++i) {
    ObExpr &arg = arguments[i];
    arg.type_ = T_REF_COLUMN;
    arg.frame_idx_ = 0;
    arg.datum_off_ = i * 64;
    arg.obj_meta_ = meta;
    arg.datum_meta_ = ObDatumMeta(ObCollectionSQLType, static_cast<ObCollationType>(id), -1);
    arg.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(ObCollectionSQLType);
    arg.max_length_ = LENGTH_UNKNOWN_YET;
    ASSERT_EQ(OB_SUCCESS, arg.bind_sql_type(*type, owner));
    new (frame + arg.datum_off_) Datum();
    ObField field;
    field.type_meta_ = meta;
    field.accuracy_ = accuracy;
    ASSERT_EQ(OB_SUCCESS, info.param_fields_.push_back(field));
  }
  Datum &null_input = arguments[1].locate_expr_datum(eval);
  Datum &present_input = arguments[2].locate_expr_datum(eval);
  null_input.set_null();
  ASSERT_EQ(OB_SUCCESS, present_input.set_string(lob.data(), lob.size()));
  for (const auto protocol : {TEXT, BINARY}) {
    SCOPED_TRACE(static_cast<int>(protocol));
    ObSEArray<ObMySQLCellValue, 2> cells;
    ASSERT_EQ(OB_SUCCESS, result_member(OutputPackRowTag())(
        pack, eval, &session, owner, &info, nullptr, protocol, cells));
    ASSERT_EQ(2, cells.count());
    EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, cells.at(0).kind_);
    EXPECT_EQ(ObMySQLCellValueKind::LENENC_BYTES, cells.at(1).kind_);
    EXPECT_EQ(std::string("[7,-9,NULL]"),
        std::string(cells.at(1).get_bytes(), cells.at(1).get_bytes_len()));
    EXPECT_TRUE(null_input.is_null());
    EXPECT_FALSE(present_input.is_null());
    EXPECT_EQ(lob, std::string(present_input.get_string().ptr(), present_input.get_string().length()));
    EXPECT_EQ(meta, arguments[1].obj_meta_);
    EXPECT_EQ(ObCollectionSQLType, arguments[1].get_sql_type().kind());
    EXPECT_EQ(id, arguments[1].obj_meta_.get_subschema_id());
  }
  EXPECT_EQ(0, service.calls_);
  // A present but empty collection payload is malformed, not SQL NULL.
  // It must still reach the existing decoder and fail without publishing a cell.
  pack.arg_cnt_ = 2;
  ASSERT_EQ(OB_SUCCESS, null_input.set_string(nullptr, 0));
  ASSERT_FALSE(null_input.is_null());
  for (const auto protocol : {TEXT, BINARY}) {
    SCOPED_TRACE(static_cast<int>(protocol));
    ObSEArray<ObMySQLCellValue, 2> cells;
    EXPECT_EQ(OB_ERR_UNEXPECTED, result_member(OutputPackRowTag())(
        pack, eval, &session, owner, &info, nullptr, protocol, cells));
    EXPECT_EQ(0, cells.count());
    EXPECT_FALSE(null_input.is_null());
  }
  EXPECT_EQ(0, service.calls_);
  exec.set_frames(nullptr);
}

TEST(SessionNativeResult, output_pack_typed_lob_json_geometry_null_keeps_null_cell)
{
  using namespace oceanbase::obmysql;
  ObArenaAllocator owner;
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  ObExecContext exec(owner);
  exec.set_my_session(&session);
  OutputPackNoIoLobService service;
  OutputPackInlineLobContext context;
  exec.set_lob_read_service(&service);
  OutputPackInlineContextGuard context_guard(exec, context);
  alignas(8) char frame[256]{};
  char *frames[] = {frame};
  exec.set_frames(frames);
  ObEvalCtx eval(exec);
  for (const SqlValueType kind : {ObLongTextType, ObJsonType, ObGeometryType}) {
    SCOPED_TRACE(static_cast<int>(kind));
    ObExpr pack, argument;
    ObExpr *argument_ptrs[] = {nullptr, &argument};
    pack.args_ = argument_ptrs;
    pack.arg_cnt_ = 2;
    argument.type_ = T_REF_COLUMN;
    argument.frame_idx_ = 0;
    argument.datum_off_ = 64;
    argument.obj_meta_.set_type(kind);
    argument.obj_meta_.set_collation_type(CS_TYPE_BINARY);
    argument.obj_meta_.set_collation_level(CS_LEVEL_IMPLICIT);
    argument.obj_meta_.set_has_lob_header();
    argument.datum_meta_ = ObDatumMeta(kind, CS_TYPE_BINARY, -1);
    argument.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(kind);
    ObAccuracy accuracy;
    accuracy.set_full_length(1024, LS_BYTE);
    accuracy.set_scale(0);
    ASSERT_EQ(OB_SUCCESS, argument.prepare_scalar_type(argument.obj_meta_, accuracy, owner));
    new (frame + argument.datum_off_) Datum();
    argument.locate_expr_datum(eval).set_null();
    const SqlTypeMeta original_meta = argument.obj_meta_;
    ObOutputPackInfo info(owner, T_OP_OUTPUT_PACK);
    ASSERT_EQ(OB_SUCCESS, info.param_fields_.reserve(1));
    ObField field;
    field.type_meta_ = original_meta;
    field.accuracy_ = accuracy;
    ASSERT_EQ(OB_SUCCESS, info.param_fields_.push_back(field));
    for (const auto protocol : {TEXT, BINARY}) {
      SCOPED_TRACE(static_cast<int>(protocol));
      ObSEArray<ObMySQLCellValue, 1> cells;
      ASSERT_EQ(OB_SUCCESS, result_member(OutputPackRowTag())(
          pack, eval, &session, owner, &info, nullptr, protocol, cells));
      ASSERT_EQ(1, cells.count());
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, cells.at(0).kind_);
      EXPECT_TRUE(argument.locate_expr_datum(eval).is_null());
      EXPECT_EQ(original_meta, argument.obj_meta_);
      EXPECT_EQ(kind, argument.get_sql_type().kind());
    }
  }
  EXPECT_EQ(0, service.calls_);
  exec.set_frames(nullptr);
}
