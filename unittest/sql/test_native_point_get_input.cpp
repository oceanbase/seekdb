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

#include "sql/engine/table/native_point_get_input.h"
#include "sql/rewrite/ob_query_range_define.h"
#include "common/datum/datum_copy_plan.h"
#include "lib/charset/ob_charset.h"
#include <array>
#include <atomic>
#include <cstring>
#include <thread>
#include <type_traits>
#include <utility>
#include <gtest/gtest.h>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
static_assert(!std::is_copy_constructible<NativePointGetInput>::value, "A published owner cannot move");
static_assert(!std::is_copy_assignable<NativePointGetInput>::value, "A published owner cannot copy");
static_assert(!std::is_move_constructible<NativePointGetInput>::value, "Views require a stable address");
static_assert(!std::is_move_assignable<NativePointGetInput>::value, "Views require a stable address");
static_assert(std::is_same<decltype(std::declval<const NativePointGetInput &>().values()),
                           const int64_t *>::value, "The fixed BIGINT layout exposes inline integers");
static_assert(sizeof(int64_t[NativePointGetInput::COLUMN_COUNT]) == 24,
              "The three non-NULL keys need exactly 24 value bytes");

void prepare_scalar_type(SqlType &type, SqlValueType kind,
                         ObPrecision precision = PRECISION_UNKNOWN_YET, ObScale scale = -1)
{
  SqlTypeMeta meta;
  meta.set_type(kind);
  if (kind == ObDecimalIntType) { meta.set_scale(scale); }
  ObAccuracy accuracy;
  accuracy.set_precision(precision); accuracy.set_scale(scale);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, type));
}

void assert_same_key(const NativePointGetInput &owner, const int64_t (&expected)[3])
{
  ASSERT_EQ(1, owner.lookup_keys().count());
  const auto &key = owner.lookup_keys().at(0);
  ASSERT_TRUE(key.is_valid());
  ASSERT_EQ(3, key.count_);
  ASSERT_NE(nullptr, key.cached_rowkey_);
  ASSERT_TRUE(key.cached_rowkey_->is_valid());
  ASSERT_EQ(3, key.cached_rowkey_->get_value_count());
  SqlTypeMeta meta; meta.set_int();
  DatumValue baseline[3];
  for (int i = 0; i < 3; ++i) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(key.datums_[i].ptr_);
    EXPECT_EQ(reinterpret_cast<const char *>(owner.values() + i), key.datums_[i].ptr_);
    EXPECT_EQ(0U, address % alignof(int64_t));
    EXPECT_FALSE(key.datums_[i].is_local_buf());
    EXPECT_FALSE(key.datums_[i].is_null());
    EXPECT_EQ(sizeof(int64_t), key.datums_[i].pack_);
    EXPECT_EQ(expected[i], owner.values()[i]);
    EXPECT_EQ(expected[i], key.datums_[i].get_int());
    EXPECT_EQ(expected[i], key.cached_rowkey_->get_cell(i).get_int());
    EXPECT_EQ(0, key.cached_rowkey_->get_cell(i).get_datum().length());
    EXPECT_EQ(ObIntType, key.cached_rowkey_->get_cell(i).get_type());
    ObDatum legacy;
    legacy.ptr_ = reinterpret_cast<const char *>(&expected[i]);
    legacy.pack_ = sizeof(int64_t);
    ASSERT_EQ(OB_SUCCESS, baseline[i].assign_legacy_borrowed(legacy, meta));
  }
  const ObRowkey reference(baseline, 3);
  EXPECT_EQ(0, key.cached_rowkey_->get_rowkey().compare(reference));
  EXPECT_EQ(reference.murmurhash(0), key.cached_rowkey_->murmurhash(0));
  ObStoreRowkey reference_store;
  ASSERT_EQ(OB_SUCCESS, reference_store.assign(baseline, 3));
  for (uint64_t seed : {uint64_t(0), uint64_t(37), UINT64_MAX}) {
    EXPECT_EQ(reference_store.murmurhash(seed), key.cached_rowkey_->murmurhash(seed));
  }
}
}

TEST(NativePointGetInput, concurrent_owners_share_only_immutable_layout_and_keep_independent_values)
{
  constexpr int64_t THREAD_COUNT = 8;
  constexpr int64_t REBINDS = 64;
  std::array<std::thread, THREAD_COUNT> threads;
  std::atomic<int64_t> arrived(0);
  std::atomic<bool> start(false);
  std::atomic<bool> correct(true);
  std::array<uintptr_t, THREAD_COUNT> value_addresses{};
  std::array<uintptr_t, THREAD_COUNT> storage_addresses{};
  std::array<uintptr_t, THREAD_COUNT> cached_addresses{};
  // Separate live owners are constructed by their worker threads before the
  // first init. The barrier also exercises concurrent first layout access.
  for (int64_t t = 0; t < THREAD_COUNT; ++t) {
    threads[t] = std::thread([&, t]() {
      NativePointGetInput owner;
      arrived.fetch_add(1, std::memory_order_release);
      while (!start.load(std::memory_order_acquire)) { std::this_thread::yield(); }
      for (int64_t bind = 0; bind < REBINDS; ++bind) {
        int64_t source[3];
        const int64_t expected[3] = {t * 1000 + bind, INT64_MIN + t, INT64_MAX - bind};
        for (int i = 0; i < 3; ++i) { source[i] = expected[i]; }
        const int64_t group = (t + 1) << 32;
        if (owner.init(source, group) != OB_SUCCESS || owner.lookup_keys().count() != 1) {
          correct.store(false, std::memory_order_relaxed);
          break;
        }
        MEMSET(source, 0xA5, sizeof(source));
        const auto &key = owner.lookup_keys().at(0);
        if (key.count_ != 3 || key.cached_rowkey_ == nullptr || key.group_id_ != group
            || key.cached_rowkey_->get_group_idx() != group) {
          correct.store(false, std::memory_order_relaxed);
          break;
        }
        for (int i = 0; i < 3; ++i) {
          const auto &legacy = key.cached_rowkey_->get_cell(i);
          if (owner.values()[i] != expected[i] || key.datums_[i].get_int() != expected[i]
              || legacy.get_int() != expected[i] || legacy.get_type() != ObIntType
              || legacy.get_datum().length() != 0
              || key.datums_[i].ptr_ != reinterpret_cast<const char *>(owner.values() + i)) {
            correct.store(false, std::memory_order_relaxed);
          }
        }
        value_addresses[t] = reinterpret_cast<uintptr_t>(owner.values());
        storage_addresses[t] = reinterpret_cast<uintptr_t>(key.datums_);
        cached_addresses[t] = reinterpret_cast<uintptr_t>(key.cached_rowkey_);
      }
      // Keep every owner alive while all workers finish checking their views.
      arrived.fetch_sub(1, std::memory_order_release);
      while (arrived.load(std::memory_order_acquire) != 0) { std::this_thread::yield(); }
    });
  }
  while (arrived.load(std::memory_order_acquire) != THREAD_COUNT) { std::this_thread::yield(); }
  start.store(true, std::memory_order_release);
  for (auto &thread : threads) { thread.join(); }
  ASSERT_TRUE(correct.load(std::memory_order_relaxed));
  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    ASSERT_NE(0U, value_addresses[i]);
    for (int64_t j = i + 1; j < THREAD_COUNT; ++j) {
      EXPECT_NE(value_addresses[i], value_addresses[j]);
      EXPECT_NE(storage_addresses[i], storage_addresses[j]);
      EXPECT_NE(cached_addresses[i], cached_addresses[j]);
    }
  }
}

TEST(NativePointGetInput, compact_values_survive_the_input_lifetime_and_report_full_owner_cost)
{
  NativePointGetInput owner;
  const int64_t expected[] = {INT64_MIN, -1, INT64_MAX};
  {
    int64_t source[3];
    for (int i = 0; i < 3; ++i) { source[i] = expected[i]; }
    ASSERT_EQ(OB_SUCCESS, owner.init(source, 0));
    EXPECT_NE(reinterpret_cast<const void *>(source), reinterpret_cast<const void *>(owner.values()));
    memset(static_cast<void *>(source), 0xA5, sizeof(source));
  }
  assert_same_key(owner, expected);
  EXPECT_EQ(24U, sizeof(int64_t) * NativePointGetInput::COLUMN_COUNT);
  // Keep complete-owner and compatibility-view costs in standard test XML;
  // 24B describes only the values, not the total owner or its shared type.
  RecordProperty("owner_bytes", static_cast<int>(sizeof(NativePointGetInput)));
  RecordProperty("value_bytes", 24);
  RecordProperty("storage_views_bytes", static_cast<int>(3 * sizeof(oceanbase::blocksstable::ObStorageDatum)));
  RecordProperty("legacy_views_bytes", static_cast<int>(3 * sizeof(DatumValue)));
  RecordProperty("cached_rowkey_bytes", static_cast<int>(sizeof(ObStoreRowkey)));
  RecordProperty("lookup_key_bytes", static_cast<int>(sizeof(oceanbase::storage::DatumLookupKey)));
  RecordProperty("array_view_bytes", static_cast<int>(sizeof(ObArrayHelper<oceanbase::storage::DatumLookupKey>)));
}

TEST(NativePointGetInput, source_overwrite_cannot_change_owned_inline_keys_or_their_cached_views)
{
  const int64_t expected[] = {INT64_MIN, 0, INT64_MAX};
  int64_t source[3];
  for (int i = 0; i < 3; ++i) { source[i] = expected[i]; }
  NativePointGetInput owner;
  EXPECT_EQ(0, owner.lookup_keys().count());
  const int64_t group_id = int64_t(7) << 32;
  ASSERT_EQ(OB_SUCCESS, owner.init(source, group_id));
  MEMSET(source, 0xA5, sizeof(source));
  assert_same_key(owner, expected);
  const auto &key = owner.lookup_keys().at(0);
  EXPECT_EQ(group_id, key.group_id_);
  EXPECT_EQ(group_id, key.cached_rowkey_->get_group_idx());
}

TEST(NativePointGetInput, rebind_after_consumers_finish_refreshes_views_and_cached_hash)
{
  int64_t source[3];
  NativePointGetInput owner;
  const int64_t first[] = {1, 2, 3};
  for (int i = 0; i < 3; ++i) { source[i] = first[i]; }
  ASSERT_EQ(OB_SUCCESS, owner.init(source, 0));
  assert_same_key(owner, first);
  const auto *storage = owner.lookup_keys().at(0).datums_;
  const auto *cached = owner.lookup_keys().at(0).cached_rowkey_;
  const int64_t second[] = {INT64_MAX, -19, INT64_MIN};
  for (int i = 0; i < 3; ++i) { source[i] = second[i]; }
  ASSERT_EQ(OB_SUCCESS, owner.init(source, int64_t(11) << 32));
  EXPECT_EQ(storage, owner.lookup_keys().at(0).datums_);
  EXPECT_EQ(cached, owner.lookup_keys().at(0).cached_rowkey_);
  assert_same_key(owner, second);
}

TEST(NativePointGetInput, invalid_columns_do_not_publish_or_replace_an_existing_key)
{
  // Invalid states belong to the real extraction boundary. The owner receives
  // only the raw I64 domain after this provider accepts every column.
  ObArenaAllocator input_owner;
  ObPreRangeGraph graph(input_owner);
  ObRangeNode node(input_owner);
  ObColumnRefRawExpr columns[3];
  ObSEArray<ColumnItem, 3> items;
  int64_t start[3] = {0, 1, 2}, end[3] = {0, 1, 2};
  for (int64_t i = 0; i < 3; ++i) {
    columns[i].set_ref_id(1001, 16 + i);
    columns[i].set_data_type(ObIntType);
    ColumnItem item; item.expr_ = &columns[i]; item.set_ref_id(1001, 16 + i);
    ASSERT_EQ(OB_SUCCESS, items.push_back(item));
  }
  ASSERT_EQ(OB_SUCCESS, graph.fill_column_metas(items));
  ASSERT_EQ(OB_SUCCESS, graph.get_range_map().expr_final_infos_.prepare_allocate(3));
  for (int64_t i = 0; i < 3; ++i) {
    auto &info = graph.get_range_map().expr_final_infos_.at(i);
    info.is_param_ = true; info.param_idx_ = i;
  }
  node.column_cnt_ = 3; node.start_keys_ = start; node.end_keys_ = end;
  node.min_offset_ = 0; node.max_offset_ = 2;
  node.include_start_ = true; node.include_end_ = true;
  graph.set_range_head(&node); graph.set_node_count(1);
  graph.set_is_precise_get(true); graph.set_is_standard_range(true);
  graph.set_is_equal_range(true); graph.set_is_get(true);

  const int64_t expected[] = {17, -23, 31};
  for (int column = 0; column < 3; ++column) {
    for (int state = 0; state < 9; ++state) {
      DatumParamStore source((ObWrapperAllocator(&input_owner)));
      for (int i = 0; i < 3; ++i) {
        DatumParameter parameter;
        parameter.get_meta().set_int();
        parameter.datum_.set_int(expected[i]);
        ASSERT_EQ(OB_SUCCESS, source.push_back(parameter));
      }
      NativePointGetInput owner, empty;
      int64_t values[3] = {101, 102, 103};
      bool supported = false;
      ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(source, values, supported));
      ASSERT_TRUE(supported);
      ASSERT_EQ(OB_SUCCESS, owner.init(values, int64_t(5) << 32));
      const auto *published_values = owner.values();
      const auto *published_storage = owner.lookup_keys().at(0).datums_;
      const auto *published_cached = owner.lookup_keys().at(0).cached_rowkey_;
      const uint64_t published_hash = published_cached->murmurhash(0);
      // All otherwise-valid columns now differ. A partial prefix publish
      // must fail even when the invalid cell is the final column.
      for (int i = 0; i < 3; ++i) { source.at(i).datum_.set_int(INT64_MIN + i); }
      char bytes[8] = {};
      Datum &invalid = source.at(column).datum_;
      switch (state) {
        case 0: invalid.set_null(); break;
        case 1: invalid.set_min(); break;
        case 2: invalid.set_max(); break;
        case 3: invalid.set_nop(); break;
        case 4: ASSERT_EQ(OB_SUCCESS, invalid.set_reference(bytes, sizeof(bytes))); break;
        case 5: ASSERT_EQ(OB_SUCCESS, invalid.set_reference(nullptr, 0, Datum::OutRow)); break;
        case 6: ASSERT_EQ(OB_SUCCESS, invalid.set_reference(nullptr, 0, Datum::HasLobHeader)); break;
        case 7: ASSERT_EQ(OB_SUCCESS, invalid.set_reference(nullptr, 0, Datum::LegacyExtended)); break;
        default: ASSERT_EQ(OB_SUCCESS, invalid.set_reference(nullptr, 0,
                     Datum::OutRow | Datum::HasLobHeader)); break;
      }
      ASSERT_EQ(OB_SUCCESS, graph.try_get_native_bigint_point(source, values, supported));
      if (supported) {
        EXPECT_EQ(OB_SUCCESS, empty.init(values, 0));
        EXPECT_EQ(OB_SUCCESS, owner.init(values, 0));
      }
      EXPECT_FALSE(supported);
      for (int i = 0; i < 3; ++i) { EXPECT_EQ(expected[i], values[i]); }
      EXPECT_EQ(0, empty.lookup_keys().count());
      assert_same_key(owner, expected);
      EXPECT_EQ(published_values, owner.values());
      EXPECT_EQ(published_storage, owner.lookup_keys().at(0).datums_);
      EXPECT_EQ(published_cached, owner.lookup_keys().at(0).cached_rowkey_);
      EXPECT_EQ(published_hash, published_cached->murmurhash(0));
      EXPECT_EQ(int64_t(5) << 32, owner.lookup_keys().at(0).group_id_);
      EXPECT_EQ(int64_t(5) << 32, published_cached->get_group_idx());
    }
  }
}

TEST(NativePointGetInput, compact_keys_preserve_signed_lexicographic_order_and_hash)
{
  const int64_t sorted[][3] = {
    {INT64_MIN, INT64_MAX, INT64_MAX}, {-1, INT64_MAX, INT64_MAX},
    {0, INT64_MIN, INT64_MAX}, {0, 0, INT64_MIN}, {0, 0, -1},
    {0, 0, 0}, {0, 0, INT64_MAX}, {0, 1, INT64_MIN}, {INT64_MAX, INT64_MIN, INT64_MIN}
  };
  NativePointGetInput left, right;
  int64_t source[3];
  for (int i = 0; i < static_cast<int>(sizeof(sorted) / sizeof(sorted[0])) - 1; ++i) {
    for (int column = 0; column < 3; ++column) { source[column] = sorted[i][column]; }
    ASSERT_EQ(OB_SUCCESS, left.init(source, int64_t(3) << 32));
    assert_same_key(left, sorted[i]);
    for (int column = 0; column < 3; ++column) { source[column] = sorted[i + 1][column]; }
    ASSERT_EQ(OB_SUCCESS, right.init(source, int64_t(9) << 32));
    assert_same_key(right, sorted[i + 1]);
    const auto &a = left.lookup_keys().at(0).cached_rowkey_->get_rowkey();
    const auto &b = right.lookup_keys().at(0).cached_rowkey_->get_rowkey();
    EXPECT_LT(a.compare(b), 0);
    EXPECT_GT(b.compare(a), 0);
  }
}

TEST(NativePointGetInput, legacy_borrow_uses_inline_scalar_bytes_after_unaligned_import)
{
  SqlType type;
  prepare_scalar_type(type, ObIntType, 20, 0);
  const DatumCopyPlan plan(type);
  alignas(8) char input[sizeof(int64_t) + 1];
  const int64_t expected = INT64_MIN;
  memcpy(input + 1, &expected, sizeof(expected));
  ObDatum legacy;
  legacy.ptr_ = input + 1; legacy.pack_ = sizeof(expected);
  Datum value;
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy, value));
  memset(input, 0, sizeof(input));
  ObDatum borrowed;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_legacy(value, borrowed));
  const uintptr_t begin = reinterpret_cast<uintptr_t>(&value);
  const uintptr_t address = reinterpret_cast<uintptr_t>(borrowed.ptr_);
  EXPECT_GE(address, begin); EXPECT_LE(address + sizeof(expected), begin + sizeof(value));
  EXPECT_EQ(0U, address % alignof(int64_t));
  EXPECT_EQ(expected, borrowed.get_int());
  EXPECT_EQ(sizeof(expected), borrowed.len_);
}

TEST(NativePointGetInput, null_can_borrow_but_wrapped_markers_leave_the_output_unchanged)
{
  SqlType type;
  prepare_scalar_type(type, ObIntType, 20, 0);
  const DatumCopyPlan plan(type);
  Datum value;
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_legacy(value, output));
  EXPECT_TRUE(output.is_null());
  int64_t previous = 71;
  output.ptr_ = reinterpret_cast<const char *>(&previous); output.pack_ = sizeof(previous);
  for (int marker = 0; marker < 3; ++marker) {
    if (marker == 0) { value.set_min(); }
    else if (marker == 1) { value.set_max(); }
    else { value.set_nop(); }
    EXPECT_EQ(OB_NOT_SUPPORTED, plan.borrow_legacy(value, output));
    EXPECT_EQ(reinterpret_cast<const char *>(&previous), output.ptr_);
    EXPECT_EQ(71, output.get_int()); EXPECT_EQ(sizeof(previous), output.len_);
  }
}

TEST(NativePointGetInput, referenced_decimal_borrows_only_without_coefficient_normalization)
{
  SqlType wide, unknown;
  prepare_scalar_type(wide, ObDecimalIntType, 20, 5);
  prepare_scalar_type(unknown, ObDecimalIntType, PRECISION_UNKNOWN_YET, 5);
  const DatumCopyPlan wide_plan(wide), unknown_plan(unknown);
  alignas(8) char bytes[65] = {};
  Datum value;
  ASSERT_EQ(OB_SUCCESS, value.set_reference(bytes + 1, 16));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, wide_plan.borrow_legacy(value, output));
  EXPECT_EQ(bytes + 1, output.ptr_); EXPECT_EQ(16U, output.len_);
  int64_t previous = 97;
  for (uint32_t width : {0U, 4U, 8U, 32U, 64U}) {
    ASSERT_EQ(OB_SUCCESS, value.set_reference(bytes + 1, width));
    output.ptr_ = reinterpret_cast<const char *>(&previous); output.pack_ = sizeof(previous);
    EXPECT_EQ(OB_NOT_SUPPORTED, wide_plan.borrow_legacy(value, output));
    EXPECT_EQ(reinterpret_cast<const char *>(&previous), output.ptr_);
    EXPECT_EQ(97, output.get_int());
    ASSERT_EQ(OB_SUCCESS, unknown_plan.borrow_legacy(value, output));
    EXPECT_EQ(bytes + 1, output.ptr_); EXPECT_EQ(width, output.len_);
  }
}

TEST(NativePointGetInput, narrow_scalar_borrow_uses_the_matching_member_bytes)
{
  SqlType decimal, floating;
  prepare_scalar_type(decimal, ObDecimalIntType, 9, 2);
  prepare_scalar_type(floating, ObFloatType);
  const DatumCopyPlan decimal_plan(decimal), float_plan(floating);
  Datum value;
  ObDatum output;
  value.set_int(INT32_MIN);
  ASSERT_EQ(OB_SUCCESS, decimal_plan.borrow_legacy(value, output));
  int32_t coefficient = 0; memcpy(&coefficient, output.ptr_, sizeof(coefficient));
  EXPECT_EQ(INT32_MIN, coefficient); EXPECT_EQ(sizeof(coefficient), output.len_);
  value.set_float(-1.25F);
  ASSERT_EQ(OB_SUCCESS, float_plan.borrow_legacy(value, output));
  float scalar = 0; memcpy(&scalar, output.ptr_, sizeof(scalar));
  EXPECT_EQ(-1.25F, scalar); EXPECT_EQ(sizeof(scalar), output.len_);
}

TEST(NativePointGetInput, borrowed_string_keeps_the_producers_bytes_and_scalar_shape_failures_are_atomic)
{
  SqlType text, integer;
  prepare_scalar_type(text, ObVarcharType);
  prepare_scalar_type(integer, ObIntType, 20, 0);
  const DatumCopyPlan text_plan(text), integer_plan(integer);
  const char bytes[] = {'a', '\0', 'b'};
  Datum value;
  ASSERT_EQ(OB_SUCCESS, value.set_reference(bytes, sizeof(bytes)));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, text_plan.borrow_legacy(value, output));
  EXPECT_EQ(bytes, output.ptr_); EXPECT_EQ(sizeof(bytes), output.len_);
  EXPECT_EQ(0, memcmp(bytes, output.ptr_, sizeof(bytes)));
  const char *previous = output.ptr_;
  const uint32_t descriptor = output.pack_;
  EXPECT_NE(OB_SUCCESS, integer_plan.borrow_legacy(value, output));
  EXPECT_EQ(previous, output.ptr_); EXPECT_EQ(descriptor, output.pack_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
