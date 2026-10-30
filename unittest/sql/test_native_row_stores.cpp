#include "sql/engine/basic/ob_chunk_row_store.h"
#include "sql/engine/basic/ob_ra_row_store.h"
#include "common/datum/datum_value_buffer.h"
#include "common/row/ob_row_store.h"
#include <gtest/gtest.h>
#include <vector>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
ObChunkRowStore::StoredRow *copy_chunk(const ObNewRow &row, uint32_t extra, std::vector<char> &memory)
{
  const int64_t bytes = ObChunkRowStore::Block::row_store_size(row, extra);
  memory.resize(bytes);
  auto *stored = new (memory.data()) ObChunkRowStore::StoredRow;
  EXPECT_EQ(OB_SUCCESS, stored->copy_row(row, stored->payload_,
      bytes - sizeof(*stored), bytes, extra));
  return stored;
}
}
TEST(NativeRowStores, chunk_owns_scalar_string_and_preserves_projected_order_and_extra)
{
  ObArenaAllocator source;
  DatumValueBuffer values(source);
  ASSERT_EQ(OB_SUCCESS, values.reserve(3));
  ASSERT_EQ(OB_SUCCESS, values.set_int(0, 17));
  char text[] = "a payload that cannot borrow a transient input row";
  ASSERT_EQ(OB_SUCCESS, values.set_varchar_view(1, ObString::make_string(text)));
  ASSERT_EQ(OB_SUCCESS, values.set_int(2, 29));
  int32_t projection[] = {1, 0, 1};
  ObNewRow row(values.data(), 3);
  row.projector_ = projection; row.projector_size_ = 3;
  std::vector<char> memory;
  auto *stored = copy_chunk(row, 3, memory);
  ASSERT_EQ(3, stored->cnt_);
  ASSERT_EQ(8, stored->extra_size_);
  std::memcpy(stored->get_extra_payload(), "XYZ", 3);
  std::memset(text, 'q', sizeof(text) - 1);
  ASSERT_EQ(OB_SUCCESS, values.set_int(0, 99));
  ASSERT_EQ(17, stored->cells()[1].get_int());
  ASSERT_EQ(ObString::make_string("a payload that cannot borrow a transient input row"), stored->cells()[0].get_string());
  ASSERT_EQ(stored->cells()[0].get_string(), stored->cells()[2].get_string());
  std::vector<char> moved(memory.size());
  auto *copy = new (moved.data()) ObChunkRowStore::StoredRow;
  ASSERT_EQ(OB_SUCCESS, copy->assign(stored));
  std::fill(memory.begin(), memory.end(), char(0xda));
  ASSERT_EQ(17, copy->cells()[1].get_int());
  ASSERT_EQ(0, std::memcmp(copy->get_extra_payload(), "XYZ", 3));
  ASSERT_EQ(ObString::make_string("a payload that cannot borrow a transient input row"), copy->cells()[0].get_string());
}
TEST(NativeRowStores, chunk_nested_array_survives_offsets_copy_and_source_reset)
{
  ObArenaAllocator source;
  auto *inner = DatumParameterArray::alloc(source, 1);
  auto *outer = DatumParameterArray::alloc(source, 2);
  ASSERT_NE(nullptr, inner); ASSERT_NE(nullptr, outer);
  ASSERT_EQ(OB_SUCCESS, inner->data_[0].set_varchar(ObString::make_string("nested array leaf bytes longer than sixteen"), source));
  SqlTypeMeta array_meta; array_meta.set_ext(); array_meta.set_extend_type(T_EXT_SQL_ARRAY);
  ASSERT_EQ(OB_SUCCESS, outer->data_[0].alloc_datum_reserved_buff(array_meta, 0, source));
  outer->data_[0].set_type_meta(array_meta); outer->data_[0].set_array_address(inner);
  ASSERT_EQ(OB_SUCCESS, outer->data_[1].set_int(31, source));
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.alloc_datum_reserved_buff(array_meta, 0, source));
  parameter.set_type_meta(array_meta); parameter.set_array_address(outer);
  DatumValue value = parameter.value_view(); ObNewRow row(&value, 1);
  std::vector<char> memory; auto *stored = copy_chunk(row, 5, memory);
  source.reset();
  ASSERT_EQ(OB_SUCCESS, stored->unswizzling());
  ASSERT_LT(reinterpret_cast<uintptr_t>(stored->cells()[0].datum_.ptr_), memory.size());
  std::vector<char> moved = memory;
  std::fill(memory.begin(), memory.end(), char(0xdb));
  auto *restored = reinterpret_cast<ObChunkRowStore::StoredRow *>(moved.data());
  ASSERT_EQ(OB_SUCCESS, restored->swizzling());
  auto *a = reinterpret_cast<DatumParameterArray *>(restored->cells()[0].get_ext());
  ASSERT_EQ(2, a->count_); ASSERT_EQ(31, a->data_[1].get_int());
  auto *b = a->data_[0].get_array<DatumParameterArray>();
  ASSERT_EQ(1, b->count_);
  ASSERT_EQ(ObString::make_string("nested array leaf bytes longer than sixteen"), b->data_[0].get_string());
}
TEST(NativeRowStores, copied_offset_payload_does_not_encode_copy_address_delta)
{
  ObArenaAllocator source; DatumValueBuffer values(source);
  ASSERT_EQ(OB_SUCCESS, values.reserve(1)); ASSERT_EQ(OB_SUCCESS, values.set_int(0, 41));
  ObNewRow row(values.data(), 1);
  std::vector<char> memory; auto *stored = copy_chunk(row, 0, memory);
  std::vector<char> copy = memory;
  auto *copied = reinterpret_cast<ObChunkRowStore::StoredRow *>(copy.data());
  ASSERT_EQ(OB_SUCCESS, copied->unswizzling(memory.data()));
  ASSERT_EQ(OB_SUCCESS, stored->unswizzling());
  ASSERT_EQ(memory, copy);
  ASSERT_EQ(OB_SUCCESS, copied->swizzling()); ASSERT_EQ(41, copied->cells()[0].get_int());
}
TEST(NativeRowStores, random_access_payload_is_owned_and_relocated_after_spill)
{
  ObArenaAllocator source; DatumValueBuffer values(source);
  ASSERT_EQ(OB_SUCCESS, values.reserve(2)); ASSERT_EQ(OB_SUCCESS, values.set_int(0, 47));
  ASSERT_EQ(OB_SUCCESS, values.set_varchar_view(1, ObString::make_string("random access stored value with external bytes")));
  ObNewRow row(values.data(), 2);
  const int64_t body_size = ObRARowStore::StoreRow::row_copy_size(row);
  std::vector<char> memory(sizeof(ObRARowStore::StoreRow) + body_size);
  auto *stored = new (memory.data()) ObRARowStore::StoreRow;
  ASSERT_EQ(OB_SUCCESS, stored->copy_row(row, stored->payload_, body_size));
  values.reset(); source.reset(); ASSERT_EQ(OB_SUCCESS, stored->to_copyable());
  ASSERT_FALSE(stored->readable_);
  std::vector<char> copy = memory; std::fill(memory.begin(), memory.end(), char(0xdc));
  auto *restored = reinterpret_cast<ObRARowStore::StoreRow *>(copy.data());
  ASSERT_EQ(OB_SUCCESS, restored->to_readable()); ASSERT_TRUE(restored->readable_);
  ASSERT_EQ(47, restored->cells()[0].get_int());
  ASSERT_EQ(ObString::make_string("random access stored value with external bytes"), restored->cells()[1].get_string());
}
TEST(NativeRowStores, malformed_flat_payload_fails_before_publishing_pointer_relocation)
{
  ObArenaAllocator source; DatumValueBuffer values(source);
  ASSERT_EQ(OB_SUCCESS, values.reserve(2)); ASSERT_EQ(OB_SUCCESS, values.set_int(0, 53));
  ASSERT_EQ(OB_SUCCESS, values.set_int(1, 59)); ObNewRow row(values.data(), 2);
  std::vector<char> memory; auto *stored = copy_chunk(row, 0, memory);
  ASSERT_EQ(OB_SUCCESS, stored->unswizzling());
  const char *first_offset = stored->cells()[0].datum_.ptr_;
  stored->cells()[1].datum_.len_ = UINT32_MAX / 2;
  ASSERT_NE(OB_SUCCESS, stored->swizzling());
  ASSERT_EQ(first_offset, stored->cells()[0].datum_.ptr_);
}

TEST(NativeRowStores, overdepth_arrays_reject_at_public_store_entry_without_publication)
{
  ObArenaAllocator source, owner;
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, value.set_int(17, source));
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  for (int depth = 0; depth < 65; ++depth) {
    auto *array = DatumParameterArray::alloc(source, 1);
    ASSERT_NE(nullptr, array);
    array->data_[0] = value;
    DatumParameter parent;
    ASSERT_EQ(OB_SUCCESS, parent.alloc_datum_reserved_buff(meta, 0, source));
    parent.set_type_meta(meta); parent.set_array_address(array); value = parent;
  }
  DatumValue cell = value.value_view();
  ObNewRow row(&cell, 1);
  int32_t projector[] = {0}; row.projector_ = projector; row.projector_size_ = 1;
  ASSERT_EQ(INT64_MAX, row.get_projected_deep_copy_size());
  {
    ObRowStore store(owner, "OverdepthRaw", false);
    const ObRowStore::StoredRow *stored = nullptr;
    EXPECT_EQ(OB_SIZE_OVERFLOW, store.add_row(row, stored, 0, true));
    EXPECT_EQ(nullptr, stored); EXPECT_EQ(0, store.get_row_count());
    EXPECT_EQ(0, store.get_block_count());
  }
  {
    ObChunkRowStore store(&owner);
    ASSERT_EQ(OB_SUCCESS, store.init(0, ObCtxIds::DEFAULT_CTX_ID, "OverdepthChunk", false, ObChunkRowStore::FULL));
    ObChunkRowStore::StoredRow *stored = nullptr;
    EXPECT_EQ(OB_SIZE_OVERFLOW, store.add_row(row, &stored));
    EXPECT_EQ(nullptr, stored); EXPECT_EQ(0, store.get_row_cnt());
  }
  {
    ObRARowStore store(&owner, true);
    ASSERT_EQ(OB_SUCCESS, store.init(0));
    EXPECT_EQ(OB_SIZE_OVERFLOW, store.add_row(row));
    EXPECT_EQ(0, store.get_row_cnt());
  }
  EXPECT_EQ(projector, row.projector_); EXPECT_EQ(1, row.projector_size_);
}
