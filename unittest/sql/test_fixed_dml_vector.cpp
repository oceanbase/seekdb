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

#include "sql/das/ob_das_dml_vec_iter.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "common/udt/ob_array_fixed_size.h"
#include "common/udt/ob_map_type.h"
#include "lib/charset/ob_charset.h"
#include "share/ob_i_lob_read_service.h"
#include "share/ob_lob_access_utils.h"
#include <gtest/gtest.h>
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
std::string bytes(const ObString &value) { return std::string(value.ptr(), value.length()); }
std::string bytes(const ObDatum &value) { return std::string(value.ptr_, value.len_); }

struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values)
  {
    const uint32_t extra = (8 - (sizeof(Row) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size); row_ = new (memory_.data()) Row;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(Row), size, extra));
  }
  void overwrite_descriptors()
  { MEMSET(row_->stored_cells(), 0xA5, row_->cnt_ * sizeof(StoredDatumCell)); }
  std::vector<char> memory_;
  Row *row_ = nullptr;
};

// Bind existing consumer/output boundaries without a production test hook.
template <typename Tag, typename Tag::Type Member>
struct PrivateMember { friend typename Tag::Type member(Tag) { return Member; } };
#define VECTOR_GET(Tag, Class, Method) \
  struct Tag { \
    using Type = int (Class::*)(const Row *, int64_t, int64_t, int64_t &, ObString &); \
    friend Type member(Tag); \
  }; \
  template struct PrivateMember<Tag, &Class::Method>
VECTOR_GET(DenseGet, ObVecIndexDMLIterator, get_vec_data);
VECTOR_GET(DenseUpdateGet, ObVecIndexDMLIterator, get_vec_data_for_update);
#undef VECTOR_GET
#define SPARSE_GET(Tag, Method) \
  struct Tag { \
    using Type = int (ObSparseVecIndexDMLIterator::*)(const Row *, int64_t, int64_t, ObDatum &, ObString &); \
    friend Type member(Tag); \
  }; \
  template struct PrivateMember<Tag, &ObSparseVecIndexDMLIterator::Method>
SPARSE_GET(SparseGet, get_sparse_vec_data);
SPARSE_GET(SparseUpdateGet, get_sparse_vec_data_for_update);
#undef SPARSE_GET
#define ID_GET(Tag, Class, Method) \
  struct Tag { using Type = int (Class::*)(const Row *, int64_t, int64_t &); friend Type member(Tag); }; \
  template struct PrivateMember<Tag, &Class::Method>
ID_GET(HybridGet, ObHybridVecLogDMLIterator, get_vec_id);
ID_GET(HybridUpdateGet, ObHybridVecLogDMLIterator, get_vec_data_for_update);
ID_GET(EmbeddedIdGet, ObEmbeddedVecDMLIterator, get_vid);
#undef ID_GET
#define OLD_ROW(Tag, Class) \
  struct Tag { using Type = bool Class::*; friend Type member(Tag); }; \
  template struct PrivateMember<Tag, &Class::is_old_row_>
OLD_ROW(DenseOldRow, ObVecIndexDMLIterator);
OLD_ROW(HybridOldRow, ObHybridVecLogDMLIterator);
#undef OLD_ROW
struct DenseOutput
{
  using Type = int (ObVecIndexDMLIterator::*)(ObIAllocator &, const Row *, int64_t, int64_t,
      int64_t, const int64_t &, ObString &, ObDomainIndexRow &);
  friend Type member(DenseOutput);
};
template struct PrivateMember<DenseOutput, &ObVecIndexDMLIterator::generate_vec_delta_buff_row>;
struct SparseOutput
{
  using Type = int (ObSparseVecIndexDMLIterator::*)(ObIAllocator &, const Row *, int64_t, int64_t,
      int64_t, int64_t, const ObDatum &, ObString &, ObDomainIndexRow &);
  friend Type member(SparseOutput);
};
template struct PrivateMember<SparseOutput, &ObSparseVecIndexDMLIterator::generate_sparse_vec_index_row>;
struct EmbeddedChunkGet
{
  using Type = int (ObEmbeddedVecDMLIterator::*)(const Row *, int64_t, ObString &);
  friend Type member(EmbeddedChunkGet);
};
template struct PrivateMember<EmbeddedChunkGet, &ObEmbeddedVecDMLIterator::get_chunk_data>;

struct CtDef final : ObDASDMLBaseCtDef
{
  CtDef(ObIAllocator &allocator, ObDASOpType operation) : ObDASDMLBaseCtDef(allocator, operation) {}
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

class FixedDmlVector : public ::testing::Test
{
protected:
  FixedDmlVector() : arena_(ObMemAttr("DmlVectorTest")), main_(arena_), projector_(arena_),
      lob_options_(lob_service_, INT64_MAX) {}
  void projection(IntFixedArray &target, const std::vector<int64_t> &indices)
  {
    ASSERT_EQ(OB_SUCCESS, target.init(indices.size()));
    for (auto index : indices) { ASSERT_EQ(OB_SUCCESS, target.push_back(index)); }
  }
  void dense_columns(CtDef &ctdef)
  {
    ASSERT_EQ(OB_SUCCESS, ctdef.column_types_.init(3));
    ASSERT_EQ(OB_SUCCESS, ctdef.column_accuracys_.init(3));
    for (auto type : {ObIntType, ObVarcharType, ObLongTextType}) {
      SqlTypeMeta meta; meta.set_type(type); meta.set_collation_type(CS_TYPE_BINARY);
      if (type == ObLongTextType) { meta.set_has_lob_header(); }
      ASSERT_EQ(OB_SUCCESS, ctdef.column_types_.push_back(meta));
      ASSERT_EQ(OB_SUCCESS, ctdef.column_accuracys_.push_back(ObAccuracy()));
    }
  }
  void lob(const std::string &value, ObString &output)
  {
    ASSERT_EQ(OB_SUCCESS, lob_helper::pack_to_disk_inrow_lob(arena_,
        ObString(value.size(), value.data()), output));
  }
  void sparse_payload(std::string &output)
  {
    ObSqlCollectionInfo info(arena_); info.set_name("SPARSEVECTOR");
    ASSERT_EQ(OB_SUCCESS, info.parse_type_info());
    ObIArrayType *array = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObArrayTypeObjFactory::construct(arena_, *info.collection_meta_, array, false));
    auto *map = static_cast<ObMapType *>(array);
    ASSERT_EQ(OB_SUCCESS, map->init());
    auto *keys = static_cast<ObArrayFixedSize<uint32_t> *>(map->get_key_array());
    auto *values = static_cast<ObArrayFixedSize<float> *>(map->get_value_array());
    ASSERT_EQ(OB_SUCCESS, keys->push_back(2)); ASSERT_EQ(OB_SUCCESS, values->push_back(1.5F));
    ASSERT_EQ(OB_SUCCESS, keys->push_back(19)); ASSERT_EQ(OB_SUCCESS, values->push_back(-4.0F));
    ASSERT_EQ(OB_SUCCESS, map->init());
    output.resize(map->get_raw_binary_len());
    ASSERT_EQ(OB_SUCCESS, map->get_raw_binary(&output[0], output.size()));
  }
  ObArenaAllocator arena_;
  ObDASInsCtDef main_;
  IntFixedArray projector_;
  ObDASWriteBuffer::Iterator write_iter_;
  RejectLobRead lob_service_;
  ObLobReadOptions lob_options_;
};

TEST_F(FixedDmlVector, dense_insert_getter_and_output_borrow_fixed_lob_payload)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_INSERT); dense_columns(ctdef); projection(projector_, {2, 0, 1});
  int64_t id = INT64_MAX; std::string payload("vec\0data", 8); ObString packed; lob(payload, packed);
  ObDatum flagged = text(packed); flagged.set_has_lob_header();
  OwnedRow source({null_value(), flagged, integer(id)});
  ObVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  int64_t output_id = 0; ObString vector;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(DenseGet{}))(source.row_, 0, 2, output_id, vector));
  EXPECT_EQ(INT64_MAX, output_id); EXPECT_EQ(payload, bytes(vector));
  ObDomainIndexRow output;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(DenseOutput{}))(arena_, source.row_, 0, 1, 2, output_id, vector, output));
  ASSERT_EQ(1, output.count()); const auto &cells = output.at(0)->storage_datums_;
  EXPECT_EQ(INT64_MAX, cells[0].get_int()); EXPECT_EQ("I", bytes(cells[1]));
  EXPECT_EQ(vector.ptr(), cells[2].ptr_); EXPECT_EQ(ObDatumDesc::NONE, cells[2].flag_);
  source.overwrite_descriptors(); id = 0; payload.assign(payload.size(), 'x');
  EXPECT_EQ(std::string("vec\0data", 8), bytes(cells[2])); EXPECT_EQ(0, lob_service_.calls_);
}

TEST_F(FixedDmlVector, dense_delete_skips_reading_malformed_lob_and_keeps_delete_output)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_DELETE); dense_columns(ctdef); projection(projector_, {1, 2, 0});
  int64_t id = 91; std::string malformed = "delete-does-not-read-lob";
  OwnedRow source({text(malformed), integer(id), null_value()});
  // A missing LOB options pointer makes any accidental read immediately visible.
  ObVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, nullptr);
  int64_t output_id = 0; ObString vector;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(DenseGet{}))(source.row_, 0, 2, output_id, vector));
  EXPECT_EQ(91, output_id); EXPECT_EQ(malformed, bytes(vector));
  ObDomainIndexRow output;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(DenseOutput{}))(arena_, source.row_, 0, 1, 2, output_id, vector, output));
  ASSERT_EQ(1, output.count()); EXPECT_EQ("D", bytes(output.at(0)->storage_datums_[1]));
  source.overwrite_descriptors(); EXPECT_EQ(malformed, bytes(output.at(0)->storage_datums_[2]));
}

TEST_F(FixedDmlVector, dense_update_getter_selects_old_and_new_projectors)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_UPDATE); projection(projector_, {0, 1});
  projection(ctdef.old_row_projector_, {2, 1}); projection(ctdef.new_row_projector_, {3, 0});
  int64_t old_id = INT64_MIN, new_id = INT64_MAX;
  std::string old_value = "old", new_value = "new"; ObString old_lob, new_lob;
  lob(old_value, old_lob); lob(new_value, new_lob);
  OwnedRow source({text(new_lob), text(old_lob), integer(old_id), integer(new_id)});
  ObVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, nullptr, &lob_options_);
  int64_t id = 0; ObString old_vector, new_vector;
  ASSERT_TRUE(iter.*member(DenseOldRow{}));
  ASSERT_EQ(OB_SUCCESS, (iter.*member(DenseUpdateGet{}))(source.row_, 0, 1, id, old_vector));
  EXPECT_EQ(INT64_MIN, id); EXPECT_EQ(old_value, bytes(old_vector));
  iter.*member(DenseOldRow{}) = false;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(DenseUpdateGet{}))(source.row_, 0, 1, id, new_vector));
  source.overwrite_descriptors();
  EXPECT_EQ(INT64_MAX, id); EXPECT_EQ(new_value, bytes(new_vector)); EXPECT_EQ(old_value, bytes(old_vector));
  EXPECT_EQ(0, lob_service_.calls_);
}

TEST_F(FixedDmlVector, dense_projection_errors_leave_getter_outputs_unchanged)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_UPDATE); projection(projector_, {0});
  projection(ctdef.old_row_projector_, {0}); projection(ctdef.new_row_projector_, {0});
  int64_t id = 7; std::string text_value = "sentinel"; ObString vector(text_value.size(), text_value.data());
  OwnedRow source({integer(id)});
  ObVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, nullptr, &lob_options_);
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(DenseGet{}))(source.row_, 0, 1, id, vector));
  EXPECT_EQ(7, id); EXPECT_EQ(text_value, bytes(vector));
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(DenseUpdateGet{}))(source.row_, 0, 1, id, vector));
  EXPECT_EQ(7, id); EXPECT_EQ(text_value, bytes(vector));
}

TEST_F(FixedDmlVector, sparse_getter_copies_numeric_doc_descriptor_and_preserves_flags)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_INSERT); projection(projector_, {2, 0});
  int64_t id = INT64_MIN; std::string payload = "sparse"; ObString packed; lob(payload, packed);
  ObDatum flagged = integer(id); flagged.set_has_lob_header();
  OwnedRow source({text(packed), null_value(), flagged});
  const auto pack = source.row_->stored_cells()[2].pack_; const char *owner = source.row_->stored_cells()[2].ptr_;
  ObSparseVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  ObDatum doc; ObString vector;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(SparseGet{}))(source.row_, 0, 1, doc, vector));
  source.overwrite_descriptors();
  EXPECT_EQ(INT64_MIN, doc.get_int()); EXPECT_EQ(pack, doc.pack_); EXPECT_EQ(owner, doc.ptr_);
  EXPECT_EQ(payload, bytes(vector)); EXPECT_EQ(0, lob_service_.calls_);
}

TEST_F(FixedDmlVector, sparse_update_uses_current_projector_for_string_and_null_docids)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_UPDATE); projection(projector_, {0, 1});
  projection(ctdef.old_row_projector_, {2, 1}); projection(ctdef.new_row_projector_, {3, 0});
  std::string id("tablet\0seq", 10), old_value = "old", new_value = "new";
  ObString old_lob, new_lob; lob(old_value, old_lob); lob(new_value, new_lob);
  ObDatum flagged = text(id); flagged.set_has_lob_header();
  OwnedRow source({text(new_lob), text(old_lob), flagged, null_value()});
  const char *owner = source.row_->stored_cells()[2].ptr_; const auto pack = source.row_->stored_cells()[2].pack_;
  ObSparseVecIndexDMLIterator iter(arena_, &ctdef.old_row_projector_, write_iter_, &ctdef, nullptr, &lob_options_);
  ObDatum old_doc, new_doc; ObString old_vector, new_vector;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(SparseUpdateGet{}))(source.row_, 0, 1, old_doc, old_vector));
  iter.set_row_projector(&ctdef.new_row_projector_);
  ASSERT_EQ(OB_SUCCESS, (iter.*member(SparseUpdateGet{}))(source.row_, 0, 1, new_doc, new_vector));
  source.overwrite_descriptors();
  EXPECT_EQ(id, bytes(old_doc)); EXPECT_EQ(owner, old_doc.ptr_); EXPECT_EQ(pack, old_doc.pack_);
  EXPECT_TRUE(new_doc.is_null()); EXPECT_EQ(old_value, bytes(old_vector)); EXPECT_EQ(new_value, bytes(new_vector));
}

TEST_F(FixedDmlVector, sparse_getter_to_output_borrows_docid_and_owns_dimension_scalars)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_INSERT); projection(projector_, {1, 0});
  std::string raw; sparse_payload(raw); ObString packed; lob(raw, packed);
  std::string id("tablet\0seq", 10); ObDatum flagged = text(id); flagged.set_has_lob_header();
  OwnedRow source({text(packed), flagged});
  const char *owner = source.row_->stored_cells()[1].ptr_; const auto pack = source.row_->stored_cells()[1].pack_;
  ObSparseVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  ObDatum doc; ObString vector;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(SparseGet{}))(source.row_, 0, 1, doc, vector));
  ObDomainIndexRow output;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(SparseOutput{}))(arena_, source.row_, 0, 1, 2, 3, doc, vector, output));
  ASSERT_EQ(2, output.count());
  source.overwrite_descriptors();
  // Output dimension/value scalars are copied; docid intentionally borrows the source payload owner.
  MEMSET(vector.ptr(), 0xA5, vector.length());
  for (int64_t i = 0; i < 2; ++i) {
    const auto &cells = output.at(i)->storage_datums_;
    EXPECT_EQ(i == 0 ? 2 : 19, cells[0].get_uint32()); EXPECT_FLOAT_EQ(i == 0 ? 1.5F : -4.0F, cells[2].get_float());
    EXPECT_EQ(owner, cells[1].ptr_); EXPECT_EQ(pack, cells[1].pack_); EXPECT_EQ(id, bytes(cells[1]));
    EXPECT_TRUE(cells[3].is_nop());
  }
}

TEST_F(FixedDmlVector, sparse_output_preserves_null_docid_and_first_decode_error_publishes_nothing)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_INSERT); projection(projector_, {0, 1});
  ObSparseVecIndexDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  OwnedRow source({null_value(), null_value()}); ObDatum doc = null_value();
  std::string raw; sparse_payload(raw); ObString vector(raw.size(), raw.data()); ObDomainIndexRow output;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(SparseOutput{}))(arena_, source.row_, 0, 1, 2, 3, doc, vector, output));
  ASSERT_EQ(2, output.count()); EXPECT_TRUE(output.at(0)->storage_datums_[1].is_null());
  EXPECT_TRUE(output.at(1)->storage_datums_[1].is_null());
  std::string truncated(1, '\0'); vector.assign_ptr(&truncated[0], truncated.size()); ObDomainIndexRow failed;
  EXPECT_NE(OB_SUCCESS, (iter.*member(SparseOutput{}))(arena_, source.row_, 0, 1, 2, 3, doc, vector, failed));
  EXPECT_EQ(0, failed.count());
}

TEST_F(FixedDmlVector, hybrid_getter_keeps_new_only_and_old_new_selection_distinct)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_UPDATE); projection(projector_, {0});
  projection(ctdef.old_row_projector_, {1}); projection(ctdef.new_row_projector_, {2});
  int64_t unused = 9, old_id = INT64_MIN, new_id = INT64_MAX;
  OwnedRow source({integer(unused), integer(old_id), integer(new_id)});
  ObHybridVecLogDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, nullptr, &lob_options_);
  int64_t id = 0; ASSERT_TRUE(iter.*member(HybridOldRow{}));
  ASSERT_EQ(OB_SUCCESS, (iter.*member(HybridUpdateGet{}))(source.row_, 0, id)); EXPECT_EQ(INT64_MIN, id);
  ASSERT_EQ(OB_SUCCESS, (iter.*member(HybridGet{}))(source.row_, 0, id)); EXPECT_EQ(INT64_MAX, id);
  iter.*member(HybridOldRow{}) = false;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(HybridUpdateGet{}))(source.row_, 0, id)); EXPECT_EQ(INT64_MAX, id);
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(HybridUpdateGet{}))(source.row_, 1, id)); EXPECT_EQ(INT64_MAX, id);
}

TEST_F(FixedDmlVector, hybrid_nonupdate_getter_reads_projected_fixed_id)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_DELETE); projection(projector_, {1});
  int64_t id = INT64_MIN, ignored = 2; OwnedRow source({integer(ignored), integer(id)});
  ObHybridVecLogDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  int64_t output = 7;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(HybridGet{}))(source.row_, 0, output)); EXPECT_EQ(INT64_MIN, output);
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(HybridGet{}))(source.row_, 1, output)); EXPECT_EQ(INT64_MIN, output);
}

TEST_F(FixedDmlVector, embedded_getters_keep_projected_payload_live_after_descriptor_overwrite)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_INSERT); projection(projector_, {1, 0});
  int64_t id = INT64_MAX; std::string payload("chunk\0text", 10); ObString packed; lob(payload, packed);
  OwnedRow source({text(packed), integer(id)});
  ObEmbeddedVecDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  int64_t output_id = 0; ObString chunk;
  ASSERT_EQ(OB_SUCCESS, (iter.*member(EmbeddedIdGet{}))(source.row_, 0, output_id));
  ASSERT_EQ(OB_SUCCESS, (iter.*member(EmbeddedChunkGet{}))(source.row_, 1, chunk));
  source.overwrite_descriptors();
  EXPECT_EQ(INT64_MAX, output_id); EXPECT_EQ(payload, bytes(chunk)); EXPECT_EQ(0, lob_service_.calls_);
}

TEST_F(FixedDmlVector, embedded_null_chunk_and_invalid_indices_keep_existing_reset_contract)
{
  CtDef ctdef(arena_, DAS_OP_TABLE_DELETE); projection(projector_, {0, 1});
  int64_t id = 7; OwnedRow source({integer(id), null_value()});
  ObEmbeddedVecDMLIterator iter(arena_, &projector_, write_iter_, &ctdef, &main_, &lob_options_);
  EXPECT_TRUE(iter.is_old_row_);
  ObString chunk; ASSERT_EQ(OB_SUCCESS, (iter.*member(EmbeddedChunkGet{}))(source.row_, 1, chunk)); EXPECT_TRUE(chunk.empty());
  EXPECT_EQ(OB_ERR_UNEXPECTED, (iter.*member(EmbeddedIdGet{}))(source.row_, OB_INVALID_INDEX, id));
  EXPECT_EQ(OB_INVALID_ID, static_cast<uint64_t>(id));
  std::string sentinel = "sentinel"; chunk.assign_ptr(sentinel.data(), sentinel.size());
  EXPECT_EQ(OB_INVALID_ARGUMENT, (iter.*member(EmbeddedChunkGet{}))(source.row_, 2, chunk)); EXPECT_TRUE(chunk.empty());
  EXPECT_EQ(0, lob_service_.calls_);
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (OB_SUCCESS != ObCharset::init_charset()) { return 1; }
  return RUN_ALL_TESTS();
}
