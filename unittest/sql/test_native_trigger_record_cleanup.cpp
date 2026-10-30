#include "sql/engine/ob_exec_context.h"
#include "sql/engine/dml/ob_trigger_handler.h"
#include "sql/pl/ob_pl_user_type.h"
#include "lib/allocator/page_arena.h"
#include <gtest/gtest.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;

namespace {
ObPLRecord *make_record(ObIAllocator &owner, int count, bool initialize)
{
  void *memory = owner.alloc(ObRecordType::get_init_size(count));
  if (memory == nullptr) { return nullptr; }
  ObPLRecord *record = new (memory) ObPLRecord(OB_INVALID_ID, count);
  if (initialize && OB_SUCCESS != record->init_data(owner, true)) { return nullptr; }
  return record;
}
void fill_record(ObPLRecord &record, int64_t number)
{
  ASSERT_NE(nullptr, record.get_allocator());
  ASSERT_NE(nullptr, record.get_element());
  ASSERT_EQ(OB_SUCCESS, record.get_element()[0].set_int(number, *record.get_allocator()));
  ASSERT_EQ(OB_SUCCESS, record.get_element()[1].set_varchar(ObString("row data"), *record.get_allocator()));
}
}

TEST(NativeTriggerRecordCleanup, both_records_can_be_reused_then_released)
{
  ObArenaAllocator owner;
  ObTrigDMLRtDef rt;
  rt.old_record_ = make_record(owner, 2, true);
  rt.new_record_ = make_record(owner, 2, true);
  ASSERT_NE(nullptr, rt.old_record_);
  ASSERT_NE(nullptr, rt.new_record_);
  auto *old_data = rt.old_record_->get_element();
  auto *new_data = rt.new_record_->get_element();
  auto *old_allocator = rt.old_record_->get_allocator();
  auto *new_allocator = rt.new_record_->get_allocator();
  fill_record(*rt.old_record_, 1);
  fill_record(*rt.new_record_, 2);
  ASSERT_EQ(OB_SUCCESS, TriggerHandle::free_trigger_param_memory(rt, true));
  EXPECT_EQ(old_data, rt.old_record_->get_element());
  EXPECT_EQ(new_data, rt.new_record_->get_element());
  EXPECT_EQ(old_allocator, rt.old_record_->get_allocator());
  EXPECT_EQ(new_allocator, rt.new_record_->get_allocator());
  for (int i = 0; i < 2; ++i) {
    EXPECT_TRUE(rt.old_record_->get_element()[i].is_null());
    EXPECT_TRUE(rt.new_record_->get_element()[i].is_null());
  }
  fill_record(*rt.old_record_, 3);
  fill_record(*rt.new_record_, 4);
  ASSERT_EQ(OB_SUCCESS, TriggerHandle::free_trigger_param_memory(rt, false));
  EXPECT_EQ(nullptr, rt.old_record_->get_element());
  EXPECT_EQ(nullptr, rt.new_record_->get_element());
  EXPECT_EQ(nullptr, rt.old_record_->get_allocator());
  EXPECT_EQ(nullptr, rt.new_record_->get_allocator());
  EXPECT_EQ(0, rt.old_record_->get_count());
  EXPECT_EQ(0, rt.new_record_->get_count());
}

TEST(NativeTriggerRecordCleanup, absent_and_uninitialized_records_are_safe)
{
  ObArenaAllocator owner;
  ObTrigDMLRtDef rt;
  ASSERT_EQ(OB_SUCCESS, TriggerHandle::free_trigger_param_memory(rt, false));
  rt.old_record_ = make_record(owner, 1, false);
  ASSERT_NE(nullptr, rt.old_record_);
  ASSERT_EQ(OB_SUCCESS, TriggerHandle::free_trigger_param_memory(rt, false));
  EXPECT_EQ(nullptr, rt.old_record_->get_allocator());
  EXPECT_EQ(nullptr, rt.old_record_->get_element());
  EXPECT_EQ(1, rt.old_record_->get_count());
}

TEST(NativeTriggerRecordCleanup, unsupported_record_subtype_is_left_unchanged)
{
  ObArenaAllocator owner;
  ObTrigDMLRtDef rt;
  rt.old_record_ = make_record(owner, 2, true);
  ASSERT_NE(nullptr, rt.old_record_);
  fill_record(*rt.old_record_, 19);
  auto *data = rt.old_record_->get_element();
  auto *allocator = rt.old_record_->get_allocator();
  rt.old_record_->set_type(PL_NESTED_TABLE_TYPE);
  // The original wrapper ignored the destructor's unsupported-subtype error.
  ASSERT_EQ(OB_SUCCESS, TriggerHandle::free_trigger_param_memory(rt, false));
  EXPECT_EQ(data, rt.old_record_->get_element());
  EXPECT_EQ(allocator, rt.old_record_->get_allocator());
  EXPECT_EQ(19, rt.old_record_->get_element()[0].get_int());
  rt.old_record_->set_type(PL_RECORD_TYPE);
  ASSERT_EQ(OB_SUCCESS, TriggerHandle::free_trigger_param_memory(rt, false));
  EXPECT_EQ(nullptr, rt.old_record_->get_allocator());
}
