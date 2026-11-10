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

#include "sql/das/ob_das_utils.h"
#include "sql/engine/dml/ob_dml_service.h"
#include "sql/das/ob_das_dml_ctx_define.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "data_plane/blocksstable/ob_datum_row.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using oceanbase::blocksstable::ObDatumRow;

namespace
{
using FixedRow = ObDASWriteBuffer::DmlRow;
using ShadowRow = ObDASWriteBuffer::DmlShadowRow;

SqlTypeMeta type(SqlValueType value, ObCollationType collation = CS_TYPE_BINARY)
{
  SqlTypeMeta result; result.set_type(value); result.set_collation_type(collation);
  return result;
}
ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum text(const std::string &value)
{ return ObDatum(value.data(), value.size(), false); }
ObDatum null_value()
{ ObDatum result; result.set_null(); return result; }
int64_t read_integer(const ObDatum &datum)
{
  int64_t result = 0;
  EXPECT_FALSE(datum.is_null()); EXPECT_EQ(sizeof(result), datum.len_);
  if (!datum.is_null() && datum.len_ == sizeof(result)) { MEMCPY(&result, datum.ptr_, sizeof(result)); }
  return result;
}
std::string read_text(const ObDatum &datum)
{ return std::string(datum.ptr_, datum.len_); }
ObDatum decoded(const FixedRow &row, int64_t column)
{ return StoredDatumCodec::decode_legacy_view(row.stored_cells()[column]); }

struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values)
  {
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), 0);
    memory_.resize(size);
    row_ = new (memory_.data()) FixedRow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(FixedRow), size, 0));
  }
  void relocate()
  {
    row_->unswizzling(row_->payload_);
    std::vector<char> moved(memory_);
    MEMSET(memory_.data(), 0xA5, memory_.size());
    memory_.swap(moved);
    row_ = reinterpret_cast<FixedRow *>(memory_.data());
    row_->swizzling(row_->payload_);
  }
  std::vector<char> memory_;
  FixedRow *row_ = nullptr;
};

class RejectAllocator final : public ObIAllocator
{
public:
  void *alloc(int64_t) override { ++calls_; return nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}
  int64_t calls_ = 0;
};

class FixedDmlCore : public ::testing::Test
{
protected:
  static constexpr int64_t COLS = 4;
  static constexpr int64_t BATCH = 2;
  FixedDmlCore() : arena_("FixedDmlCore"), exec_(arena_), session_(),
      frame_{}, frames_{frame_.data()}, eval_(exec_), batch_(eval_), expressions_{} {}
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    exec_.set_my_session(&session_);
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH);
    batch_.set_batch_idx(1);
    for (int64_t col = 0; col < COLS; ++col) {
      ObExpr &expr = expressions_[col];
      expr.type_ = T_REF_COLUMN;
      expr.frame_idx_ = 0;
      expr.datum_off_ = col * 512;
      expr.eval_info_off_ = col * 512 + 64;
      expr.eval_flags_off_ = col * 512 + 96;
      expr.res_buf_off_ = col * 512 + 128;
      expr.res_buf_len_ = 128;
      expr.batch_result_ = true;
      expr.batch_idx_mask_ = UINT64_MAX;
      expr.obj_meta_.set_int();
      expr.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0);
      expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
      for (int64_t row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      expr.get_eval_info(eval_).evaluated_ = true;
      expr.get_evaluated_flags(eval_).set_all(BATCH);
      ASSERT_EQ(OB_SUCCESS, exprs_.push_back(&expr));
    }
  }
  Datum &datum(int64_t col, int64_t row = 1)
  { return expressions_[col].locate_batch_datums(eval_)[row]; }
  void set_datum(int64_t col, const ObDatum &value, int64_t row = 1)
  { ASSERT_EQ(OB_SUCCESS, expressions_[col].get_datum_copy_plan().import_legacy(value, datum(col, row))); }

  void columns(ObSEArray<SqlTypeMeta, 4> &types, const std::vector<SqlTypeMeta> &values)
  { for (const auto &value : values) { ASSERT_EQ(OB_SUCCESS, types.push_back(value)); } }
  void projection(ObDASInsCtDef &ctdef, const std::vector<SqlTypeMeta> &types,
                  const std::vector<int32_t> &indexes)
  {
    ASSERT_EQ(types.size(), indexes.size());
    ASSERT_EQ(OB_SUCCESS, ctdef.column_types_.init(types.size()));
    ASSERT_EQ(OB_SUCCESS, ctdef.column_accuracys_.init(types.size()));
    ASSERT_EQ(OB_SUCCESS, ctdef.new_row_projector_.init(indexes.size()));
    for (size_t i = 0; i < types.size(); ++i) {
      ObAccuracy accuracy; accuracy.set_length(5);
      ASSERT_EQ(OB_SUCCESS, ctdef.column_types_.push_back(types[i]));
      ASSERT_EQ(OB_SUCCESS, ctdef.column_accuracys_.push_back(accuracy));
      ASSERT_EQ(OB_SUCCESS, ctdef.new_row_projector_.push_back(indexes[i]));
    }
  }
  static int fail_eval(const ObExpr &, ObEvalCtx &, Datum &) { return OB_TIMEOUT; }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObSQLSessionInfo session_;
  alignas(16) std::array<char, COLS * 512> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExpr expressions_[COLS];
  ObSEArray<ObExpr *, COLS> exprs_;
};

TEST_F(FixedDmlCore, ignored_null_integer_becomes_inline_zero_without_result_payload)
{
  ObExpr &expr = expressions_[0];
  expr.res_buf_len_ = 0;
  expr.res_buf_off_ = 0;
  datum(0, 0).set_int(INT64_MIN);
  datum(0).set_null();
  ColumnContent column;
  column.column_name_ = ObString::make_string("i");
  const auto untouched_batch_row = datum(0, 0);
  ASSERT_EQ(OB_SUCCESS, ObDMLService::check_column_null(
      eval_, exprs_, column, datum(0), true, true, 0));
  ASSERT_FALSE(datum(0).is_null());
  EXPECT_EQ(0, datum(0).get_int());
  int64_t payload_bytes = -1;
  ASSERT_EQ(OB_SUCCESS, expr.get_datum_copy_plan().payload_copy_size(datum(0), payload_bytes));
  EXPECT_EQ(0, payload_bytes);
  EXPECT_EQ(0, expr.res_buf_len_);
  EXPECT_EQ(0, MEMCMP(&untouched_batch_row, &datum(0, 0), sizeof(Datum)));

  // The other supported correction path is non-strict, multi-row DML.
  datum(0).set_null();
  ASSERT_EQ(OB_SUCCESS, ObDMLService::check_column_null(
      eval_, exprs_, column, datum(0), false, false, 0));
  EXPECT_EQ(0, datum(0).get_int());
  datum(0).set_null();
  EXPECT_EQ(OB_BAD_NULL_ERROR, ObDMLService::check_column_null(
      eval_, exprs_, column, datum(0), false, true, 0));
  EXPECT_TRUE(datum(0).is_null());
}

TEST_F(FixedDmlCore, ignored_null_binary_padding_uses_expression_owned_memory)
{
  ObExpr &expr = expressions_[0];
  expr.obj_meta_.set_binary();
  expr.datum_meta_ = ObDatumMeta(ObCharType, CS_TYPE_BINARY, 0);
  expr.obj_datum_map_ = OBJ_DATUM_STRING;
  expr.max_length_ = 19;
  ObAccuracy accuracy; accuracy.set_length(expr.max_length_);
  ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, accuracy, arena_));
  datum(0).set_null();
  ColumnContent column;
  column.column_name_ = ObString::make_string("b");
  ASSERT_EQ(OB_SUCCESS, ObDMLService::check_column_null(
      eval_, exprs_, column, datum(0), true, true, 0));
  EXPECT_EQ(std::string(19, '\0'), std::string(datum(0).get_string().ptr(), datum(0).length()));
  EXPECT_EQ(frame_.data() + expr.res_buf_off_ + expr.res_buf_len_, datum(0).get_string().ptr());
}

TEST_F(FixedDmlCore, ignored_null_number_owns_zero_bytes_after_legacy_scratch_expires)
{
  ObExpr &expr = expressions_[0];
  expr.obj_meta_.set_number();
  expr.datum_meta_ = ObDatumMeta(ObNumberType, CS_TYPE_BINARY, 0);
  expr.obj_datum_map_ = OBJ_DATUM_NUMBER;
  ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
  datum(0).set_null();
  ColumnContent column;
  column.column_name_ = ObString::make_string("n");
  ASSERT_EQ(OB_SUCCESS, ObDMLService::check_column_null(
      eval_, exprs_, column, datum(0), true, true, 0));
  EXPECT_EQ(frame_.data() + expr.res_buf_off_ + expr.res_buf_len_, datum(0).get_string().ptr());
  EXPECT_TRUE(datum(0).get_number().is_zero());
  // Another correction reuses the helper's temporary stack, not this owner.
  datum(1).set_null(); column.projector_index_ = 1;
  ASSERT_EQ(OB_SUCCESS, ObDMLService::check_column_null(
      eval_, exprs_, column, datum(1), true, true, 0));
  EXPECT_TRUE(datum(0).get_number().is_zero());
}

TEST_F(FixedDmlCore, storage_shadow_initializes_and_preserves_reserved_numeric_buffers)
{
  ObSEArray<SqlTypeMeta, 4> types;
  SqlTypeMeta decimal; decimal.set_decimal_int(3);
  columns(types, {type(ObIntType), decimal, type(ObVarcharType), type(ObNullType)});
  ShadowRow shadow; ASSERT_EQ(OB_SUCCESS, shadow.init(arena_, types, false));
  FixedRow *row = shadow.get_store_row(); ASSERT_NE(nullptr, row);
  std::array<const char *, COLS> reserved;
  for (int64_t i = 0; i < COLS; ++i) {
    reserved[i] = row->stored_cells()[i].ptr_;
    EXPECT_EQ(0, row->stored_cells()[i].pack_);
  }
  ASSERT_NE(nullptr, reserved[0]); ASSERT_NE(nullptr, reserved[1]);
  EXPECT_NE(reserved[0], reserved[1]); EXPECT_EQ(nullptr, reserved[3]);
  std::array<unsigned char, 64> coefficient;
  for (size_t i = 0; i < coefficient.size(); ++i) { coefficient[i] = static_cast<unsigned char>(i + 1); }
  const auto expected_coefficient = coefficient;
  std::string string_value("a\0b", 3);
  ObDatumRow source; ASSERT_EQ(OB_SUCCESS, source.init(arena_, COLS));
  source.storage_datums_[0].set_int(INT64_MIN);
  source.storage_datums_[1].set_decimal_int_shallow(
      reinterpret_cast<const ObDecimalInt *>(coefficient.data()), coefficient.size());
  source.storage_datums_[2].set_string(ObString(string_value.size(), string_value.data()));
  source.storage_datums_[2].set_has_lob_header();
  source.storage_datums_[3].set_null();
  ASSERT_EQ(OB_SUCCESS, shadow.shadow_copy(source)); // Deliberately no reuse after init.
  EXPECT_EQ(reserved[0], row->stored_cells()[0].ptr_);
  EXPECT_EQ(reserved[1], row->stored_cells()[1].ptr_);
  EXPECT_EQ(string_value.data(), row->stored_cells()[2].ptr_);
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, row->stored_cells()[2].flag_);
  EXPECT_TRUE(row->stored_cells()[3].is_null());
  EXPECT_EQ(sizeof(FixedRow) + COLS * sizeof(StoredDatumCell) + 8 + 64 + 3, row->row_size_);
  source.storage_datums_[0].set_int(0); coefficient.fill(0);
  EXPECT_EQ(INT64_MIN, read_integer(decoded(*row, 0)));
  EXPECT_EQ(0, MEMCMP(expected_coefficient.data(), row->stored_cells()[1].ptr_, 64));
  shadow.reuse();
  for (int64_t i = 0; i < COLS; ++i) { EXPECT_EQ(reserved[i], row->stored_cells()[i].ptr_); }
  source.storage_datums_[0].set_null();
  source.storage_datums_[1].set_decimal_int_shallow(
      reinterpret_cast<const ObDecimalInt *>(coefficient.data()), 0);
  ASSERT_EQ(OB_SUCCESS, shadow.shadow_copy(source));
  EXPECT_TRUE(row->stored_cells()[0].is_null());
  EXPECT_FALSE(row->stored_cells()[1].is_null()); EXPECT_EQ(0, row->stored_cells()[1].len_);
  shadow.reuse(); source.storage_datums_[0].set_int(INT64_MAX);
  ASSERT_EQ(OB_SUCCESS, shadow.shadow_copy(source));
  EXPECT_EQ(reserved[0], row->stored_cells()[0].ptr_);
  EXPECT_EQ(INT64_MAX, read_integer(decoded(*row, 0)));
}

TEST_F(FixedDmlCore, storage_nop_keeps_the_existing_completed_prefix_and_reuse_recovers)
{
  ObSEArray<SqlTypeMeta, 4> types;
  columns(types, {type(ObIntType), type(ObIntType), type(ObIntType)});
  ShadowRow shadow; ASSERT_EQ(OB_SUCCESS, shadow.init(arena_, types, false));
  FixedRow *row = shadow.get_store_row();
  const char *second_reserved = row->stored_cells()[1].ptr_;
  ObDatumRow source; ASSERT_EQ(OB_SUCCESS, source.init(arena_, 3));
  source.storage_datums_[0].set_int(17); source.storage_datums_[1].set_nop();
  source.storage_datums_[2].set_int(29);
  EXPECT_EQ(OB_INVALID_ARGUMENT, shadow.shadow_copy(source));
  EXPECT_EQ(17, read_integer(decoded(*row, 0)));
  EXPECT_EQ(second_reserved, row->stored_cells()[1].ptr_);
  EXPECT_EQ(0, row->stored_cells()[1].pack_); EXPECT_EQ(0, row->stored_cells()[2].pack_);
  EXPECT_EQ(sizeof(FixedRow) + 3 * sizeof(StoredDatumCell) + 8, row->row_size_);
  // saved_ and the completed prefix are existing per-column semantics.
  EXPECT_TRUE(shadow.is_saved());
  const auto before = std::vector<char>(reinterpret_cast<char *>(row),
      reinterpret_cast<char *>(row) + sizeof(FixedRow) + 3 * sizeof(StoredDatumCell));
  ObDatumRow wrong_count; ASSERT_EQ(OB_SUCCESS, wrong_count.init(arena_, 2));
  EXPECT_EQ(OB_ERR_UNEXPECTED, shadow.shadow_copy(wrong_count));
  EXPECT_EQ(0, MEMCMP(before.data(), row, before.size()));
  shadow.reuse(); source.storage_datums_[1].set_int(23);
  ASSERT_EQ(OB_SUCCESS, shadow.shadow_copy(source));
  EXPECT_EQ(17, read_integer(decoded(*row, 0)));
  EXPECT_EQ(23, read_integer(decoded(*row, 1)));
  EXPECT_EQ(29, read_integer(decoded(*row, 2)));
  EXPECT_EQ(sizeof(FixedRow) + 3 * sizeof(StoredDatumCell) + 24, row->row_size_);
}

TEST_F(FixedDmlCore, expression_shadow_uses_batch_index_and_nonbatch_slot_then_buffer_owns_bytes)
{
  int64_t ignored = 91, value = INT64_MAX, constant = INT64_MIN;
  std::string string_value("s\0t", 3);
  expressions_[1].obj_meta_.set_varchar();
  expressions_[1].obj_meta_.set_collation_type(CS_TYPE_BINARY);
  expressions_[1].obj_datum_map_ = OBJ_DATUM_STRING;
  ASSERT_EQ(OB_SUCCESS, expressions_[1].prepare_scalar_type(expressions_[1].obj_meta_, ObAccuracy(), arena_));
  for (int64_t i = 0; i < COLS; ++i) { if (i != 1) { set_datum(i, integer(ignored), 0); } }
  set_datum(1, text(string_value), 0);
  set_datum(0, integer(value)); set_datum(1, text(string_value)); datum(1).set_has_lob_header();
  set_datum(2, integer(constant), 0); datum(3).set_null();
  expressions_[2].batch_result_ = false; expressions_[2].batch_idx_mask_ = 0;
  ShadowRow shadow; ASSERT_EQ(OB_SUCCESS, shadow.init(arena_, COLS, false));
  ASSERT_EQ(OB_SUCCESS, shadow.shadow_copy(exprs_, eval_));
  FixedRow *borrowed = shadow.get_store_row();
  EXPECT_EQ(value, read_integer(decoded(*borrowed, 0)));
  EXPECT_NE(reinterpret_cast<const char *>(&value), borrowed->stored_cells()[0].ptr_);
  EXPECT_EQ(string_value.data(), borrowed->stored_cells()[1].ptr_);
  EXPECT_EQ(INT64_MIN, read_integer(decoded(*borrowed, 2)));
  EXPECT_TRUE(borrowed->stored_cells()[3].is_null());
  ObDASWriteBuffer buffer; ASSERT_EQ(OB_SUCCESS, buffer.init(arena_, 0, "FixedDmlCore"));
  FixedRow *owned = nullptr; ASSERT_EQ(OB_SUCCESS, buffer.add_row(shadow, &owned));
  ASSERT_NE(nullptr, owned);
  value = constant = 0; string_value.assign(3, 'x'); shadow.reuse();
  for (int64_t i = 0; i < COLS; ++i) { datum(i).set_null(); }
  EXPECT_EQ(INT64_MAX, read_integer(decoded(*owned, 0)));
  EXPECT_EQ(std::string("s\0t", 3), read_text(decoded(*owned, 1)));
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, owned->stored_cells()[1].flag_);
  EXPECT_EQ(INT64_MIN, read_integer(decoded(*owned, 2))); EXPECT_TRUE(owned->stored_cells()[3].is_null());
}

TEST_F(FixedDmlCore, expression_failure_does_not_append_and_next_call_reuses_shadow)
{
  int64_t values[COLS] = {11, 22, 33, 44};
  for (int64_t i = 0; i < COLS; ++i) { set_datum(i, integer(values[i])); }
  expressions_[2].batch_result_ = false; expressions_[2].batch_idx_mask_ = 0;
  expressions_[2].eval_func_ = fail_eval;
  expressions_[2].get_eval_info(eval_).evaluated_ = false;
  ObDASWriteBuffer buffer; ASSERT_EQ(OB_SUCCESS, buffer.init(arena_, 0, "FixedDmlCore"));
  FixedRow sentinel; FixedRow *output = &sentinel;
  EXPECT_EQ(OB_TIMEOUT, buffer.add_row(exprs_, &eval_, output, false));
  EXPECT_EQ(&sentinel, output); EXPECT_EQ(0, buffer.get_row_cnt());
  expressions_[2].get_eval_info(eval_).evaluated_ = true;
  set_datum(2, integer(values[2]), 0);
  ASSERT_EQ(OB_SUCCESS, buffer.add_row(exprs_, &eval_, output, false));
  ASSERT_NE(&sentinel, output); EXPECT_EQ(1, buffer.get_row_cnt());
  EXPECT_EQ(sizeof(FixedRow) + COLS * sizeof(StoredDatumCell) + COLS * sizeof(int64_t), output->row_size_);
  for (int64_t i = 0; i < COLS; ++i) { EXPECT_EQ(values[i], read_integer(decoded(*output, i))); }
}

TEST_F(FixedDmlCore, projection_preserves_mapping_null_nop_flags_and_wide_decimal_borrow)
{
  int64_t value = INT64_MIN;
  std::string string_value("x\0y", 3);
  std::array<unsigned char, 64> coefficient; coefficient.fill(0xAB);
  ObDatum decimal; decimal.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(coefficient.data()), 64);
  ObDatum flagged = text(string_value); flagged.set_has_lob_header();
  OwnedRow source({integer(value), decimal, flagged, null_value()}); source.relocate();
  const auto original = source.memory_;
  SqlTypeMeta dec_meta; dec_meta.set_decimal_int(7);
  ObDASInsCtDef ctdef(arena_);
  projection(ctdef, {type(ObVarcharType), type(ObIntType), type(ObNullType), type(ObIntType),
      type(ObVarcharType), dec_meta}, {2, 0, 3, -1, 2, 1});
  ObDatumRow result; ASSERT_EQ(OB_SUCCESS, result.init(arena_, 6));
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::project_storage_row(ctdef, *source.row_,
      ctdef.new_row_projector_, arena_, result));
  EXPECT_EQ(std::string("x\0y", 3), read_text(result.storage_datums_[0]));
  EXPECT_EQ(INT64_MIN, read_integer(result.storage_datums_[1]));
  EXPECT_TRUE(result.storage_datums_[2].is_null()); EXPECT_TRUE(result.storage_datums_[3].is_nop());
  EXPECT_EQ(source.row_->stored_cells()[2].ptr_, result.storage_datums_[0].ptr_);
  EXPECT_EQ(result.storage_datums_[0].ptr_, result.storage_datums_[4].ptr_);
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, result.storage_datums_[4].flag_);
  EXPECT_EQ(source.row_->stored_cells()[1].ptr_, result.storage_datums_[5].ptr_);
  EXPECT_EQ(64, result.storage_datums_[5].len_);
  EXPECT_EQ(0, MEMCMP(coefficient.data(), result.storage_datums_[5].ptr_, 64));
  EXPECT_EQ(original, source.memory_); EXPECT_EQ(7, ctdef.column_types_.at(5).get_scale());
}

TEST_F(FixedDmlCore, projection_reshapes_only_output_and_retains_lob_schema_flags)
{
  std::string binary = "ab", padded_char = "xy  ", lob = "lob";
  OwnedRow source({text(binary), text(padded_char), text(lob)});
  const auto original = source.memory_;
  SqlTypeMeta bin_meta; bin_meta.set_binary();
  SqlTypeMeta lob_meta = type(ObLongTextType); lob_meta.set_has_lob_header();
  ObDASInsCtDef ctdef(arena_);
  projection(ctdef, {bin_meta, type(ObCharType, CS_TYPE_UTF8MB4_BIN), lob_meta}, {0, 1, 2});
  ObDatumRow result; ASSERT_EQ(OB_SUCCESS, result.init(arena_, 3));
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::project_storage_row(ctdef, *source.row_,
      ctdef.new_row_projector_, arena_, result));
  EXPECT_EQ(std::string("ab\0\0\0", 5), read_text(result.storage_datums_[0]));
  EXPECT_NE(source.row_->stored_cells()[0].ptr_, result.storage_datums_[0].ptr_);
  EXPECT_EQ("xy", read_text(result.storage_datums_[1]));
  EXPECT_EQ(source.row_->stored_cells()[1].ptr_, result.storage_datums_[1].ptr_);
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, result.storage_datums_[2].flag_);
  EXPECT_EQ(original, source.memory_);
}

TEST_F(FixedDmlCore, reshape_failure_stops_later_columns_and_negative_projector_skips_reshape)
{
  std::string binary = "ab"; int64_t value = 19;
  OwnedRow source({text(binary), integer(value)}); const auto original = source.memory_;
  SqlTypeMeta bin_meta; bin_meta.set_binary();
  ObDASInsCtDef ctdef(arena_); projection(ctdef, {bin_meta, type(ObIntType)}, {0, 1});
  ObDatumRow result; ASSERT_EQ(OB_SUCCESS, result.init(arena_, 2));
  result.storage_datums_[1].set_int(77);
  RejectAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObDASUtils::project_storage_row(ctdef, *source.row_,
      ctdef.new_row_projector_, reject, result));
  EXPECT_EQ(1, reject.calls_); EXPECT_EQ(77, read_integer(result.storage_datums_[1]));
  EXPECT_EQ("ab", read_text(result.storage_datums_[0])); EXPECT_EQ(original, source.memory_);
  ctdef.new_row_projector_.at(0) = -1;
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::project_storage_row(ctdef, *source.row_,
      ctdef.new_row_projector_, reject, result));
  EXPECT_EQ(1, reject.calls_); EXPECT_TRUE(result.storage_datums_[0].is_nop());
  EXPECT_EQ(19, read_integer(result.storage_datums_[1])); EXPECT_EQ(original, source.memory_);
}

TEST_F(FixedDmlCore, unique_index_shadow_primary_key_is_retained_only_for_null_index_key)
{
  int64_t index = 7, primary = INT64_MAX;
  OwnedRow nonnull({integer(index), integer(primary)}), nullable({null_value(), integer(primary)});
  ObDASInsCtDef ctdef(arena_); projection(ctdef, {type(ObIntType), type(ObIntType)}, {0, 1});
  ctdef.rowkey_cnt_ = 2; ctdef.spk_cnt_ = 1;
  ObDatumRow result; ASSERT_EQ(OB_SUCCESS, result.init(arena_, 2));
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::project_storage_row(ctdef, *nonnull.row_,
      ctdef.new_row_projector_, arena_, result));
  EXPECT_EQ(7, read_integer(result.storage_datums_[0])); EXPECT_TRUE(result.storage_datums_[1].is_null());
  EXPECT_EQ(INT64_MAX, read_integer(decoded(*nonnull.row_, 1)));
  ASSERT_EQ(OB_SUCCESS, ObDASUtils::project_storage_row(ctdef, *nullable.row_,
      ctdef.new_row_projector_, arena_, result));
  EXPECT_TRUE(result.storage_datums_[0].is_null());
  EXPECT_EQ(INT64_MAX, read_integer(result.storage_datums_[1]));
  EXPECT_EQ(nullable.row_->stored_cells()[1].ptr_, result.storage_datums_[1].ptr_);
}

TEST_F(FixedDmlCore, owned_list_and_chunk_rows_feed_storage_iterator_after_inputs_are_destroyed)
{
  ObSEArray<SqlTypeMeta, 4> types;
  columns(types, {type(ObIntType), type(ObVarcharType), type(ObNullType)});
  ObDASWriteBuffer buffer; ASSERT_EQ(OB_SUCCESS, buffer.init(arena_, 0, "FixedDmlCore"));
  std::vector<FixedRow *> owned_rows;
  {
    ShadowRow shadow; ASSERT_EQ(OB_SUCCESS, shadow.init(arena_, types, false));
    ObDatumRow source; ASSERT_EQ(OB_SUCCESS, source.init(arena_, 3));
    std::string string_value;
    // Production retains 128 linked rows, then appends to its in-memory Chunk store.
    for (int64_t i = 0; i < 130; ++i) {
      if (i != 0) { shadow.reuse(); }
      string_value = "row-" + std::to_string(i);
      source.storage_datums_[0].set_int(i - 65);
      source.storage_datums_[1].set_string(ObString(string_value.size(), string_value.data()));
      if (i % 2 == 0) { source.storage_datums_[1].set_has_lob_header(); }
      source.storage_datums_[2].set_null();
      ASSERT_EQ(OB_SUCCESS, shadow.shadow_copy(source));
      FixedRow *owned = nullptr; ASSERT_EQ(OB_SUCCESS, buffer.add_row(shadow, &owned));
      ASSERT_NE(nullptr, owned); owned_rows.push_back(owned);
      source.storage_datums_[0].set_int(0); string_value.assign(string_value.size(), 'x');
    }
  }
  ASSERT_EQ(130, buffer.get_row_cnt());
  ObDASWriteBuffer::NewRowIterator iterator;
  ASSERT_EQ(OB_SUCCESS, buffer.begin(iterator, types));
  for (int64_t i = 0; i < 130; ++i) {
    ObDatumRow *row = nullptr; ASSERT_EQ(OB_SUCCESS, iterator.get_next_row(row));
    ASSERT_NE(nullptr, row); ASSERT_EQ(3, row->get_column_count());
    EXPECT_EQ(i - 65, read_integer(row->storage_datums_[0]));
    EXPECT_EQ("row-" + std::to_string(i), read_text(row->storage_datums_[1]));
    EXPECT_TRUE(row->storage_datums_[2].is_null());
    EXPECT_EQ(i % 2 == 0 ? ObDatumDesc::HAS_LOB_HEADER : ObDatumDesc::NONE, row->storage_datums_[1].flag_);
    EXPECT_EQ(owned_rows[i]->stored_cells()[0].ptr_, row->storage_datums_[0].ptr_);
    EXPECT_EQ(owned_rows[i]->stored_cells()[1].ptr_, row->storage_datums_[1].ptr_);
  }
  ObDatumRow *row = reinterpret_cast<ObDatumRow *>(uintptr_t(1));
  EXPECT_EQ(OB_ITER_END, iterator.get_next_row(row)); EXPECT_EQ(nullptr, row);
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
