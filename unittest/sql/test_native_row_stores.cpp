#include "sql/engine/basic/ob_chunk_row_store.h"
#include "sql/engine/basic/ob_ra_row_store.h"
#include "common/datum/datum_value_buffer.h"
#include "common/row/ob_row_store.h"
#include <gtest/gtest.h>
#include <vector>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include "sql/ob_scanner.h"
#include "sql/engine/basic/ob_values_op.h"
#include "sql/pl/ob_pl_type.h"
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
  ASSERT_EQ(OB_SUCCESS, outer->data_[0].set_extend(reinterpret_cast<int64_t>(inner), T_EXT_SQL_ARRAY, source));
  ASSERT_EQ(OB_SUCCESS, outer->data_[1].set_int(31, source));
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_extend(reinterpret_cast<int64_t>(outer), T_EXT_SQL_ARRAY, source));
  DatumValue value = parameter.value_view(); ObNewRow row(&value, 1);
  std::vector<char> memory; auto *stored = copy_chunk(row, 5, memory);
  source.reset();
  ASSERT_EQ(OB_SUCCESS, stored->unswizzling());
  ASSERT_LT(reinterpret_cast<uintptr_t>(stored->cells()[0].datum_.get_reference()), memory.size());
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
  ASSERT_EQ(OB_SUCCESS, values.reserve(2));
  ASSERT_EQ(OB_SUCCESS, values.set_varchar_view(0, ObString::make_string("first stored reference payload")));
  ASSERT_EQ(OB_SUCCESS, values.set_varchar_view(1, ObString::make_string("second stored reference payload")));
  ObNewRow row(values.data(), 2);
  std::vector<char> memory; auto *stored = copy_chunk(row, 0, memory);
  ASSERT_EQ(OB_SUCCESS, stored->unswizzling());
  const void *first_offset = stored->cells()[0].datum_.get_reference();
  Datum &malformed = stored->cells()[1].datum_;
  ASSERT_EQ(OB_SUCCESS, malformed.set_reference(malformed.get_reference(), UINT32_MAX / 2));
  ASSERT_NE(OB_SUCCESS, stored->swizzling());
  ASSERT_EQ(first_offset, stored->cells()[0].datum_.get_reference());
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
    ASSERT_EQ(OB_SUCCESS, parent.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source));
    value = parent;
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

namespace {
// Captured by the accepted 1d6 serializer (Value16/Parameter64) using a
// zeroing allocator. These are real historical wire images, not current
// descriptors relabelled with an old version number.
std::vector<char> old_row_wire(const char *name)
{
  std::ifstream input(std::string("unittest/sql/data/native-row-") + name, std::ios::binary);
  EXPECT_TRUE(input.good());
  return std::vector<char>(std::istreambuf_iterator<char>(input), {});
}
void expect_old_row(const ObNewRow &row, bool full)
{
  ASSERT_EQ(full ? 4 : 2, row.count_);
  EXPECT_EQ(INT64_MIN, row.cells_[0].get_int());
  EXPECT_EQ(ObString::make_string("old physical row bytes"), row.cells_[1].get_string());
  if (full) {
    EXPECT_TRUE(row.cells_[2].is_null());
    EXPECT_EQ(ObDecimalIntType, row.cells_[2].get_type());
    EXPECT_EQ(5, row.cells_[2].get_scale());
    const auto *array = reinterpret_cast<const DatumParameterArray *>(row.cells_[3].get_ext());
    ASSERT_NE(nullptr, array); ASSERT_EQ(2, array->count_);
    EXPECT_EQ(177, array->source_value_size_);
    EXPECT_EQ(123, array->data_[0].get_int());
    EXPECT_EQ(20, array->data_[0].get_precision());
    EXPECT_EQ(0x1234u, array->data_[0].get_result_flag());
    EXPECT_EQ(37, array->data_[0].get_raw_text_pos()); EXPECT_EQ(41, array->data_[0].get_raw_text_len());
    EXPECT_TRUE(array->data_[0].need_to_check_type());
    EXPECT_TRUE(array->data_[1].is_null());
    EXPECT_EQ(ObDecimalIntType, array->data_[1].get_type());
    EXPECT_EQ(ObDecimalIntType, array->data_[1].get_param_meta().get_type());
    EXPECT_EQ(20, array->data_[1].get_precision()); EXPECT_EQ(5, array->data_[1].get_scale());
    // The historical image does not contain full runtime Type provenance.
    EXPECT_EQ(nullptr, array->get_element_type(0));
    EXPECT_EQ(nullptr, array->get_element_type(1));
  }
}
void expect_old_store_row(ObRowStore &store, bool full)
{
  ASSERT_EQ(1, store.get_row_count());
  auto it = store.begin();
  if (store.use_compact_row()) {
    DatumValue cells[4]; ObNewRow row(cells, full ? 4 : 2);
    ASSERT_EQ(OB_SUCCESS, it.get_next_row(row)); expect_old_row(row, full);
    EXPECT_EQ(OB_ITER_END, it.get_next_row(row));
  } else {
    ObNewRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, it.get_next_row(row, nullptr)); ASSERT_NE(nullptr, row);
    expect_old_row(*row, full);
    EXPECT_EQ(OB_ITER_END, it.get_next_row(row, nullptr));
  }
}
template <typename Store>
std::vector<char> current_row_wire(const Store &store)
{
  std::vector<char> result(store.get_serialize_size()); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, store.serialize(result.data(), result.size(), pos));
  EXPECT_EQ(result.size(), pos);
  return result;
}
}

TEST(NativeRowStores, actual_1d6_raw_and_compact_wire_converts_then_reencodes_logical_v2)
{
  for (bool compact : {false, true}) {
    ObArenaAllocator output_owner;
    ObRowStore output(output_owner, "V2Rows", compact);
    std::vector<char> native_wire;
    {
      auto bytes = old_row_wire(compact ? "row-compact-v1.bin" : "row-flat-v1.bin");
      ASSERT_FALSE(bytes.empty());
      ObArenaAllocator input_owner; ObRowStore input(input_owner, "OldRows", compact);
      int64_t pos = 0;
      ASSERT_EQ(OB_SUCCESS, input.deserialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(bytes.size(), pos);
      std::fill(bytes.begin(), bytes.end(), char(0xa5));
      expect_old_store_row(input, !compact);
      native_wire = current_row_wire(input);
    }
    int64_t marker = 0, marker_pos = 0;
    ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(native_wire.data(), native_wire.size(), marker_pos, &marker));
    EXPECT_EQ(-2, marker);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, output.deserialize(native_wire.data(), native_wire.size(), pos));
    EXPECT_EQ(native_wire.size(), pos);
    std::fill(native_wire.begin(), native_wire.end(), char(0xa6));
    expect_old_store_row(output, !compact);
  }
}

TEST(NativeRowStores, actual_1d6_scanner_and_values_outer1_upgrade_to_outer2)
{
  ObArenaAllocator owner;
  {
    auto bytes = old_row_wire("scanner-v1.bin"); ASSERT_FALSE(bytes.empty()); EXPECT_EQ(1, bytes[0]);
    ObScanner source("OldScanner", &owner); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, source.deserialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(bytes.size(), pos);
    std::fill(bytes.begin(), bytes.end(), char(0xb5));
    auto native_wire = current_row_wire(source); ASSERT_FALSE(native_wire.empty()); EXPECT_EQ(2, native_wire[0]);
    ObScanner output("NewScanner", &owner); pos = 0;
    ASSERT_EQ(OB_SUCCESS, output.deserialize(native_wire.data(), native_wire.size(), pos));
    EXPECT_EQ(native_wire.size(), pos); std::fill(native_wire.begin(), native_wire.end(), char(0xb6));
    auto it = output.begin(); ObNewRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, it.get_next_row(row, nullptr)); ASSERT_NE(nullptr, row); expect_old_row(*row, true);
  }
  {
    auto bytes = old_row_wire("values-v1.bin"); ASSERT_FALSE(bytes.empty()); EXPECT_EQ(1, bytes[0]);
    ObValuesSpec source(owner, PHY_VALUES); int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, source.deserialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(bytes.size(), pos);
    auto native_wire = current_row_wire(source); ASSERT_FALSE(native_wire.empty()); EXPECT_EQ(2, native_wire[0]);
    ObValuesSpec output(owner, PHY_VALUES); pos = 0;
    ASSERT_EQ(OB_SUCCESS, output.deserialize(native_wire.data(), native_wire.size(), pos));
    EXPECT_EQ(native_wire.size(), pos); std::fill(native_wire.begin(), native_wire.end(), char(0xb7));
    expect_old_store_row(output.row_store_, false);
  }
}

TEST(NativeRowStores, actual_1d6_chunk_wire_converts_then_reencodes_logical_v2)
{
  ObArenaAllocator owner;
  auto bytes = old_row_wire("chunk-v1.bin"); ASSERT_FALSE(bytes.empty()); EXPECT_EQ(1, bytes[0]);
  ObChunkRowStore source(&owner); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.deserialize(bytes.data(), bytes.size(), pos)); EXPECT_EQ(bytes.size(), pos);
  std::fill(bytes.begin(), bytes.end(), char(0xc5));
  auto native_wire = current_row_wire(source); ASSERT_FALSE(native_wire.empty()); EXPECT_EQ(2, native_wire[0]);
  ObChunkRowStore output(&owner); pos = 0;
  ASSERT_EQ(OB_SUCCESS, output.deserialize(native_wire.data(), native_wire.size(), pos));
  EXPECT_EQ(native_wire.size(), pos); std::fill(native_wire.begin(), native_wire.end(), char(0xc6));
  EXPECT_EQ(1, output.get_row_cnt());
  ObChunkRowStore::Iterator it;
  ASSERT_EQ(OB_SUCCESS, output.begin(it));
  const ObChunkRowStore::StoredRow *stored = nullptr;
  ASSERT_EQ(OB_SUCCESS, it.get_next_row(stored)); ASSERT_NE(nullptr, stored);
  ObNewRow row(const_cast<DatumValue *>(stored->cells()), stored->cnt_); expect_old_row(row, true);
  EXPECT_EQ(OB_ITER_END, it.get_next_row(stored));
}

TEST(NativeRowStores, outer_headers_reject_unknown_overlong_and_truncated_without_advancing)
{
  ObArenaAllocator owner;
  for (const char *name : {"scanner-v1.bin", "values-v1.bin"}) {
    auto bytes = old_row_wire(name); ASSERT_GT(bytes.size(), 6u);
    for (int64_t length = 0; length < 6; ++length) {
      int64_t pos = 0;
      if (name[0] == 's') {
        ObScanner scanner("InvalidScanner", &owner);
        EXPECT_NE(OB_SUCCESS, scanner.deserialize(bytes.data(), length, pos));
      } else {
        ObValuesSpec spec(owner, PHY_VALUES);
        EXPECT_NE(OB_SUCCESS, spec.deserialize(bytes.data(), length, pos));
      }
      EXPECT_EQ(0, pos);
    }
    bytes[0] = 3; int64_t pos = 0;
    if (name[0] == 's') {
      ObScanner scanner("InvalidScanner", &owner);
      EXPECT_EQ(OB_NOT_SUPPORTED, scanner.deserialize(bytes.data(), bytes.size(), pos));
    } else {
      ObValuesSpec spec(owner, PHY_VALUES);
      EXPECT_EQ(OB_NOT_SUPPORTED, spec.deserialize(bytes.data(), bytes.size(), pos));
    }
    EXPECT_EQ(0, pos);
  }
  char overlong[20]; MEMSET(overlong, 0x80, sizeof(overlong)); int64_t pos = 0;
  ObScanner scanner("InvalidScanner", &owner);
  EXPECT_EQ(OB_DESERIALIZE_ERROR, scanner.deserialize(overlong, sizeof(overlong), pos)); EXPECT_EQ(0, pos);
  ObValuesSpec spec(owner, PHY_VALUES);
  EXPECT_EQ(OB_DESERIALIZE_ERROR, spec.deserialize(overlong, sizeof(overlong), pos)); EXPECT_EQ(0, pos);
}

TEST(NativeRowStores, truncated_v2_row_body_preserves_existing_store_and_position)
{
  ObArenaAllocator owner;
  ObRowStore source(owner, "AtomicSource", false), output(owner, "AtomicOutput", false);
  DatumValue cell; ASSERT_EQ(OB_SUCCESS, cell.set_int(733, owner)); ObNewRow row(&cell, 1);
  ASSERT_EQ(OB_SUCCESS, source.add_row(row)); ASSERT_EQ(OB_SUCCESS, output.add_row(row));
  const auto bytes = current_row_wire(source); ASSERT_FALSE(bytes.empty());
  for (int64_t length = 0; length < bytes.size(); ++length) {
    int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, output.deserialize(bytes.data(), length, pos)) << length;
    EXPECT_EQ(0, pos); ASSERT_EQ(1, output.get_row_count());
    auto it = output.begin(); ObNewRow *stored = nullptr;
    ASSERT_EQ(OB_SUCCESS, it.get_next_row(stored, nullptr)); ASSERT_NE(nullptr, stored);
    EXPECT_EQ(733, stored->cells_[0].get_int());
  }
}

TEST(NativeRowStores, legacy_extended_span_is_validated_before_reading_its_payload)
{
  // Only the historical public descriptor fields are assembled here, to put
  // a zero-byte extended value exactly at the end of a bounded image.
  // The reader must reject its physical shape before reading an address.
  alignas(8) char image[16] = {};
  const uint64_t end = sizeof(image); MEMCPY(image, &end, sizeof(end));
  ObDatum physical; physical.set_ext(0);
  MEMCPY(image + 8, &physical.pack_, sizeof(physical.pack_));
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(oceanbase::pl::PL_RECORD_TYPE);
  MEMCPY(image + 12, &meta, sizeof(meta));
  ObArenaAllocator owner;
  DatumValue output; ASSERT_EQ(OB_SUCCESS, output.set_int(911, owner));
  EXPECT_NE(OB_SUCCESS, NativeValueFlat::read_legacy_1d6_value(image, sizeof(image), 0, owner, output));
  EXPECT_EQ(911, output.get_int());
}

TEST(NativeRowStores, chunk_wire_appends_on_reuse_and_failed_append_preserves_existing_rows)
{
  ObArenaAllocator owner;
  const auto old_wire = old_row_wire("chunk-v1.bin"); ASSERT_FALSE(old_wire.empty());
  ObChunkRowStore source(&owner); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.deserialize(old_wire.data(), old_wire.size(), pos));
  const auto new_wire = current_row_wire(source);
  for (const auto *wire : {&old_wire, &new_wire}) {
    ASSERT_GT(wire->size(), 1u);
    ObChunkRowStore output(&owner);
    for (int iteration = 0; iteration < 2; ++iteration) {
      pos = 0;
      ASSERT_EQ(OB_SUCCESS, output.deserialize(wire->data(), wire->size(), pos));
      EXPECT_EQ(wire->size(), pos); EXPECT_EQ(iteration + 1, output.get_row_cnt());
    }
    pos = 0;
    EXPECT_NE(OB_SUCCESS, output.deserialize(wire->data(), wire->size() - 1, pos));
    EXPECT_EQ(2, output.get_row_cnt());
    ObChunkRowStore::Iterator it; ASSERT_EQ(OB_SUCCESS, output.begin(it));
    const ObChunkRowStore::StoredRow *stored = nullptr;
    for (int iteration = 0; iteration < 2; ++iteration) {
      ASSERT_EQ(OB_SUCCESS, it.get_next_row(stored)); ASSERT_NE(nullptr, stored);
      ObNewRow row(const_cast<DatumValue *>(stored->cells()), stored->cnt_);
      expect_old_row(row, true);
    }
    EXPECT_EQ(OB_ITER_END, it.get_next_row(stored));
  }
}

TEST(NativeRowStores, compact_extended_action_markers_survive_end_flag_and_source_owner_retirement)
{
  for (int64_t action : {int64_t(ObActionFlag::OP_DEL_ROW), int64_t(ObActionFlag::OP_LOCK_ROW),
                         int64_t(ObActionFlag::OP_ROW_DOES_NOT_EXIST)}) {
    SCOPED_TRACE(action);
    std::vector<char> wire;
    {
      ObArenaAllocator source_owner;
      ObRowStore source(source_owner, "CompactActions", true);
      DatumValue cells[2];
      ASSERT_EQ(OB_SUCCESS, cells[0].set_extend(action, 0, source_owner));
      ASSERT_EQ(OB_SUCCESS, cells[1].set_int(733, source_owner));
      ObNewRow row(cells, 2);
      ASSERT_EQ(OB_SUCCESS, source.add_row(row));
      // The compact stream already owns these source fields. Overwrite the
      // actual inline source bits before serializing the retained row.
      ASSERT_TRUE(cells[0].datum_.has_ext_value());
      ASSERT_TRUE(cells[0].datum_.is_inline_extended());
      cells[0].datum_.set_null();
      cells[1].datum_.set_int(999);
      wire = current_row_wire(source);
    }
    ObArenaAllocator output_owner;
    ObRowStore output(output_owner, "CompactActOut", true);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, output.deserialize(wire.data(), wire.size(), pos)); EXPECT_EQ(wire.size(), pos);
    std::fill(wire.begin(), wire.end(), char(0xa8));
    DatumValue cells[2]; ObNewRow row(cells, 2); auto it = output.begin();
    ASSERT_EQ(OB_SUCCESS, it.get_next_row(row));
    EXPECT_EQ(action, cells[0].get_ext()); EXPECT_EQ(733, cells[1].get_int());
    EXPECT_EQ(OB_ITER_END, it.get_next_row(row));
  }
}
