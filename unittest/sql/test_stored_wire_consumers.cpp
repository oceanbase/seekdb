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

#include "sql/das/ob_das_dml_ctx_define.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/px/datahub/components/ob_dh_range_dist_wf.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using FixedRow = ObChunkDatumStore::StoredRow;

// A shadow row intentionally borrows disjoint scalar/string buffers, as a
// projected DML or partial window row does before ownership is established.
struct BorrowedFixedRow
{
  explicit BorrowedFixedRow(uint32_t extra = 0)
    : descriptor_bytes_(sizeof(FixedRow) + 3 * sizeof(StoredDatumCell) + extra),
      row_(new (descriptor_bytes_.data()) FixedRow()), integer_(INT64_MIN),
      text_{'f', 'i', 'x', 'e', 'd', '\0'}
  {
    row_->cnt_ = 3;
    auto cells = row_->stored_cells();
    cells[0].ptr_ = reinterpret_cast<const char *>(&integer_);
    cells[0].len_ = sizeof(integer_);
    cells[0].flag_ = 0;
    cells[0].null_ = 0;
    cells[1].ptr_ = text_;
    cells[1].len_ = 5;
    cells[1].flag_ = 0;
    cells[1].null_ = 0;
    cells[2].set_null();
    row_->row_size_ = ObChunkDatumStore::Block::row_store_size(cells, 3, extra);
  }

  void overwrite() { integer_ = 17; MEMSET(text_, 'x', 5); }

  void expect_owned(const FixedRow &row) const
  {
    ASSERT_EQ(3, row.cnt_);
    EXPECT_EQ(INT64_MIN, StoredDatumCodec::decode_legacy_view(row.stored_cells()[0]).get_int());
    EXPECT_EQ(5, row.stored_cells()[1].len_);
    EXPECT_EQ(0, MEMCMP(row.stored_cells()[1].ptr_, "fixed", 5));
    EXPECT_TRUE(row.stored_cells()[2].is_null());
    auto first = reinterpret_cast<uintptr_t>(&row);
    for (int i = 0; i < 2; ++i) {
      auto pointer = reinterpret_cast<uintptr_t>(row.stored_cells()[i].ptr_);
      EXPECT_GE(pointer, first + sizeof(FixedRow) + 3 * sizeof(StoredDatumCell));
      EXPECT_LE(pointer + row.stored_cells()[i].len_, first + row.row_size_);
    }
  }

  std::vector<char> descriptor_bytes_;
  FixedRow *row_;
  int64_t integer_;
  char text_[6];
};

class DmlFixedShadow final : public ObDASWriteBuffer::DmlShadowRow
{
public:
  int bind(ObIAllocator &allocator, const FixedRow &row)
  {
    int ret = init(allocator, row.cnt_, false);
    if (ret == OB_SUCCESS) {
      MEMCPY(store_row_->stored_cells(), row.stored_cells(), row.cnt_ * sizeof(StoredDatumCell));
      store_row_->row_size_ = row.row_size_;
    }
    return ret;
  }
};
}

TEST(StoredWireConsumers, das_shadow_ownership_and_payload_relative_wire_roundtrip)
{
  ObArenaAllocator arena;
  BorrowedFixedRow input;
  DmlFixedShadow shadow;
  ASSERT_EQ(OB_SUCCESS, shadow.bind(arena, *input.row_));
  ObDASWriteBuffer source;
  ASSERT_EQ(OB_SUCCESS, source.init(arena, sizeof(int64_t)));
  FixedRow *owned = nullptr;
  ASSERT_EQ(OB_SUCCESS, source.add_row(shadow, &owned));
  ASSERT_NE(nullptr, owned);
  owned->extra_payload<int64_t>() = INT64_MAX;
  input.overwrite();
  input.expect_owned(*owned);
  EXPECT_EQ(INT64_MAX, owned->extra_payload<int64_t>());

  const char *owned_integer = owned->stored_cells()[0].ptr_;
  const char *owned_string = owned->stored_cells()[1].ptr_;
  const int64_t size = source.get_serialize_size();
  std::vector<char> wire(size + 16, char(0x5a));
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(wire.data(), size, position));
  ASSERT_EQ(size, position);
  EXPECT_EQ(owned_integer, owned->stored_cells()[0].ptr_);
  EXPECT_EQ(owned_string, owned->stored_cells()[1].ptr_);
  for (int64_t i = size; i < int64_t(wire.size()); ++i) { EXPECT_EQ(char(0x5a), wire[i]); }

  // Deserialization allocates another row and restores offsets against its
  // payload, not its header. Poison the serialized source before consuming it.
  ObArenaAllocator read_arena;
  ObDASWriteBuffer decoded;
  ASSERT_EQ(OB_SUCCESS, decoded.init(read_arena));
  position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), size, position));
  ASSERT_EQ(size, position);
  MEMSET(wire.data(), 0xcc, size);
  EXPECT_EQ(sizeof(int64_t), decoded.get_row_extend_size());
  ObDASWriteBuffer::Iterator iterator;
  ASSERT_EQ(OB_SUCCESS, decoded.begin(iterator));
  const FixedRow *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, iterator.get_next_row(result));
  ASSERT_NE(nullptr, result);
  input.expect_owned(*result);
  EXPECT_EQ(INT64_MAX, result->extra_payload<int64_t>());
  ASSERT_EQ(OB_ITER_END, iterator.get_next_row(result));
}

TEST(StoredWireConsumers, range_distribution_formalizes_disjoint_cells_and_extra_before_wire)
{
  ObArenaAllocator arena;
  ObExecContext exec(arena);
  ObRDWFPieceMsgCtx context(1, 1, INT64_MAX, exec);
  ObRDWFPartialInfo info(arena);
  BorrowedFixedRow input(sizeof(int64_t));
  input.row_->extra_payload<int64_t>() = -37;
  info.first_row_ = input.row_;
  info.last_row_ = input.row_;
  ASSERT_EQ(OB_SUCCESS, context.infos_.push_back(&info));
  ASSERT_EQ(OB_SUCCESS, context.formalize_store_row());
  ASSERT_NE(input.row_, info.first_row_);
  ASSERT_NE(input.row_, info.last_row_);
  input.overwrite();
  input.expect_owned(*info.first_row_);
  input.expect_owned(*info.last_row_);
  EXPECT_EQ(-37, info.first_row_->extra_payload<int64_t>());
  EXPECT_EQ(-37, info.last_row_->extra_payload<int64_t>());
  const int64_t expected_size = sizeof(FixedRow) + 3 * sizeof(StoredDatumCell)
      + sizeof(int64_t) + sizeof(int64_t) + 5;
  EXPECT_EQ(expected_size, info.first_row_->row_size_);
  EXPECT_EQ(expected_size, info.last_row_->row_size_);
}
