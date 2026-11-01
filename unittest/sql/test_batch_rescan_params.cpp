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

#include "query/engine/batch_rescan_params.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native batch parameters must not import the legacy value definition"
#endif
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/sql_type.h"
#include "sql/das/ob_das_context.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
NativeTypedDatum integer(ObIAllocator &owner, int64_t n) {
  NativeTypedDatum p; SqlTypeMeta meta; meta.set_int();
  EXPECT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(19, 0), owner, p.type_));
  p.datum_.set_int(n); return p;
}
DatumParameterMetadata integer_metadata() {
  DatumParameterMetadata m; m.type_meta_.set_int(); m.param_meta_ = m.type_meta_;
  m.accuracy_ = ObAccuracy(19, 0); return m;
}
DatumParameter physical_cell(const ObBatchRescanParams &batch, int64_t row, int64_t column) {
  DatumParameter p;
  const auto &cell = batch.get_one_batch_physical_values(row).at(column);
  const auto &attributes = batch.get_one_batch_metadata(row).at(column);
  EXPECT_EQ(OB_SUCCESS, p.assign_native_borrowed(cell.datum_, attributes.type_meta_));
  p.set_metadata(attributes); return p;
}
std::string bytes(const std::string &hex) {
  std::string out; for (size_t i=0; i<hex.size(); i+=2) { out += char(std::stoul(hex.substr(i,2),nullptr,16)); } return out;
}
std::string encode(const ObBatchRescanParams &batch) {
  std::string out(batch.get_serialize_size(), '\0'); int64_t pos=0;
  EXPECT_EQ(OB_SUCCESS,batch.serialize(&out[0],out.size(),pos)); EXPECT_EQ(out.size(),pos); return out;
}
std::string frame(const std::string &body) {
  std::string out(1+serialization::OB_SERIALIZE_SIZE_NEED_BYTES+body.size(),'\0'); int64_t pos=0;
  EXPECT_EQ(OB_SUCCESS,serialization::encode_vi64(&out[0],out.size(),pos,1));
  EXPECT_EQ(OB_SUCCESS,serialization::encode_fixed_bytes_i64(&out[0],out.size(),pos,body.size()));
  memcpy(&out[pos],body.data(),body.size()); return out;
}
}
TEST(BatchRescanParams, captured_old_batch_frames_roundtrip_with_owned_native_values) {
  std::ifstream file("unittest/sql/data/batch_rescan_params.golden"); ASSERT_TRUE(file.good()); std::string line; int cases=0;
  while (std::getline(file,line)) {
    if (line.empty()||line[0]=='#') { continue; }
    const size_t tab=line.find('\t'); ASSERT_NE(std::string::npos,tab); const std::string name=line.substr(0,tab); SCOPED_TRACE(name);
    std::string wire=bytes(line.substr(tab+1)); ObBatchRescanParams batch; int64_t pos=0;
    ASSERT_EQ(OB_SUCCESS,batch.deserialize(wire.data(),wire.size(),pos)); EXPECT_FALSE(batch.native_ready()); EXPECT_EQ(wire.size(),pos); EXPECT_EQ(wire,encode(batch));
    if (name=="empty") { EXPECT_EQ(0,batch.get_count()); }
    else if(name=="mixed-two-rows") {
      ASSERT_EQ(2,batch.get_count()); EXPECT_EQ(INT64_MIN,batch.get_one_batch_physical_values(0).at(0).datum_.get_int());
      EXPECT_TRUE(physical_cell(batch, 1, 0).is_null()); EXPECT_EQ(1792684800123456LL,physical_cell(batch, 1, 1).datum_.get_timestamp());
      memset(&wire[0],0xa5,wire.size()); EXPECT_EQ(ObString(10,"a\0中文  "),physical_cell(batch, 0, 1).datum_.get_string());
    } else if(name=="nested-array") {
      auto *array=physical_cell(batch, 0, 0).get_array<DatumParameterArray>(); ASSERT_NE(nullptr,array); ASSERT_EQ(1,array->count_); EXPECT_EQ(42,array->data_[0].datum_.get_int());
    } else if(name=="long-string") {
      memset(&wire[0],0xa5,wire.size()); EXPECT_EQ(1024,physical_cell(batch, 0, 0).datum_.get_string().length());
      EXPECT_EQ('x',physical_cell(batch, 0, 0).datum_.get_string().ptr()[1023]);
    }
    ++cases;
  }
  EXPECT_EQ(4,cases);
}
TEST(BatchRescanParams, append_and_assign_own_strings_nested_arrays_and_runtime_metadata) {
  ObArenaAllocator source; ObBatchRescanParams batch, assigned; ObBatchRescanParams::Row row(4*sizeof(NativeTypedDatum)); ObBatchRescanParams::MetadataRow metadata; ObSEArray<int64_t,2> ids,exprs;
  ASSERT_EQ(OB_SUCCESS,ids.push_back(3)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(1));
  DatumParameter value; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY); value.set_type_meta(meta);
  auto *array=DatumParameterArray::alloc(source,1); ASSERT_NE(nullptr,array); ASSERT_EQ(OB_SUCCESS,value.set_array_address(array,source));
  char text[]="mutable"; SqlTypeMeta type; type.set_varchar(); type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  array->data_[0].set_type_meta(type); array->data_[0].set_param_meta(type); array->data_[0].set_length(99); array->data_[0].set_raw_text_info(5,7); array->data_[0].get_param_flag().flag_=0xa5; array->data_[0].datum_.set_string(text,sizeof(text)-1);
  const SqlType *leaf_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(type, array->data_[0].get_accuracy(), source, leaf_type));
  ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(0, *leaf_type, source));
  SqlTypeSpec declaration; declaration.attributes.set_meta_type(meta);
  declaration.form = SqlTypeForm::Array; declaration.heterogeneous = true;
  NativeTypedDatum input; input.datum_ = value.get_datum();
  ASSERT_EQ(OB_SUCCESS, SqlType::create(declaration, source, input.type_));
  ASSERT_EQ(OB_SUCCESS,row.push_back(input)); ASSERT_EQ(OB_SUCCESS,metadata.push_back(value.get_metadata()));
  ASSERT_EQ(OB_SUCCESS,batch.append_batch_rescan_param(ids,row,metadata,exprs));
  memset(text,'x',sizeof(text)); source.reset(); ASSERT_EQ(OB_SUCCESS,assigned.assign(batch)); batch.reset();
  auto *owned=physical_cell(assigned, 0, 0).get_array<DatumParameterArray>(); ASSERT_NE(nullptr,owned);
  EXPECT_EQ(ObString("mutable"),owned->data_[0].datum_.get_string()); EXPECT_EQ(type,owned->data_[0].get_param_meta());
  EXPECT_EQ(99,owned->data_[0].get_length()); EXPECT_EQ(5,owned->data_[0].get_raw_text_pos()); EXPECT_EQ(0xa5,owned->data_[0].get_param_flag().flag_);
  ASSERT_EQ(OB_SUCCESS,assigned.assign(assigned)); EXPECT_EQ(ObString("mutable"),owned->data_[0].datum_.get_string());
}
TEST(BatchRescanParams, malformed_counts_shapes_and_truncated_frames_are_rejected) {
  ObArenaAllocator arena; ObBatchRescanParams source; ObBatchRescanParams::Row row(4*sizeof(NativeTypedDatum)); ObBatchRescanParams::MetadataRow metadata; ObSEArray<int64_t,1> ids,exprs;
  ASSERT_EQ(OB_SUCCESS,ids.push_back(0)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(1)); ASSERT_EQ(OB_SUCCESS,row.push_back(integer(arena,42))); ASSERT_EQ(OB_SUCCESS,metadata.push_back(integer_metadata())); ASSERT_EQ(OB_SUCCESS,source.append_batch_rescan_param(ids,row,metadata,exprs));
  const std::string wire=encode(source);
  for(int64_t prefix=0;prefix<wire.size();++prefix) { ObBatchRescanParams output; int64_t pos=0; EXPECT_NE(OB_SUCCESS,output.deserialize(wire.data(),prefix,pos)); }
  for(int64_t count:{int64_t(-1),INT64_MAX}) {
    char buffer[32]; int64_t pos=0; ASSERT_EQ(OB_SUCCESS,serialization::encode_vi64(buffer,sizeof(buffer),pos,count));
    std::string malformed=frame(std::string(buffer,pos)); ObBatchRescanParams output; pos=0;
    EXPECT_EQ(OB_DESERIALIZE_ERROR,output.deserialize(malformed.data(),malformed.size(),pos)); EXPECT_EQ(0,output.get_count());
  }
  // One row with no values but one slot map is not a valid matrix.
  const std::string invalid=frame(std::string("\x01\x00\x01\x00\x00",5)); ObBatchRescanParams output; int64_t pos=0;
  EXPECT_EQ(OB_DESERIALIZE_ERROR,output.deserialize(invalid.data(),invalid.size(),pos)); EXPECT_EQ(0,output.get_count());
}
TEST(BatchRescanParams, append_rejects_mismatched_or_reordered_slot_maps) {
  ObArenaAllocator arena; ObBatchRescanParams batch; ObBatchRescanParams::Row row(4*sizeof(NativeTypedDatum)); ObBatchRescanParams::MetadataRow metadata; ObSEArray<int64_t,2> ids,exprs;
  ASSERT_EQ(OB_SUCCESS,ids.push_back(2)); ASSERT_EQ(OB_SUCCESS,ids.push_back(5)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(1)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(2));
  ASSERT_EQ(OB_SUCCESS,row.push_back(integer(arena,1))); ASSERT_EQ(OB_SUCCESS,metadata.push_back(integer_metadata())); EXPECT_EQ(OB_INVALID_ARGUMENT,batch.append_batch_rescan_param(ids,row,metadata,exprs)); EXPECT_EQ(0,batch.get_count());
  ASSERT_EQ(OB_SUCCESS,row.push_back(integer(arena,2))); ASSERT_EQ(OB_SUCCESS,metadata.push_back(integer_metadata())); ASSERT_EQ(OB_SUCCESS,batch.append_batch_rescan_param(ids,row,metadata,exprs));
  std::swap(ids.at(0),ids.at(1)); EXPECT_EQ(OB_INVALID_ARGUMENT,batch.append_batch_rescan_param(ids,row,metadata,exprs)); EXPECT_EQ(1,batch.get_count());
  std::swap(ids.at(0),ids.at(1)); exprs.at(0)=0; EXPECT_EQ(OB_INVALID_ARGUMENT,batch.append_batch_rescan_param(ids,row,metadata,exprs)); EXPECT_EQ(1,batch.get_count());
}
TEST(BatchRescanParams, older_v1_frame_can_omit_expression_index_map) {
  ObBatchRescanParams batch; const std::string old=frame(std::string("\x00\x00",2)); int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,batch.deserialize(old.data(),old.size(),pos)); EXPECT_EQ(0,batch.get_count()); EXPECT_TRUE(batch.param_expr_idxs_.empty());
}
TEST(BatchRescanParams, row_builder_captures_before_next_expression_reuses_payload) {
  ObArenaAllocator arena; ObBatchRescanParams batch; NativeTypedDatum value=integer(arena,1);
  const DatumParameterMetadata attributes=integer_metadata();
  ObSEArray<int64_t,2> ids,exprs;ASSERT_EQ(OB_SUCCESS,ids.push_back(2));ASSERT_EQ(OB_SUCCESS,ids.push_back(5));ASSERT_EQ(OB_SUCCESS,exprs.push_back(1));ASSERT_EQ(OB_SUCCESS,exprs.push_back(2));
  ObBatchRescanParams::RowBuilder builder(batch);ASSERT_EQ(OB_SUCCESS,builder.append(value,attributes));
  value.datum_.set_int(2);ASSERT_EQ(OB_SUCCESS,builder.append(value,attributes));value.datum_.set_int(3);
  ASSERT_EQ(OB_SUCCESS,builder.finish(ids,exprs));ASSERT_EQ(1,batch.get_count());
  EXPECT_EQ(1,batch.get_one_batch_physical_values(0).at(0).datum_.get_int());EXPECT_EQ(2,batch.get_one_batch_physical_values(0).at(1).datum_.get_int());
  EXPECT_EQ(OB_STATE_NOT_MATCH,builder.append(value,attributes));EXPECT_EQ(OB_STATE_NOT_MATCH,builder.finish(ids,exprs));EXPECT_EQ(1,batch.get_count());
}
TEST(BatchRescanParams, physical_v1_requires_each_prepared_type_before_native_restore) {
  ObArenaAllocator declarations;
  ObBatchRescanParams source, received;
  const NativeTypedDatum value = integer(declarations, INT64_MIN);
  ObBatchRescanParams::RowBuilder builder(source);
  ObSEArray<int64_t, 1> ids, exprs;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(3)); ASSERT_EQ(OB_SUCCESS, exprs.push_back(1));
  ASSERT_EQ(OB_SUCCESS, builder.append(value, integer_metadata()));
  ASSERT_EQ(OB_SUCCESS, builder.finish(ids, exprs));
  std::string wire = encode(source); int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, received.deserialize(wire.data(), wire.size(), position));
  ASSERT_FALSE(received.native_ready());
  const Datum before = received.get_one_batch_physical_values(0).at(0).datum_;
  ObSEArray<ObBatchRescanParams::TypeRow, 1> types;
  ObBatchRescanParams::TypeRow row;
  ASSERT_EQ(OB_SUCCESS, row.push_back(nullptr)); ASSERT_EQ(OB_SUCCESS, types.push_back(row));
  EXPECT_EQ(OB_INVALID_ARGUMENT, received.bind_received_types(types));
  EXPECT_FALSE(received.native_ready());
  EXPECT_EQ(before.get_int(), received.get_one_batch_physical_values(0).at(0).datum_.get_int());
  types.at(0).at(0) = value.type_;
  ASSERT_EQ(OB_SUCCESS, received.bind_received_types(types));
  ASSERT_TRUE(received.native_ready());
  EXPECT_NE(value.type_, received.get_one_batch_values(0).at(0).type_);
  source.reset(); declarations.reset(); memset(&wire[0], 0xa5, wire.size());
  EXPECT_EQ(INT64_MIN, received.get_one_batch_values(0).at(0).datum_.get_int());
  EXPECT_EQ(ObIntType, received.get_one_batch_values(0).at(0).type_->kind());
}
TEST(BatchRescanParams, typed_transport_preserves_distinct_row_types_and_typed_null_after_source_reset) {
  ObArenaAllocator source_owner;
  ObBatchRescanParams source, received;
  SqlTypeMeta text_meta; text_meta.set_varchar(); text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  const SqlType *text_type = nullptr;
  ObAccuracy text_accuracy; text_accuracy.set_full_length(64, LS_BYTE); text_accuracy.set_scale(0);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(text_meta, text_accuracy, source_owner, text_type));
  NativeTypedDatum scalar = integer(source_owner, INT64_MIN), text, null;
  text.type_ = null.type_ = text_type;
  char bytes[] = {'a', 0, 'b'}; ASSERT_EQ(OB_SUCCESS, text.datum_.set_string(bytes, sizeof(bytes))); null.datum_.set_null();
  DatumParameterMetadata text_attributes; text_attributes.type_meta_ = text_meta;
  text_attributes.accuracy_ = text_accuracy; text_attributes.res_flags_ = 0x1234;
  ObSEArray<int64_t, 1> ids, exprs;
  ASSERT_EQ(OB_SUCCESS, ids.push_back(3)); ASSERT_EQ(OB_SUCCESS, exprs.push_back(1));
  for (int i = 0; i < 3; ++i) {
    ObBatchRescanParams::RowBuilder row(source);
    ASSERT_EQ(OB_SUCCESS, row.append(i == 0 ? scalar : i == 1 ? text : null,
        i == 0 ? integer_metadata() : text_attributes));
    ASSERT_EQ(OB_SUCCESS, row.finish(ids, exprs));
  }
  int64_t size = 0; ASSERT_EQ(OB_SUCCESS, source.native_size(size));
  std::string wire(size, 0); int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, source.native_encode(&wire[0], wire.size(), position)); EXPECT_EQ(size, position);
  position = 0; ASSERT_EQ(OB_SUCCESS, received.native_decode(wire.data(), wire.size(), position));
  ASSERT_TRUE(received.native_ready()); ASSERT_EQ(3, received.get_count()); EXPECT_EQ(size, position);
  source.reset(); source_owner.reset(); memset(bytes, 'x', sizeof(bytes)); memset(&wire[0], 0xa5, wire.size());
  EXPECT_EQ(INT64_MIN, received.get_one_batch_values(0).at(0).datum_.get_int());
  EXPECT_EQ(ObIntType, received.get_one_batch_values(0).at(0).type_->kind());
  EXPECT_EQ(ObString(3, "a\0b"), received.get_one_batch_values(1).at(0).datum_.get_string());
  EXPECT_EQ(ObVarcharType, received.get_one_batch_values(1).at(0).type_->kind());
  EXPECT_TRUE(received.get_one_batch_values(2).at(0).datum_.is_null());
  EXPECT_EQ(ObVarcharType, received.get_one_batch_values(2).at(0).type_->kind());
  EXPECT_EQ(0x1234, received.get_one_batch_metadata(1).at(0).res_flags_);
}
namespace {
class BatchAllocationCounter final : public ObIAllocator {
public:
  ObArenaAllocator owner_; int64_t calls_ = 0, last_size_ = 0, fail_at_ = -1;
  void *alloc(int64_t size) override { last_size_ = size; return ++calls_ == fail_at_ ? nullptr : owner_.alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override { owner_.free(p); }
};
}
TEST(BatchRescanParams, group_column_payload_failure_does_not_publish_a_partial_prefix) {
  ObArenaAllocator source, descriptors;
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  NativeTypedDatum value;
  ObAccuracy text_accuracy; text_accuracy.set_full_length(64, LS_BYTE); text_accuracy.set_scale(0);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, text_accuracy, source, value.type_));
  const char payload[] = "a real retained reference payload";
  ASSERT_EQ(OB_SUCCESS, value.datum_.set_string(payload, sizeof(payload) - 1));
  DatumParameterMetadata attributes; attributes.type_meta_ = meta;
  GroupRescanParam column; ASSERT_EQ(OB_SUCCESS, column.init(3, 2, descriptors));
  BatchAllocationCounter success;
  ASSERT_EQ(OB_SUCCESS, column.stage(value, attributes, success));
  EXPECT_EQ(0, column.count_); ++column.count_;
  ASSERT_GT(success.calls_, 0); EXPECT_EQ(sizeof(payload) - 1, success.last_size_);
  BatchAllocationCounter fail; fail.fail_at_ = success.calls_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, column.stage(value, attributes, fail));
  EXPECT_EQ(success.calls_, fail.calls_); EXPECT_EQ(sizeof(payload) - 1, fail.last_size_);
  EXPECT_EQ(1, column.count_); EXPECT_TRUE(column.values_.at(1).datum_.is_null());
  source.reset();
  EXPECT_EQ(ObString(sizeof(payload) - 1, payload), column.values_.at(0).datum_.get_string());
  EXPECT_EQ(ObVarcharType, column.values_.at(0).type_->kind());
}
int main(int argc,char **argv) {
  ::testing::InitGoogleTest(&argc,argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  return RUN_ALL_TESTS();
}
