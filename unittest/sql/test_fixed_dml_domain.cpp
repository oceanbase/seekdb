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

#include "sql/das/ob_das_domain_utils.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/json_type/ob_json_tree.h"
#include "lib/charset/ob_charset.h"
#include "share/ob_i_lob_read_service.h"
#include "share/ob_lob_access_utils.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using Row = ObChunkDatumStore::StoredRow;

ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum text(const std::string &value)
{ return ObDatum(value.data(), value.size(), false); }
ObDatum text(const ObString &value)
{ return ObDatum(value.ptr(), value.length(), false); }
ObDatum null_value()
{ ObDatum result; result.set_null(); return result; }
std::string bytes(const ObString &value)
{ return std::string(value.ptr(), value.length()); }
std::string bytes(const ObDatum &value)
{ return std::string(value.ptr_, value.len_); }
SqlTypeMeta meta(SqlValueType type)
{
  SqlTypeMeta result; result.set_type(type);
  result.set_collation_type(ob_is_string_type(type) ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
  result.set_collation_level(ob_is_string_type(type) ? CS_LEVEL_IMPLICIT : CS_LEVEL_NUMERIC);
  return result;
}

struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values)
  {
    const uint32_t extra = (8 - (sizeof(Row) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) Row;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(Row), size, extra));
  }
  void overwrite_descriptors()
  { MEMSET(row_->stored_cells(), 0xA5, row_->cnt_ * sizeof(StoredDatumCell)); }
  std::vector<char> memory_;
  Row *row_ = nullptr;
};

// Access actual consumer boundaries without adding a production test hook.
template <typename Tag, typename Tag::Type Member>
struct PrivateMember { friend typename Tag::Type member(Tag) { return Member; } };
#define GEO_MEMBER(Tag, Method) \
  struct Tag { \
    using Type = int (ObSpatialDMLIterator::*)(const Row *, int64_t &, ObString &, SqlTypeMeta &) const; \
    friend Type member(Tag); \
  }; \
  template struct PrivateMember<Tag, &ObSpatialDMLIterator::Method>
GEO_MEMBER(GeoGet, get_geo_wkb);
GEO_MEMBER(GeoUpdateGet, get_geo_wkb_for_update);
#undef GEO_MEMBER
#define FT_MEMBER(Tag, Method) \
  struct Tag { \
    using Type = int (ObFTDMLIterator::*)(const Row *, ObDatum &, ObString &, SqlTypeMeta &); \
    friend Type member(Tag); \
  }; \
  template struct PrivateMember<Tag, &ObFTDMLIterator::Method>
FT_MEMBER(FtGet, get_ft_and_doc_id);
FT_MEMBER(FtUpdateGet, get_ft_and_doc_id_for_update);
#undef FT_MEMBER
#define MVI_MEMBER(Tag, Method) \
  struct Tag { \
    using Type = int (ObMultivalueDMLIterator::*)(const Row *, int64_t &, int64_t &, ObString &); \
    friend Type member(Tag); \
  }; \
  template struct PrivateMember<Tag, &ObMultivalueDMLIterator::Method>
MVI_MEMBER(MviGet, get_multivlaue_json_data);
MVI_MEMBER(MviUpdateGet, get_multivlaue_json_data_for_update);
#undef MVI_MEMBER
struct SaveRowkey
{
  using Type = int (*)(ObIAllocator &, const ObDASDMLBaseCtDef &, const IntFixedArray &,
                      const Row &, int64_t, bool &);
  friend Type member(SaveRowkey);
};
template struct PrivateMember<SaveRowkey, &ObDASDomainUtils::calc_save_rowkey_policy>;
struct PlanState
{
  using Type = data_plane::ObIDmlTablePlanState *data_plane::ObDmlTablePlan::*;
  friend Type member(PlanState);
};
template struct PrivateMember<PlanState, &data_plane::ObDmlTablePlan::state_>;

// Supply table metadata through the existing data-plane protocol. The code
// under test still uses the production ObDmlTableView accessors and projectors.
class TableState final : public data_plane::ObIDmlTablePlanState
{
public:
  void destroy() override {}
  void reset() override {}
  bool is_valid() const override { return true; }
  int build(const share::schema::ObTableSchema *, int64_t, const ObIArray<uint64_t> &) override
  { return OB_NOT_SUPPORTED; }
  int set_data_table_rowkey_tags(share::schema::ObSchemaGetterGuard *, const share::schema::ObTableSchema *) override
  { return OB_NOT_SUPPORTED; }
  int configure_multivalue_index(int64_t) override { return OB_NOT_SUPPORTED; }
  void set_has_async_index(bool) override {}
  const ObIArray<share::schema::ObColDesc> &get_col_descs() const override { return columns_; }
  int serialize(char *, int64_t, int64_t &) const override { return OB_NOT_SUPPORTED; }
  int deserialize(const char *, int64_t, int64_t &) override { return OB_NOT_SUPPORTED; }
  int64_t get_serialize_size() const override { return 0; }
  int64_t to_string(char *, int64_t) const override { return 0; }
  uint64_t get_table_id() const override { return 1001; }
  int64_t get_schema_version() const override { return 1; }
  share::schema::ObIndexType get_index_type() const override
  { return share::schema::INDEX_TYPE_NORMAL_MULTIVALUE_LOCAL; }
  int64_t get_rowkey_column_num() const override { return 1; }
  int64_t get_data_table_rowkey_column_num() const override { return 1; }
  int64_t get_fulltext_col_id() const override { return ft_id_; }
  int get_typed_doc_id_col_id(uint64_t &id, ObDocIDType &type) const override
  { id = 46; type = ObDocIDType::TABLET_SEQUENCE; return OB_SUCCESS; }
  const ObString &get_fts_parser_name() const override { return empty_; }
  const ObString &get_fts_parser_property() const override { return empty_; }
  const ObString &get_index_name() const override { return empty_; }
  uint64_t get_spatial_geo_col_id() const override { return 99; }
  int64_t get_multivalue_col_id() const override { return mvi_id_; }
  int64_t get_multivalue_array_col_id() const override { return 45; }
  int64_t get_vec_id_col_id() const override { return OB_INVALID_ID; }
  int64_t get_vec_chunk_col_id() const override { return OB_INVALID_ID; }
  int64_t get_embedded_vec_col_id() const override { return OB_INVALID_ID; }
  int64_t get_vec_vector_col_id() const override { return OB_INVALID_ID; }
  ObString get_vec_index_param() const override { return empty_; }
  bool is_fts_index_aux() const override { return ft_aux_; }
#define FALSE_VIEW(Name) bool Name() const override { return false; }
  FALSE_VIEW(has_async_index) FALSE_VIEW(can_read_index) FALSE_VIEW(is_storage_index_table)
  FALSE_VIEW(is_unique_index) FALSE_VIEW(is_domain_index) FALSE_VIEW(is_spatial_index)
  FALSE_VIEW(is_fts_index) FALSE_VIEW(is_fts_doc_word_aux) FALSE_VIEW(is_multivalue_index)
  FALSE_VIEW(is_multivalue_index_aux) FALSE_VIEW(is_index_local_storage)
  FALSE_VIEW(is_vector_delta_buffer) FALSE_VIEW(is_vector_index_id) FALSE_VIEW(is_vector_index)
  FALSE_VIEW(is_sparse_vector_index) FALSE_VIEW(is_ivf_vector_index) FALSE_VIEW(is_hybrid_vector_index)
  FALSE_VIEW(is_hybrid_vector_index_log) FALSE_VIEW(is_no_need_update_vector_index)
#undef FALSE_VIEW
  uint64_t ft_id_ = 33;
  uint64_t mvi_id_ = 44;
  bool ft_aux_ = false;
private:
  ObString empty_;
  ObSEArray<share::schema::ObColDesc, 1> columns_;
};

struct BindState
{
  BindState(data_plane::ObDmlTablePlan &plan, TableState &state)
    : plan_(plan), saved_(plan.*member(PlanState{})) { plan_.*member(PlanState{}) = &state; }
  ~BindState() { plan_.*member(PlanState{}) = saved_; }
  data_plane::ObDmlTablePlan &plan_;
  data_plane::ObIDmlTablePlanState *saved_;
};

class RejectLobRead final : public ObILobReadService
{
public:
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
  { ++calls_; return OB_TIMEOUT; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, SqlValueType, ObCollationType, ObLobLocatorV2 &,
                            ObIAllocator *, ObString &) override { ++calls_; return OB_TIMEOUT; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *, uint32_t) override
  { ++calls_; return OB_TIMEOUT; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
                      ObString &, ObTextStringIterState &) override { ++calls_; return OB_TIMEOUT; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool, ObString &,
                           ObTextStringIterState &) override { ++calls_; return OB_TIMEOUT; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
  { ++calls_; return OB_TIMEOUT; }
  void free_lob_query_iter(ObLobTextIterCtx &) override {}
  int calls_ = 0;
};

class FixedDmlDomain : public ::testing::Test
{
protected:
  FixedDmlDomain() : arena_(ObMemAttr("DmlDomainTest")), ctdef_(arena_), main_(arena_),
      projector_(arena_), binding_(ctdef_.table_param_, table_), lob_options_(lob_service_, INT64_MAX) {}
  void columns(const std::vector<uint64_t> &ids, const std::vector<SqlTypeMeta> &types,
               const std::vector<int64_t> &projector)
  {
    ASSERT_EQ(ids.size(), types.size()); ASSERT_EQ(ids.size(), projector.size());
    ASSERT_EQ(OB_SUCCESS, ctdef_.column_ids_.init(ids.size()));
    ASSERT_EQ(OB_SUCCESS, ctdef_.column_types_.init(types.size()));
    ASSERT_EQ(OB_SUCCESS, ctdef_.column_accuracys_.init(types.size()));
    ASSERT_EQ(OB_SUCCESS, projector_.init(projector.size()));
    for (size_t i = 0; i < ids.size(); ++i) {
      ASSERT_EQ(OB_SUCCESS, ctdef_.column_ids_.push_back(ids[i]));
      ASSERT_EQ(OB_SUCCESS, ctdef_.column_types_.push_back(types[i]));
      ASSERT_EQ(OB_SUCCESS, ctdef_.column_accuracys_.push_back(ObAccuracy()));
      ASSERT_EQ(OB_SUCCESS, projector_.push_back(projector[i]));
    }
  }
  void update_projectors(int64_t count)
  {
    ASSERT_EQ(OB_SUCCESS, ctdef_.old_row_projector_.init(count));
    ASSERT_EQ(OB_SUCCESS, ctdef_.new_row_projector_.init(count));
    for (int64_t i = 0; i < count; ++i) {
      ASSERT_EQ(OB_SUCCESS, ctdef_.old_row_projector_.push_back(i));
      ASSERT_EQ(OB_SUCCESS, ctdef_.new_row_projector_.push_back(i));
    }
  }
  void json(const std::vector<ObDatum> &values, const SqlTypeMeta &type, ObString &result)
  {
    int64_t size = sizeof(uint32_t);
    for (const auto &value : values) {
      int64_t value_size = 0;
      ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::value_size(value, type, value_size));
      size += value_size;
    }
    std::vector<char> payload(size); const uint32_t count = values.size();
    MEMCPY(payload.data(), &count, sizeof(count));
    int64_t pos = sizeof(count);
    for (const auto &value : values) {
      ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::encode_value(value, type, payload.data(), size, pos));
    }
    ASSERT_EQ(size, pos);
    ObJsonOpaque opaque(ObString(size, payload.data()), ObUserDefinedSQLType);
    ASSERT_EQ(OB_SUCCESS, opaque.get_raw_binary(result, &arena_));
  }
  ObArenaAllocator arena_;
  ObDASInsCtDef ctdef_;
  ObDASInsCtDef main_;
  IntFixedArray projector_;
  TableState table_;
  BindState binding_;
  ObDASWriteBuffer::Iterator write_iter_;
  RejectLobRead lob_service_;
  ObLobReadOptions lob_options_;
};

TEST_F(FixedDmlDomain, spatial_getter_projects_geometry_and_borrows_fixed_payload)
{
  columns({1, 99}, {meta(ObIntType), meta(ObGeometryType)}, {1, 0});
  ASSERT_EQ(OB_SUCCESS, main_.column_ids_.init(1));
  ASSERT_EQ(OB_SUCCESS, main_.column_types_.init(1));
  SqlTypeMeta geometry = meta(ObGeometryType); geometry.set_has_lob_header();
  ASSERT_EQ(OB_SUCCESS, main_.column_ids_.push_back(99));
  ASSERT_EQ(OB_SUCCESS, main_.column_types_.push_back(geometry));
  std::string value("w\0kb", 4); int64_t key = 7; OwnedRow source({text(value), integer(key)});
  const char *payload = source.row_->stored_cells()[0].ptr_;
  ObSpatialDMLIterator iter(arena_, &projector_, write_iter_, &ctdef_, &main_, nullptr, &lob_options_);
  int64_t index = -1; ObString output; SqlTypeMeta type;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(GeoGet{}))(source.row_, index, output, type));
  source.overwrite_descriptors();
  EXPECT_EQ(0, index); EXPECT_EQ(payload, output.ptr()); EXPECT_EQ(value, bytes(output));
  EXPECT_EQ(ObGeometryType, type.get_type()); EXPECT_TRUE(type.has_lob_header());
}

TEST_F(FixedDmlDomain, spatial_update_validates_projectors_and_keeps_null_geometry_empty)
{
  columns({1, 99}, {meta(ObIntType), meta(ObGeometryType)}, {0, 1});
  int64_t key = 7; OwnedRow source({integer(key), null_value()});
  ObSpatialDMLIterator iter(arena_, &projector_, write_iter_, &ctdef_, nullptr, nullptr, &lob_options_);
  int64_t index = 23; ObString output; SqlTypeMeta type;
  EXPECT_EQ(OB_ERR_UNEXPECTED, (iter.*member(GeoUpdateGet{}))(source.row_, index, output, type));
  EXPECT_EQ(-1, index);
  update_projectors(2);
  ASSERT_EQ(OB_SUCCESS, (iter.*member(GeoUpdateGet{}))(source.row_, index, output, type));
  EXPECT_EQ(1, index); EXPECT_TRUE(output.empty());
  EXPECT_EQ(ObGeometryType, type.get_type()); EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, type.get_collation_type());
}

TEST_F(FixedDmlDomain, fulltext_getter_copies_numeric_doc_descriptor_and_keeps_flags)
{
  columns({46, 33}, {meta(ObIntType), meta(ObVarcharType)}, {2, 0});
  std::string value("word\0list", 9); int64_t id = INT64_MIN, ignored = 2;
  ObDatum flagged = integer(id); flagged.set_has_lob_header();
  OwnedRow source({text(value), integer(ignored), flagged});
  const char *payload = source.row_->stored_cells()[2].ptr_;
  const auto pack = source.row_->stored_cells()[2].pack_;
  ObFTDMLIterator iter(ObDomainDMLMode::DOMAIN_DML_MODE_DEFAULT, nullptr,
      arena_, &projector_, write_iter_, &ctdef_, &main_, &lob_options_);
  ObDatum doc; ObString output; SqlTypeMeta type;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(FtGet{}))(source.row_, doc, output, type));
  source.overwrite_descriptors();
  EXPECT_EQ(INT64_MIN, doc.get_int()); EXPECT_EQ(pack, doc.pack_); EXPECT_EQ(payload, doc.ptr_);
  EXPECT_EQ(value, bytes(output)); EXPECT_EQ(ObVarcharType, type.get_type()); EXPECT_EQ(0, lob_service_.calls_);
}

TEST_F(FixedDmlDomain, fulltext_aux_update_borrows_tablet_sequence_docid_and_reads_inrow_lob)
{
  table_.ft_aux_ = true;
  columns({33, 46}, {meta(ObLongTextType), meta(ObVarcharType)}, {2, 0}); update_projectors(3);
  std::string id("tablet\0seq", 10), value = "fulltext"; int64_t ignored = 8;
  ObString lob; ASSERT_EQ(OB_SUCCESS, lob_helper::pack_to_disk_inrow_lob(arena_,
      ObString(value.size(), value.data()), lob));
  ObDatum flagged = text(id); flagged.set_has_lob_header();
  OwnedRow source({flagged, integer(ignored), text(lob)});
  const char *payload = source.row_->stored_cells()[0].ptr_;
  const auto pack = source.row_->stored_cells()[0].pack_;
  ObFTDMLIterator iter(ObDomainDMLMode::DOMAIN_DML_MODE_DEFAULT, nullptr,
      arena_, &projector_, write_iter_, &ctdef_, nullptr, &lob_options_);
  ObDatum doc; ObString output; SqlTypeMeta type;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(FtUpdateGet{}))(source.row_, doc, output, type));
  source.overwrite_descriptors();
  EXPECT_EQ(id, bytes(doc)); EXPECT_EQ(payload, doc.ptr_); EXPECT_EQ(pack, doc.pack_);
  EXPECT_EQ(value, bytes(output)); EXPECT_EQ(ObLongTextType, type.get_type()); EXPECT_EQ(0, lob_service_.calls_);
}

TEST_F(FixedDmlDomain, fulltext_null_docid_and_early_errors_preserve_output)
{
  columns({46, 33}, {meta(ObIntType), meta(ObVarcharType)}, {0, 1});
  std::string value = "text"; OwnedRow source({null_value(), text(value)});
  ObFTDMLIterator iter(ObDomainDMLMode::DOMAIN_DML_MODE_DEFAULT, nullptr,
      arena_, &projector_, write_iter_, &ctdef_, &main_, &lob_options_);
  ObDatum doc; ObString output; SqlTypeMeta type;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(FtGet{}))(source.row_, doc, output, type)); EXPECT_TRUE(doc.is_null());
  int64_t sentinel = 19; doc = integer(sentinel); output.assign_ptr(value.data(), value.size());
  table_.ft_id_ = OB_INVALID_ID;
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(FtGet{}))(source.row_, doc, output, type));
  EXPECT_EQ(19, doc.get_int()); EXPECT_EQ(value, bytes(output));
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(FtUpdateGet{}))(source.row_, doc, output, type));
  EXPECT_EQ(19, doc.get_int());
}

TEST_F(FixedDmlDomain, multivalue_getters_read_projected_array_lob_and_reject_missing_column)
{
  columns({44, 1, 46}, {meta(ObIntType), meta(ObVarcharType), meta(ObIntType)}, {1, 0, 3});
  std::string value("opaque\0payload", 14); int64_t key = 1, id = 9;
  ObString lob; ASSERT_EQ(OB_SUCCESS, lob_helper::pack_to_disk_inrow_lob(arena_,
      ObString(value.size(), value.data()), lob));
  OwnedRow source({integer(key), null_value(), text(lob), integer(id)});
  ObMultivalueDMLIterator iter(arena_, &projector_, write_iter_, &ctdef_, &main_, &lob_options_);
  int64_t index = -1, array_index = -1; ObString output, update_output;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(MviGet{}))(source.row_, index, array_index, output));
  EXPECT_EQ(1, index); EXPECT_EQ(2, array_index); EXPECT_EQ(value, bytes(output));
  ASSERT_EQ(OB_SUCCESS, (iter.*member(MviUpdateGet{}))(source.row_, index, array_index, update_output));
  source.overwrite_descriptors();
  EXPECT_EQ(value, bytes(update_output)); EXPECT_EQ(0, lob_service_.calls_);
  table_.mvi_id_ = OB_INVALID_ID;
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(MviGet{}))(source.row_, index, array_index, output));
}

TEST_F(FixedDmlDomain, multivalue_output_owns_decoded_scalar_and_borrows_projected_rowkey)
{
  columns({44, 1, 46}, {meta(ObVarcharType), meta(ObVarcharType), meta(ObIntType)}, {0, 2, 3});
  std::string scalar("scalar\0value", 12), rowkey = "original-key"; int64_t id = INT64_MAX;
  ObString encoded; json({text(scalar)}, meta(ObVarcharType), encoded);
  ObDatum flagged = text(rowkey); flagged.set_has_lob_header();
  OwnedRow source({null_value(), null_value(), flagged, integer(id)});
  const char *rowkey_payload = source.row_->stored_cells()[2].ptr_;
  const auto rowkey_pack = source.row_->stored_cells()[2].pack_;
  ObDomainIndexRow output;
  ASSERT_EQ(OB_SUCCESS, ObDASDomainUtils::generate_multivalue_index_rows(arena_, ctdef_, 0, 1,
      encoded, projector_, *source.row_, output));
  ASSERT_EQ(1, output.count()); ASSERT_EQ(3, output.at(0)->count_);
  auto &datums = output.at(0)->storage_datums_;
  EXPECT_EQ(rowkey_payload, datums[1].ptr_); EXPECT_EQ(rowkey_pack, datums[1].pack_);
  const uintptr_t scalar_address = reinterpret_cast<uintptr_t>(datums[0].ptr_);
  const uintptr_t encoded_address = reinterpret_cast<uintptr_t>(encoded.ptr());
  EXPECT_TRUE(scalar_address < encoded_address || scalar_address >= encoded_address + encoded.length());
  MEMSET(encoded.ptr(), 0xA5, encoded.length()); source.overwrite_descriptors();
  EXPECT_EQ(scalar, bytes(datums[0])); EXPECT_EQ(rowkey, bytes(datums[1])); EXPECT_EQ(INT64_MAX, datums[2].get_int());
}

TEST_F(FixedDmlDomain, multivalue_output_keeps_nulls_and_does_not_publish_first_bad_scalar)
{
  columns({44, 1, 46}, {meta(ObIntType), meta(ObVarcharType), meta(ObIntType)}, {0, 2, 3});
  int64_t scalar = INT64_MIN; OwnedRow source({null_value(), null_value(), null_value(), null_value()});
  ObString encoded; json({integer(scalar)}, meta(ObIntType), encoded); ObDomainIndexRow output;
  ASSERT_EQ(OB_SUCCESS, ObDASDomainUtils::generate_multivalue_index_rows(arena_, ctdef_, 0, 1,
      encoded, projector_, *source.row_, output));
  ASSERT_EQ(1, output.count()); EXPECT_EQ(INT64_MIN, output.at(0)->storage_datums_[0].get_int());
  EXPECT_TRUE(output.at(0)->storage_datums_[1].is_null()); EXPECT_TRUE(output.at(0)->storage_datums_[2].is_null());
  std::string wrong_type = "wrong-type"; json({text(wrong_type)}, meta(ObVarcharType), encoded);
  ObDomainIndexRow failed;
  EXPECT_EQ(OB_DESERIALIZE_ERROR, ObDASDomainUtils::generate_multivalue_index_rows(arena_, ctdef_, 0, 1,
      encoded, projector_, *source.row_, failed));
  EXPECT_EQ(0, failed.count());
}

TEST_F(FixedDmlDomain, rowkey_policy_uses_projected_value_size_threshold_and_null)
{
  columns({44, 1, 46}, {meta(ObIntType), meta(ObVarcharType), meta(ObIntType)}, {0, 2, 3});
  std::string long_key(64, 'k'); int64_t id = 8;
  OwnedRow source({null_value(), null_value(), text(long_key), integer(id)}); bool save = false;
  ASSERT_EQ(OB_SUCCESS, member(SaveRowkey{})(arena_, ctdef_, projector_, *source.row_, 5, save)); EXPECT_TRUE(save);
  ASSERT_EQ(OB_SUCCESS, member(SaveRowkey{})(arena_, ctdef_, projector_, *source.row_, 6, save)); EXPECT_FALSE(save);
  OwnedRow null_key({null_value(), null_value(), null_value(), integer(id)});
  ASSERT_EQ(OB_SUCCESS, member(SaveRowkey{})(arena_, ctdef_, projector_, *null_key.row_, 6, save)); EXPECT_TRUE(save);
}

TEST_F(FixedDmlDomain, rowkey_policy_rejects_nonnull_negative_decimal_scale_but_skips_array_column)
{
  SqlTypeMeta decimal = meta(ObDecimalIntType); decimal.set_scale(-1);
  columns({44, 1, 46}, {meta(ObIntType), decimal, meta(ObIntType)}, {0, 2, 3});
  int64_t value = 8; OwnedRow source({null_value(), null_value(), integer(value), integer(value)}); bool save = false;
  EXPECT_EQ(OB_ERR_UNEXPECTED, member(SaveRowkey{})(arena_, ctdef_, projector_, *source.row_, 6, save));
  OwnedRow null_key({null_value(), null_value(), null_value(), integer(value)});
  EXPECT_EQ(OB_SUCCESS, member(SaveRowkey{})(arena_, ctdef_, projector_, *null_key.row_, 6, save)); EXPECT_TRUE(save);
  ctdef_.column_ids_.at(1) = 45;
  EXPECT_EQ(OB_SUCCESS, member(SaveRowkey{})(arena_, ctdef_, projector_, *source.row_, 6, save)); EXPECT_TRUE(save);
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (OB_SUCCESS != ObCharset::init_charset()) { return 1; }
  return RUN_ALL_TESTS();
}
