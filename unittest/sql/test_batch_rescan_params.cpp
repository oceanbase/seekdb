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
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
DatumParameter integer(ObIAllocator &arena, int64_t value) {
  DatumParameter p; SqlTypeMeta meta; meta.set_int(); p.set_type_meta(meta);
  EXPECT_EQ(OB_SUCCESS, p.alloc_datum_reserved_buff(meta, -1, arena)); p.datum_.set_int(value); return p;
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
    ASSERT_EQ(OB_SUCCESS,batch.deserialize(wire.data(),wire.size(),pos)); EXPECT_EQ(wire.size(),pos); EXPECT_EQ(wire,encode(batch));
    if (name=="empty") { EXPECT_EQ(0,batch.get_count()); }
    else if(name=="mixed-two-rows") {
      ASSERT_EQ(2,batch.get_count()); EXPECT_EQ(INT64_MIN,batch.get_one_batch_params(0).at(0).datum_.get_int());
      EXPECT_TRUE(batch.get_one_batch_params(1).at(0).is_null()); EXPECT_EQ(1792684800123456LL,batch.get_one_batch_params(1).at(1).datum_.get_timestamp());
      memset(&wire[0],0xa5,wire.size()); EXPECT_EQ(ObString(10,"a\0中文  "),batch.get_one_batch_params(0).at(1).datum_.get_string());
    } else if(name=="nested-array") {
      auto *array=batch.get_one_batch_params(0).at(0).get_array<DatumParameterArray>(); ASSERT_NE(nullptr,array); ASSERT_EQ(1,array->count_); EXPECT_EQ(42,array->data_[0].datum_.get_int());
    } else if(name=="long-string") {
      memset(&wire[0],0xa5,wire.size()); EXPECT_EQ(1024,batch.get_one_batch_params(0).at(0).datum_.get_string().length());
      EXPECT_EQ('x',batch.get_one_batch_params(0).at(0).datum_.get_string().ptr()[1023]);
    }
    ++cases;
  }
  EXPECT_EQ(4,cases);
}
TEST(BatchRescanParams, append_and_assign_own_strings_nested_arrays_and_runtime_metadata) {
  ObArenaAllocator source; ObBatchRescanParams batch, assigned; ObBatchRescanParams::Row row(4*sizeof(DatumParameter)); ObSEArray<int64_t,2> ids,exprs;
  ASSERT_EQ(OB_SUCCESS,ids.push_back(3)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(1));
  DatumParameter value; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY); value.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS,value.alloc_datum_reserved_buff(meta,-1,source)); auto *array=DatumParameterArray::alloc(source,1); ASSERT_NE(nullptr,array); value.set_array_address(array);
  char text[]="mutable"; SqlTypeMeta type; type.set_varchar(); type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  array->data_[0].set_type_meta(type); array->data_[0].set_param_meta(type); array->data_[0].set_length(99); array->data_[0].set_raw_text_info(5,7); array->data_[0].get_param_flag().flag_=0xa5; array->data_[0].datum_.set_string(text,sizeof(text)-1);
  ASSERT_EQ(OB_SUCCESS,row.push_back(value)); ASSERT_EQ(OB_SUCCESS,batch.append_batch_rescan_param(ids,row,exprs));
  memset(text,'x',sizeof(text)); source.reset(); ASSERT_EQ(OB_SUCCESS,assigned.assign(batch)); batch.reset();
  auto *owned=assigned.get_one_batch_params(0).at(0).get_array<DatumParameterArray>(); ASSERT_NE(nullptr,owned);
  EXPECT_EQ(ObString("mutable"),owned->data_[0].datum_.get_string()); EXPECT_EQ(type,owned->data_[0].get_param_meta());
  EXPECT_EQ(99,owned->data_[0].get_length()); EXPECT_EQ(5,owned->data_[0].get_raw_text_pos()); EXPECT_EQ(0xa5,owned->data_[0].get_param_flag().flag_);
  ASSERT_EQ(OB_SUCCESS,assigned.assign(assigned)); EXPECT_EQ(ObString("mutable"),owned->data_[0].datum_.get_string());
}
TEST(BatchRescanParams, malformed_counts_shapes_and_truncated_frames_are_rejected) {
  ObArenaAllocator arena; ObBatchRescanParams source; ObBatchRescanParams::Row row(4*sizeof(DatumParameter)); ObSEArray<int64_t,1> ids,exprs;
  ASSERT_EQ(OB_SUCCESS,ids.push_back(0)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(1)); ASSERT_EQ(OB_SUCCESS,row.push_back(integer(arena,42))); ASSERT_EQ(OB_SUCCESS,source.append_batch_rescan_param(ids,row,exprs));
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
  ObArenaAllocator arena; ObBatchRescanParams batch; ObBatchRescanParams::Row row(4*sizeof(DatumParameter)); ObSEArray<int64_t,2> ids,exprs;
  ASSERT_EQ(OB_SUCCESS,ids.push_back(2)); ASSERT_EQ(OB_SUCCESS,ids.push_back(5)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(1)); ASSERT_EQ(OB_SUCCESS,exprs.push_back(2));
  ASSERT_EQ(OB_SUCCESS,row.push_back(integer(arena,1))); EXPECT_EQ(OB_INVALID_ARGUMENT,batch.append_batch_rescan_param(ids,row,exprs)); EXPECT_EQ(0,batch.get_count());
  ASSERT_EQ(OB_SUCCESS,row.push_back(integer(arena,2))); ASSERT_EQ(OB_SUCCESS,batch.append_batch_rescan_param(ids,row,exprs));
  std::swap(ids.at(0),ids.at(1)); EXPECT_EQ(OB_INVALID_ARGUMENT,batch.append_batch_rescan_param(ids,row,exprs)); EXPECT_EQ(1,batch.get_count());
  std::swap(ids.at(0),ids.at(1)); exprs.at(0)=0; EXPECT_EQ(OB_INVALID_ARGUMENT,batch.append_batch_rescan_param(ids,row,exprs)); EXPECT_EQ(1,batch.get_count());
}
TEST(BatchRescanParams, older_v1_frame_can_omit_expression_index_map) {
  ObBatchRescanParams batch; const std::string old=frame(std::string("\x00\x00",2)); int64_t pos=0;
  ASSERT_EQ(OB_SUCCESS,batch.deserialize(old.data(),old.size(),pos)); EXPECT_EQ(0,batch.get_count()); EXPECT_TRUE(batch.param_expr_idxs_.empty());
}
TEST(BatchRescanParams, row_builder_captures_before_next_expression_reuses_payload) {
  ObArenaAllocator arena; ObBatchRescanParams batch; DatumParameter value=integer(arena,1);
  ObSEArray<int64_t,2> ids,exprs;ASSERT_EQ(OB_SUCCESS,ids.push_back(2));ASSERT_EQ(OB_SUCCESS,ids.push_back(5));ASSERT_EQ(OB_SUCCESS,exprs.push_back(1));ASSERT_EQ(OB_SUCCESS,exprs.push_back(2));
  ObBatchRescanParams::RowBuilder builder(batch);ASSERT_EQ(OB_SUCCESS,builder.append(value));
  value.datum_.set_int(2);ASSERT_EQ(OB_SUCCESS,builder.append(value));value.datum_.set_int(3);
  ASSERT_EQ(OB_SUCCESS,builder.finish(ids,exprs));ASSERT_EQ(1,batch.get_count());
  EXPECT_EQ(1,batch.get_one_batch_params(0).at(0).datum_.get_int());EXPECT_EQ(2,batch.get_one_batch_params(0).at(1).datum_.get_int());
  EXPECT_EQ(OB_STATE_NOT_MATCH,builder.append(value));EXPECT_EQ(OB_STATE_NOT_MATCH,builder.finish(ids,exprs));EXPECT_EQ(1,batch.get_count());
}
int main(int argc,char **argv) {
  ::testing::InitGoogleTest(&argc,argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  return RUN_ALL_TESTS();
}
