/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/dml/ob_trigger_handler.h"
#include "sql/pl/ob_pl_user_type.h"
#include "common/datum/datum_parameter_array.h"
#include "lib/allocator/page_arena.h"
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <gtest/gtest.h>
using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;
namespace oceanbase { namespace sql {
struct NativeTriggerParameterTestAccess {
  static int stabilize(const DatumParameter *initial, int64_t count, ParamStore &params,
                       ObIAllocator &temporary_owner)
  { return TriggerHandle::stabilize_trigger_params(initial, count, params, temporary_owner, nullptr); }
  static int convert_sql_geometry(const bool has_session, const bool has_schema_guard,
                                  const DatumValue &source, DatumValue &destination)
  { return TriggerHandle::convert_sql_type_to_pl_type(has_session, has_schema_guard, source, destination); }
  static int convert_pl_geometry(const DatumValue &source, DatumValue &destination)
  { return TriggerHandle::convert_pl_type_to_sql_type(source, destination, ObGeometryType); }
};
} }
namespace {
using LiveBlocks = std::unordered_map<void *, int64_t>;
bool contains_live(const LiveBlocks &blocks, const void *pointer, int64_t length)
{
  if (pointer == nullptr || length < 0) { return false; }
  const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
  for (const auto &block : blocks) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(block.first);
    if (address >= begin && address - begin <= static_cast<uint64_t>(block.second)
        && static_cast<uint64_t>(length) <= static_cast<uint64_t>(block.second) - (address - begin)) {
      return true;
    }
  }
  return false;
}
class CountingOwner : public ObIAllocator {
public:
  ObArenaAllocator arena; int64_t calls = 0; bool reject = false;
  void *alloc(int64_t size) override {
    ++calls; void *p = reject ? nullptr : arena.alloc(size);
    if (p != nullptr) { live_[p] = size; }
    return p;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override {
    if (p != nullptr) {
      auto found = live_.find(p);
      if (found == live_.end()) { ADD_FAILURE() << "foreign or double free"; }
      else { live_.erase(found); arena.free(p); }
    }
  }
  bool owns_live(const void *pointer, int64_t length) const { return contains_live(live_, pointer, length); }
private:
  LiveBlocks live_;
};
class ReclaimingOwner : public ObIAllocator {
public:
  ~ReclaimingOwner() override { reset(); }
  int64_t allocations_left = -1;
  void *alloc(int64_t size) override {
    if (size < 0 || allocations_left == 0) { return nullptr; }
    if (allocations_left > 0) { --allocations_left; }
    void *p = std::malloc(size > 0 ? size : 1);
    if (p != nullptr) { live_[p] = size > 0 ? size : 1; }
    return p;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override {
    if (p != nullptr) {
      auto found = live_.find(p);
      if (found == live_.end()) { ADD_FAILURE() << "foreign or double free"; }
      else { std::memset(p, 0xcc, found->second); live_.erase(found); std::free(p); }
    }
  }
  void reset() { while (!live_.empty()) { free(live_.begin()->first); } }
  int64_t outstanding() const { return live_.size(); }
  bool owns_live(const void *pointer, int64_t length) const { return contains_live(live_, pointer, length); }
private:
  LiveBlocks live_;
};
// This predicate never reads a payload until its entire 15-byte descriptor is
// known live. Callers use ASSERT_TRUE, so a failed predicate stops the TEST
// before any get_ext(), Record dereference, destructor or real helper call.
template <class DescriptorOwner, class RecordOwner>
bool live_record_parameter(const DatumParameter &value, const DescriptorOwner &descriptor_owner,
                           const RecordOwner &record_owner)
{
  if (!value.datum_.is_native_extend()
      || !descriptor_owner.owns_live(value.datum_.ptr_, DATUM_EXTEND_PAYLOAD_SIZE)
      || value.get_extend_type() != PL_RECORD_TYPE
      || value.datum_.get_extend_type() != PL_RECORD_TYPE) { return false; }
  return record_owner.owns_live(reinterpret_cast<const void *>(value.get_ext()), sizeof(ObPLRecord));
}
ObPLRecord *make_record(ObIAllocator &owner, int count, int64_t number, bool separate_allocator = true)
{
  auto *record = static_cast<ObPLRecord *>(owner.alloc(ObRecordType::get_init_size(count)));
  if (record == nullptr) { return nullptr; }
  new (record) ObPLRecord(OB_INVALID_ID, count);
  if (record->init_data(owner, separate_allocator) != OB_SUCCESS) { return nullptr; }
  for (int i = 0; i < count; ++i) {
    if (record->get_element()[i].set_int(number + i, *record->get_allocator()) != OB_SUCCESS) { return nullptr; }
  }
  return record;
}
int describe(DatumParameter &value, ObPLRecord &record, ObIAllocator &owner)
{
  const int ret = value.set_extend(reinterpret_cast<int64_t>(&record), PL_RECORD_TYPE, owner, record.get_init_size());
  if (ret == OB_SUCCESS) { value.set_param_meta(); }
  return ret;
}
template <class Owner> bool poison(Owner &owner)
{
  owner.reset(); void *p = owner.alloc(64 * 1024);
  if (p == nullptr) { return false; }
  std::memset(p, 0xcc, 64 * 1024); return true;
}
}
TEST(NativeTriggerParameterLifetime, repeated_real_copyout_reuses_initial_descriptors_with_no_dml_allocation)
{
  CountingOwner dml; ParamStore params{ObWrapperAllocator(dml)};
  ASSERT_EQ(OB_SUCCESS, params.prepare_allocate(2)); DatumParameter initial[2];
  for (int i = 0; i < 2; ++i) {
    auto *record = make_record(dml, 2, 70 + i); ASSERT_NE(nullptr, record);
    ASSERT_EQ(OB_SUCCESS, describe(params.at(i), *record, dml)); initial[i] = params.at(i);
    ASSERT_TRUE(live_record_parameter(initial[i], dml, dml));
  }
  const int64_t initial_allocations = dml.calls;
  dml.reject = true; // The normal path must not allocate from the DML owner.
  for (int repetition = 0; repetition < 128; ++repetition) {
    ReclaimingOwner temporary; DatumParameter callee[2], published_snapshot[2];
    for (int i = 0; i < 2; ++i) {
      ASSERT_TRUE(live_record_parameter(params.at(i), dml, dml));
      callee[i] = params.at(i); callee[i].set_udt_id(1234 + repetition);
      callee[i].set_result_flags(0x1357); callee[i].set_raw_text_info(31, 47);
      callee[i].set_need_to_check_type(true); callee[i].set_expected_bool_value(true);
      ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(temporary, callee[i], params.at(i)));
      ASSERT_TRUE(live_record_parameter(params.at(i), temporary, dml));
      ASSERT_NE(initial[i].datum_.ptr_, params.at(i).datum_.ptr_);
      params.at(i).datum_.set_extend(params.at(i).get_ext(), PL_RECORD_TYPE,
          params.at(i).get_val_len(), 0xa7, 0xef);
      // Compare the actual copy-out publication, not a presumed clone policy.
      // Its metadata is inline; never read its borrowed payload after poison.
      published_snapshot[i] = params.at(i);
    }
    ASSERT_EQ(OB_SUCCESS, NativeTriggerParameterTestAccess::stabilize(initial, 2, params, temporary));
    ASSERT_TRUE(poison(temporary));
    for (int i = 0; i < 2; ++i) {
      ASSERT_TRUE(live_record_parameter(initial[i], dml, dml));
      ASSERT_TRUE(live_record_parameter(params.at(i), dml, dml));
      ASSERT_EQ(initial[i].datum_.ptr_, params.at(i).datum_.ptr_);
      ASSERT_EQ(initial[i].get_ext(), params.at(i).get_ext());
      EXPECT_EQ(0xa7, params.at(i).datum_.get_extend_collation_level());
      EXPECT_EQ(0xef, params.at(i).datum_.get_extend_collation());
      EXPECT_EQ(published_snapshot[i].get_accuracy(), params.at(i).get_accuracy());
      EXPECT_EQ(0, std::memcmp(&published_snapshot[i].get_meta(), &params.at(i).get_meta(), sizeof(SqlTypeMeta)));
      EXPECT_EQ(0, std::memcmp(&published_snapshot[i].get_param_meta(), &params.at(i).get_param_meta(), sizeof(SqlTypeMeta)));
      // ParamFlag has virtual UNIS methods. Compare its two flag bytes,
      // excluding vptr/padding from object-representation comparisons.
      EXPECT_EQ(published_snapshot[i].get_param_flag().flag_, params.at(i).get_param_flag().flag_);
      EXPECT_EQ(published_snapshot[i].get_param_flag().extend_flag_, params.at(i).get_param_flag().extend_flag_);
      EXPECT_EQ(published_snapshot[i].get_result_flag(), params.at(i).get_result_flag());
      EXPECT_EQ(published_snapshot[i].get_raw_text_pos(), params.at(i).get_raw_text_pos());
      EXPECT_EQ(published_snapshot[i].get_raw_text_len(), params.at(i).get_raw_text_len());
      EXPECT_EQ(70 + i, reinterpret_cast<ObPLRecord *>(params.at(i).get_ext())->get_element()[0].get_int());
    }
  }
  EXPECT_EQ(initial_allocations, dml.calls); dml.reject = false;
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(live_record_parameter(params.at(i), dml, dml));
    ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(dml, params.at(i)));
  }
}

TEST(NativeTriggerGeometryCast, preserves_current_unsupported_cast_behavior_natively)
{
  static const char wkb[] = "wkb";
  DatumValue geometry;
  geometry.meta_.set_type(ObGeometryType);
  geometry.meta_.set_collation_type(CS_TYPE_BINARY);
  geometry.datum_.set_string(ObString(sizeof(wkb) - 1, wkb));

  DatumValue pl_value;
  EXPECT_EQ(OB_NOT_SUPPORTED,
      NativeTriggerParameterTestAccess::convert_sql_geometry(true, true, geometry, pl_value));
  EXPECT_EQ(OB_NOT_SUPPORTED,
      NativeTriggerParameterTestAccess::convert_pl_geometry(geometry, pl_value));

  DatumValue null_geometry;
  null_geometry.meta_.set_type(ObGeometryType);
  null_geometry.meta_.set_collation_type(CS_TYPE_BINARY);
  EXPECT_EQ(OB_SUCCESS,
      NativeTriggerParameterTestAccess::convert_sql_geometry(true, true, null_geometry, pl_value));
  EXPECT_TRUE(pl_value.is_null());
  EXPECT_EQ(OB_ERR_UNEXPECTED,
      NativeTriggerParameterTestAccess::convert_sql_geometry(true, false, null_geometry, pl_value));
  EXPECT_EQ(OB_SUCCESS,
      NativeTriggerParameterTestAccess::convert_pl_geometry(null_geometry, pl_value));
  EXPECT_TRUE(pl_value.is_null());
}
TEST(NativeTriggerParameterLifetime, replacement_record_is_copied_back_to_original_identity)
{
  CountingOwner dml; ReclaimingOwner temporary; ParamStore params{ObWrapperAllocator(dml)};
  ASSERT_EQ(OB_SUCCESS, params.prepare_allocate(1));
  auto *original = make_record(dml, 2, 1); ASSERT_NE(nullptr, original);
  ASSERT_EQ(OB_SUCCESS, describe(params.at(0), *original, dml)); const DatumParameter initial = params.at(0);
  ASSERT_TRUE(live_record_parameter(initial, dml, dml));
  auto *replacement = make_record(temporary, 2, 90); ASSERT_NE(nullptr, replacement);
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, describe(source, *replacement, temporary)); source.set_udt_id(4567);
  ASSERT_TRUE(live_record_parameter(source, temporary, temporary));
  const int64_t source_allocations = temporary.outstanding();
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(temporary, source, params.at(0)));
  ASSERT_TRUE(live_record_parameter(params.at(0), temporary, temporary));
  ASSERT_NE(initial.get_ext(), params.at(0).get_ext());
  ASSERT_EQ(OB_SUCCESS, NativeTriggerParameterTestAccess::stabilize(&initial, 1, params, temporary));
  ASSERT_TRUE(live_record_parameter(params.at(0), dml, dml));
  ASSERT_EQ(initial.datum_.ptr_, params.at(0).datum_.ptr_); ASSERT_EQ(initial.get_ext(), params.at(0).get_ext());
  EXPECT_EQ(90, original->get_element()[0].get_int()); EXPECT_EQ(91, original->get_element()[1].get_int());
  EXPECT_EQ(source.get_udt_id(), params.at(0).get_udt_id());
  EXPECT_EQ(source_allocations, temporary.outstanding());
  ASSERT_TRUE(live_record_parameter(source, temporary, temporary));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(temporary, source));
  EXPECT_EQ(0, temporary.outstanding());
  ASSERT_TRUE(poison(temporary));
  ASSERT_TRUE(live_record_parameter(params.at(0), dml, dml));
  EXPECT_EQ(90, reinterpret_cast<ObPLRecord *>(params.at(0).get_ext())->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(dml, params.at(0)));
}
TEST(NativeTriggerParameterLifetime, rejected_replacement_restores_every_initial_descriptor)
{
  CountingOwner dml; ReclaimingOwner temporary; ParamStore params{ObWrapperAllocator(dml)};
  ASSERT_EQ(OB_SUCCESS, params.prepare_allocate(2)); DatumParameter initial[2];
  for (int i = 0; i < 2; ++i) {
    auto *record = make_record(dml, 2, 30 + i); ASSERT_NE(nullptr, record);
    ASSERT_EQ(OB_SUCCESS, describe(params.at(i), *record, dml)); initial[i] = params.at(i);
    ASSERT_TRUE(live_record_parameter(initial[i], dml, dml));
  }
  auto *wrong_count = make_record(temporary, 3, 90); ASSERT_NE(nullptr, wrong_count);
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, describe(source, *wrong_count, temporary));
  ASSERT_TRUE(live_record_parameter(source, temporary, temporary));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(temporary, source, params.at(0)));
  ASSERT_TRUE(live_record_parameter(params.at(0), temporary, temporary));
  params.at(1).set_null(); // An invalid OUT must not strand the other slot.
  ASSERT_EQ(OB_ERR_UNEXPECTED, NativeTriggerParameterTestAccess::stabilize(initial, 2, params, temporary));
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(live_record_parameter(params.at(i), dml, dml));
    ASSERT_EQ(initial[i].datum_.ptr_, params.at(i).datum_.ptr_); ASSERT_EQ(initial[i].get_ext(), params.at(i).get_ext());
    EXPECT_EQ(30 + i, reinterpret_cast<ObPLRecord *>(params.at(i).get_ext())->get_element()[0].get_int());
  }
  ASSERT_TRUE(live_record_parameter(source, temporary, temporary));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(temporary, source));
  EXPECT_EQ(0, temporary.outstanding());
  ASSERT_TRUE(poison(temporary));
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(live_record_parameter(params.at(i), dml, dml));
    ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(dml, params.at(i)));
  }
}
TEST(NativeTriggerParameterLifetime, replacement_copy_oom_restores_descriptor_and_retires_temporary_record)
{
  CountingOwner dml; ReclaimingOwner temporary; ParamStore params{ObWrapperAllocator(dml)};
  ASSERT_EQ(OB_SUCCESS, params.prepare_allocate(1));
  auto *original = make_record(dml, 2, 10, false); ASSERT_NE(nullptr, original);
  ASSERT_EQ(OB_SUCCESS, describe(params.at(0), *original, dml)); const DatumParameter initial = params.at(0);
  ASSERT_TRUE(live_record_parameter(initial, dml, dml));
  auto *replacement = make_record(temporary, 2, 90); ASSERT_NE(nullptr, replacement);
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, describe(source, *replacement, temporary));
  ASSERT_TRUE(live_record_parameter(source, temporary, temporary));
  const int64_t source_allocations = temporary.outstanding();
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(temporary, source, params.at(0)));
  ASSERT_TRUE(live_record_parameter(params.at(0), temporary, temporary));
  dml.reject = true;
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, NativeTriggerParameterTestAccess::stabilize(&initial, 1, params, temporary));
  dml.reject = false;
  ASSERT_TRUE(live_record_parameter(params.at(0), dml, dml));
  ASSERT_EQ(initial.datum_.ptr_, params.at(0).datum_.ptr_);
  ASSERT_EQ(initial.get_ext(), params.at(0).get_ext());
  EXPECT_EQ(10, original->get_element()[0].get_int());
  EXPECT_EQ(source_allocations, temporary.outstanding());
  ASSERT_TRUE(live_record_parameter(source, temporary, temporary));
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(temporary, source));
  EXPECT_EQ(0, temporary.outstanding());
  ASSERT_TRUE(poison(temporary));
  ASSERT_TRUE(live_record_parameter(params.at(0), dml, dml));
  EXPECT_EQ(10, reinterpret_cast<ObPLRecord *>(params.at(0).get_ext())->get_element()[0].get_int());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(dml, params.at(0)));
}
TEST(NativeTriggerParameterLifetime, partial_out_failure_retires_published_and_preserves_untouched_slots)
{
  CountingOwner dml; ReclaimingOwner temporary; ParamStore params{ObWrapperAllocator(dml)};
  ASSERT_EQ(OB_SUCCESS, params.prepare_allocate(2)); DatumParameter initial[2];
  for (int i = 0; i < 2; ++i) {
    auto *record = make_record(dml, 2, 50 + i); ASSERT_NE(nullptr, record);
    ASSERT_EQ(OB_SUCCESS, describe(params.at(i), *record, dml)); initial[i] = params.at(i);
    ASSERT_TRUE(live_record_parameter(initial[i], dml, dml));
  }
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::copy_out_parameter(temporary, initial[0], params.at(0)));
  ASSERT_TRUE(live_record_parameter(params.at(0), temporary, dml));
  temporary.allocations_left = 0;
  ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObUserDefinedType::copy_out_parameter(temporary, initial[1], params.at(1)));
  ASSERT_TRUE(live_record_parameter(params.at(1), dml, dml));
  ASSERT_EQ(initial[1].datum_.ptr_, params.at(1).datum_.ptr_);
  ASSERT_EQ(OB_SUCCESS, NativeTriggerParameterTestAccess::stabilize(initial, 2, params, temporary));
  temporary.allocations_left = -1; ASSERT_TRUE(poison(temporary));
  for (int i = 0; i < 2; ++i) {
    ASSERT_TRUE(live_record_parameter(params.at(i), dml, dml));
    ASSERT_EQ(initial[i].datum_.ptr_, params.at(i).datum_.ptr_);
    ASSERT_EQ(initial[i].get_ext(), params.at(i).get_ext());
    EXPECT_EQ(50 + i, reinterpret_cast<ObPLRecord *>(params.at(i).get_ext())->get_element()[0].get_int());
    ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(dml, params.at(i)));
  }
}
TEST(NativeTriggerParameterLifetime, malformed_descriptor_shape_is_rejected_before_payload_access)
{
  CountingOwner dml; ReclaimingOwner temporary; ParamStore params{ObWrapperAllocator(dml)};
  ASSERT_EQ(OB_SUCCESS, params.prepare_allocate(1));
  auto *original = make_record(dml, 2, 10); ASSERT_NE(nullptr, original);
  ASSERT_EQ(OB_SUCCESS, describe(params.at(0), *original, dml)); const DatumParameter initial = params.at(0);
  ASSERT_TRUE(live_record_parameter(initial, dml, dml));
  params.at(0).datum_.ptr_ = nullptr; // Metadata/length are still native Extend.
  ASSERT_EQ(OB_ERR_UNEXPECTED, NativeTriggerParameterTestAccess::stabilize(&initial, 1, params, temporary));
  ASSERT_TRUE(live_record_parameter(params.at(0), dml, dml));
  ASSERT_EQ(initial.datum_.ptr_, params.at(0).datum_.ptr_);
  ASSERT_EQ(initial.get_ext(), params.at(0).get_ext());
  ASSERT_EQ(OB_SUCCESS, ObUserDefinedType::destruct_objparam(dml, params.at(0)));
}
