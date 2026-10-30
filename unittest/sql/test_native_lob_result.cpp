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

#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "common/datum/datum_value.h"
#include "share/ob_i_lob_read_service.h"
#include "share/lob/ob_lob_text_iter_context.h"
#include <vector>
#include <gtest/gtest.h>
#include <string>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
class RejectAllocator : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t,const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
}
TEST(NativeLobResult, empty_and_nonempty_results_keep_metadata_and_own_input_bytes) {
  ObArenaAllocator owner;
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS,output.set_int(7,owner));
  ASSERT_EQ(OB_SUCCESS,ObTextStringHelper::str_to_lob_storage_obj(owner,ObString(),output));
  EXPECT_EQ(ObLongTextType,output.get_type()); EXPECT_TRUE(output.has_lob_header());
  EXPECT_EQ(CS_LEVEL_IMPLICIT,output.get_collation_level());
  EXPECT_EQ(CS_TYPE_BINARY,output.get_collation_type());
  ObString content;
  ObLobLocatorV2 empty(output.get_string(),true);
  ASSERT_EQ(OB_SUCCESS,empty.get_inrow_data(content)); EXPECT_EQ(0,content.length());
  {
    std::string input(8192,'v');
    ASSERT_EQ(OB_SUCCESS,ObTextStringHelper::str_to_lob_storage_obj(owner,ObString(input.size(),input.data()),output));
    input.assign(8192,'x');
  }
  ObLobLocatorV2 locator(output.get_string(),true);
  ASSERT_EQ(OB_SUCCESS,locator.get_inrow_data(content));
  EXPECT_EQ(8192,content.length());
  for(int i=0;i<content.length();++i) { ASSERT_EQ('v',content.ptr()[i]); }
  EXPECT_EQ(CS_TYPE_BINARY,output.get_collation_type());
  EXPECT_EQ(CS_LEVEL_IMPLICIT,output.get_collation_level());
}
TEST(NativeLobResult, failed_allocation_keeps_original_payload_and_metadata) {
  ObArenaAllocator owner; RejectAllocator reject;
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS,output.set_varchar(ObString::make_string("original"),owner));
  output.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  output.set_collation_level(CS_LEVEL_COERCIBLE);
  const char *original=output.get_string_ptr();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,ObTextStringHelper::str_to_lob_storage_obj(reject,ObString::make_string("replace"),output));
  EXPECT_EQ(original,output.get_string_ptr());
  EXPECT_EQ(ObString::make_string("original"),output.get_string());
  EXPECT_EQ(ObVarcharType,output.get_type()); EXPECT_EQ(CS_TYPE_UTF8MB4_BIN,output.get_collation_type());
  EXPECT_EQ(CS_LEVEL_COERCIBLE,output.get_collation_level());
}

namespace {
DatumValue lob_view(SqlValueType type, const ObString &bytes) {
  DatumValue value;
  value.meta_.set_type(type);
  value.meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  value.meta_.set_collation_level(CS_LEVEL_COERCIBLE);
  value.meta_.set_has_lob_header();
  value.datum_.set_string(bytes);
  return value;
}
std::vector<char> memory_lob(const std::string &text, bool outrow = false,
                             ObMemLobType kind = PERSISTENT_LOB) {
  const int64_t disk_size = outrow
      ? sizeof(ObLobCommon) + sizeof(ObLobData) + sizeof(ObLobDataOutRowCtx) + sizeof(uint64_t)
      : sizeof(ObLobCommon) + text.size();
  std::vector<char> bytes(sizeof(ObMemLobCommon) + disk_size);
  auto *memory = new(bytes.data()) ObMemLobCommon(kind, false);
  memory->set_has_inrow_data(!outrow);
  auto *disk = new(memory->data_) ObLobCommon();
  if (outrow) {
    disk->in_row_ = false; disk->is_init_ = true;
    auto *data = new(disk->buffer_) ObLobData();
    data->byte_size_ = text.size();
    new(data->buffer_) ObLobDataOutRowCtx();
  } else {
    std::memcpy(disk->buffer_, text.data(), text.size());
  }
  return bytes;
}
class BlockReadService : public ObILobReadService {
public:
  class Cursor : public ObILobReadCursor {
    int get_next_row(ObString &) override { return OB_ITER_END; }
    void reset() override {}
  } cursor;
  std::vector<std::string> blocks;
  int next_index = 0;
  int fail_after = -1;
  int release_count = 0;
  int next(ObLobTextIterCtx &ctx, ObString &out, ObTextStringIterState &state) {
    if (next_index == fail_after) { return OB_IO_ERROR; }
    if (next_index == static_cast<int>(blocks.size())) {
      state = TEXTSTRING_ITER_END; return OB_SUCCESS;
    }
    const std::string &block = blocks[next_index++];
    out.assign_ptr(block.data(), block.size());
    ctx.content_byte_len_ = block.size(); state = TEXTSTRING_ITER_NEXT;
    return OB_SUCCESS;
  }
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
    { return OB_NOT_SUPPORTED; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, SqlValueType, ObCollationType,
                            ObLobLocatorV2 &, ObIAllocator *, ObString &) override
    { return OB_NOT_SUPPORTED; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool,
                            ObIAllocator *, uint32_t) override { return OB_NOT_SUPPORTED; }
  int get_first_block(ObLobTextIterCtx &ctx, ObCollationType, bool, bool,
                     ObIAllocator *, ObString &out, ObTextStringIterState &state) override {
    ctx.read_cursor_ = &cursor; return next(ctx, out, state);
  }
  int get_next_block_inner(ObLobTextIterCtx &ctx, ObCollationType, bool, bool,
                          ObString &out, ObTextStringIterState &state) override
    { return next(ctx, out, state); }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
    { return OB_NOT_SUPPORTED; }
  void free_lob_query_iter(ObLobTextIterCtx &ctx) override
    { ++release_count; ctx.read_cursor_ = nullptr; }
};
}

TEST(NativeLobConversion, inapplicable_input_keeps_existing_output) {
  ObArenaAllocator owner;
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, output.set_varchar(ObString::make_string("unchanged"), owner));
  const char *original = output.datum_.ptr_;
  DatumValue plain, null;
  ASSERT_EQ(OB_SUCCESS, plain.set_varchar(ObString::make_string("input"), owner));
  for (const DatumValue &input : {plain, null}) {
    EXPECT_EQ(OB_SUCCESS, ObTextStringIter::convert_outrow_lob_to_inrow_templob(
        input, output, nullptr, owner, false, true));
    EXPECT_EQ(original, output.datum_.ptr_);
  }
}

TEST(NativeLobConversion, permitted_inrow_value_borrows_or_deep_copies_exact_metadata) {
  ObArenaAllocator owner;
  auto bytes = memory_lob("borrow");
  DatumValue input = lob_view(ObLongTextType, ObString(bytes.size(), bytes.data()));
  DatumValue output;
  ASSERT_EQ(OB_SUCCESS, ObTextStringIter::convert_outrow_lob_to_inrow_templob(
      input, output, nullptr, owner, true));
  EXPECT_EQ(input.datum_.ptr_, output.datum_.ptr_);
  EXPECT_EQ(CS_LEVEL_COERCIBLE, output.get_collation_level());
  EXPECT_TRUE(output.has_lob_header());
  ASSERT_EQ(OB_SUCCESS, ObTextStringIter::convert_outrow_lob_to_inrow_templob(
      input, output, nullptr, owner, true, true));
  EXPECT_NE(input.datum_.ptr_, output.datum_.ptr_);
  std::memset(bytes.data(), 'x', bytes.size());
  ObString payload;
  ASSERT_EQ(OB_SUCCESS, ObLobLocatorV2(output.get_string(), true).get_inrow_data(payload));
  EXPECT_EQ(ObString::make_string("borrow"), payload);
}

TEST(NativeLobConversion, rebuilt_inrow_value_owns_bytes_and_matches_lob_metadata_rules) {
  for (SqlValueType type : {ObLongTextType, ObJsonType, ObCollectionSQLType}) {
    ObArenaAllocator owner;
    auto bytes = memory_lob("converted");
    DatumValue value = lob_view(type, ObString(bytes.size(), bytes.data()));
    const char *original = value.datum_.ptr_;
    ASSERT_EQ(OB_SUCCESS, ObTextStringIter::convert_outrow_lob_to_inrow_templob(
        value, value, nullptr, owner));
    EXPECT_NE(original, value.datum_.ptr_);
    EXPECT_EQ(type, value.get_type()); EXPECT_TRUE(value.has_lob_header());
    EXPECT_FALSE(value.meta_.is_outrow());
    EXPECT_EQ(type == ObCollectionSQLType ? CS_LEVEL_EXPLICIT : CS_LEVEL_IMPLICIT,
              value.get_collation_level());
    if (type == ObCollectionSQLType) {
      EXPECT_EQ(UINT_MAX16, value.get_subschema_id());
    }
    std::memset(bytes.data(), 'x', bytes.size());
    ObString payload;
    ASSERT_EQ(OB_SUCCESS, ObLobLocatorV2(value.get_string(), true).get_inrow_data(payload));
    EXPECT_EQ(ObString::make_string("converted"), payload);
  }
}

TEST(NativeLobConversion, allocation_error_keeps_source_and_output) {
  ObArenaAllocator owner; RejectAllocator reject;
  auto bytes = memory_lob("allocation");
  DatumValue value = lob_view(ObLongTextType, ObString(bytes.size(), bytes.data()));
  const char *original = value.datum_.ptr_;
  for (bool allow : {false, true}) {
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObTextStringIter::convert_outrow_lob_to_inrow_templob(
        value, value, nullptr, reject, allow, true));
    EXPECT_EQ(original, value.datum_.ptr_);
    EXPECT_EQ(CS_LEVEL_COERCIBLE, value.get_collation_level());
  }
}

TEST(NativeLobConversion, outrow_chunks_are_owned_and_cursor_is_released) {
  ObArenaAllocator owner;
  auto bytes = memory_lob("abcdefgh", true);
  DatumValue value = lob_view(ObLongTextType, ObString(bytes.size(), bytes.data()));
  BlockReadService service; service.blocks = {"abc", "defgh"};
  ObLobReadOptions options(service);
  ASSERT_EQ(OB_SUCCESS, ObTextStringIter::convert_outrow_lob_to_inrow_templob(
      value, value, &options, owner));
  EXPECT_EQ(2, service.next_index); EXPECT_EQ(1, service.release_count);
  service.blocks.clear(); std::memset(bytes.data(), 'x', bytes.size());
  ObString payload;
  ASSERT_EQ(OB_SUCCESS, ObLobLocatorV2(value.get_string(), true).get_inrow_data(payload));
  EXPECT_EQ(ObString::make_string("abcdefgh"), payload);
}

TEST(NativeLobConversion, iterator_errors_and_short_reads_do_not_publish_partial_results) {
  for (int failure : {-1, 1}) {
    ObArenaAllocator owner;
    auto bytes = memory_lob("abcdefgh", true);
    DatumValue value = lob_view(ObLongTextType, ObString(bytes.size(), bytes.data()));
    const char *original = value.datum_.ptr_;
    BlockReadService service; service.blocks = {"abc"}; service.fail_after = failure;
    ObLobReadOptions options(service);
    EXPECT_EQ(failure < 0 ? OB_ERR_UNEXPECTED : OB_IO_ERROR,
              ObTextStringIter::convert_outrow_lob_to_inrow_templob(
                  value, value, &options, owner));
    EXPECT_EQ(original, value.datum_.ptr_);
    EXPECT_EQ(1, service.release_count);
  }
}

int main(int argc,char **argv) {
  if(ObCharset::init_charset()!=OB_SUCCESS) { return 2; }
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
