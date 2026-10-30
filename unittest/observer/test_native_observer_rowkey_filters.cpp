/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "observer/virtual_table/rowkey_filter_value.h"
#include "observer/virtual_table/ob_all_virtual_diag_index_scan.h"
#include "observer/virtual_table/ob_virtual_table_iterator.h"
#include "observer/virtual_table/ob_all_virtual_tablet_sstable_macro_info.h"
#include "storage/tablet/ob_tablet_meta.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;
namespace {
std::vector<std::string> split(const std::string &text, char delimiter)
{
  std::vector<std::string> fields;
  size_t begin = 0;
  while (true) {
    const auto end = text.find(delimiter, begin);
    fields.push_back(text.substr(begin, end - begin));
    if (end == std::string::npos) { break; }
    begin = end + 1;
  }
  return fields;
}
std::string unhex(const std::string &text)
{
  if (text == "-") { return {}; }
  const char *digits = "0123456789abcdef";
  std::string bytes;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    const char *hi = std::strchr(digits, text[i]), *lo = std::strchr(digits, text[i + 1]);
    if (hi == nullptr || lo == nullptr) { ADD_FAILURE() << "bad fixture hex"; return {}; }
    bytes.push_back(static_cast<char>((hi - digits) * 16 + lo - digits));
  }
  return bytes;
}
DatumValue value_from(const std::string &hex, ObIAllocator &owner)
{
  const auto bytes = unhex(hex);
  DatumValue result;
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(bytes.data(), bytes.size(),
      pos, owner, result.datum_, result.meta_));
  EXPECT_EQ(bytes.size(), pos);
  return result;
}
void key_from(const std::string &hex, ObIAllocator &owner, ObRowkey &result)
{
  const auto bytes = unhex(hex);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, result.deserialize(owner, bytes.data(), bytes.size(), pos, true));
  ASSERT_EQ(bytes.size(), pos);
}
std::string wire(const ObRowkey &value)
{
  std::string bytes(value.get_serialize_size(), '\0');
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, value.serialize(bytes.data(), bytes.size(), pos));
  EXPECT_EQ(bytes.size(), pos);
  return bytes;
}
std::vector<std::vector<std::string>> fixture(const char *kind)
{
  std::ifstream input("unittest/observer/data/observer_rowkey_filters.golden");
  EXPECT_TRUE(input.good());
  std::vector<std::vector<std::string>> result;
  std::string line;
  while (std::getline(input, line)) {
    const auto fields = split(line, '\t');
    if (!fields.empty() && fields[0] == kind) { result.push_back(fields); }
  }
  return result;
}
struct ConvertTag {
  using type = int (ObVirtualTableIterator::*)(const ObRowkey &, ObRowkey &,
      ObIArray<const ObColumnSchemaV2 *> &);
  friend type access(ConvertTag);
};
struct TabletTag {
  using type = bool (ObAllVirtualTabletSSTableMacroInfo::*)(const ObTabletMeta &);
  friend type access(TabletTag);
};
struct SSTableTag {
  using type = bool (ObAllVirtualTabletSSTableMacroInfo::*)(const ObITable::TableKey &);
  friend type access(SSTableTag);
};
template<typename Tag, typename Tag::type Member> struct PrivateAccess {
  friend typename Tag::type access(Tag) { return Member; }
};
template struct PrivateAccess<ConvertTag, &ObVirtualTableIterator::convert_key>;
template struct PrivateAccess<TabletTag, &ObAllVirtualTabletSSTableMacroInfo::check_tablet_need_ignore>;
template struct PrivateAccess<SSTableTag, &ObAllVirtualTabletSSTableMacroInfo::check_sstable_need_ignore>;
class FilterIterator final : public ObVirtualTableIterator {
  int inner_get_next_row(ObNewRow *&) override { return OB_ITER_END; }
};
class NativeObserverKeyFilters : public testing::Test {
protected:
  ObSQLSessionInfo session;
  ObArenaAllocator owner;
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  }
};
TEST_F(NativeObserverKeyFilters, eq_and_ne_match_actual_old_operators_including_nan_and_collation)
{
  const auto rows = fixture("PRED");
  ASSERT_GT(rows.size(), 100);
  for (const auto &fields : rows) {
    ASSERT_EQ(6, fields.size()); SCOPED_TRACE(fields[1]);
    const DatumValue left = value_from(fields[2], owner), right = value_from(fields[3], owner);
    EXPECT_EQ(std::stoi(fields[4]) != 0, key_filter_equal(left, right));
    EXPECT_EQ(std::stoi(fields[5]) != 0, key_filter_not_equal(left, right));
  }
}
TEST_F(NativeObserverKeyFilters, actual_diag_filter_preserves_errors_dedup_and_partial_prior_ids)
{
  const auto rows = fixture("DIAG");
  ASSERT_EQ(14, rows.size());
  for (const auto &fields : rows) {
    ASSERT_EQ(5, fields.size()); SCOPED_TRACE(fields[1]);
    ObSEArray<ObNewRange, 4> ranges;
    if (fields[2] != "-") {
      for (const auto &pair : split(fields[2], ';')) {
        const auto keys = split(pair, '/'); ASSERT_EQ(2, keys.size());
        ObNewRange range;
        key_from(keys[0], owner, range.start_key_);
        key_from(keys[1], owner, range.end_key_);
        ASSERT_EQ(OB_SUCCESS, ranges.push_back(range));
      }
    }
    ObAllVirtualDiagIndexScan scan;
    EXPECT_EQ(std::stoi(fields[3]), scan.set_index_ids(ranges));
    std::string actual;
    for (int64_t i = 0; i < scan.get_index_ids().count(); ++i) {
      if (i > 0) { actual += ','; }
      actual += std::to_string(scan.get_index_ids().at(i));
    }
    EXPECT_EQ(fields[4] == "-" ? "" : fields[4], actual);
  }
}
TEST_F(NativeObserverKeyFilters, actual_convert_key_matches_old_cast_null_sentinels_and_error_publication)
{
  const auto rows = fixture("CONVERT");
  ASSERT_EQ(10, rows.size());
  for (const auto &fields : rows) {
    ASSERT_EQ(6, fields.size()); SCOPED_TRACE(fields[1]);
    ObRowkey input;
    key_from(fields[3], owner, input);
    ObColumnSchemaV2 columns[4];
    ObSEArray<const ObColumnSchemaV2 *, 4> targets;
    if (fields[2] != "-") {
      const auto types = split(fields[2], ','); ASSERT_LE(types.size(), 4);
      for (size_t i = 0; i < types.size(); ++i) {
        columns[i].set_data_type(static_cast<SqlValueType>(std::stoi(types[i])));
        ASSERT_EQ(OB_SUCCESS, targets.push_back(columns + i));
      }
    }
    FilterIterator iter;
    iter.set_allocator(&owner); iter.set_session(&session);
    DatumValue previous;
    ASSERT_EQ(OB_SUCCESS, previous.set_int(99, owner));
    ObRowkey output(&previous, 1);
    EXPECT_EQ(std::stoi(fields[4]), (iter.*access(ConvertTag{}))(input, output, targets));
    EXPECT_EQ(unhex(fields[5]), wire(output));
  }
}
TEST_F(NativeObserverKeyFilters, converted_payloads_own_bytes_and_failed_allocation_keeps_previous_key)
{
  const auto rows = fixture("CONVERT");
  ASSERT_EQ(10, rows.size());
  ObColumnSchemaV2 column;
  column.set_data_type(ObVarcharType);
  ObSEArray<const ObColumnSchemaV2 *, 1> targets;
  ASSERT_EQ(OB_SUCCESS, targets.push_back(&column));
  ObArenaAllocator source_owner;
  DatumValue input_value;
  ASSERT_EQ(OB_SUCCESS, input_value.set_varchar(ObString::make_string("owned key"), source_owner));
  input_value.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ObRowkey input(&input_value, 1), output;
  FilterIterator iter;
  iter.set_allocator(&owner); iter.set_session(&session);
  ASSERT_EQ(OB_SUCCESS, (iter.*access(ConvertTag{}))(input, output, targets));
  EXPECT_NE(input_value.datum_.ptr_, output.get_cell(0).datum_.ptr_);
  source_owner.reset();
  EXPECT_EQ(ObString::make_string("owned key"), output.get_cell(0).get_string());
  const std::string before = wire(output);
  ObWrapperAllocator reject;
  iter.set_allocator(&reject);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (iter.*access(ConvertTag{}))(output, output, targets));
  EXPECT_EQ(before, wire(output));
}
TEST_F(NativeObserverKeyFilters, actual_macro_filter_preserves_tail_trim_prefix_and_conservative_fallback)
{
  const auto rows = fixture("MACRO");
  ASSERT_EQ(6, rows.size());
  for (const auto &fields : rows) {
    ASSERT_EQ(8, fields.size()); SCOPED_TRACE(fields[1]);
    ObNewRange input;
    input.table_id_ = std::stoull(fields[2]);
    key_from(fields[3], owner, input.start_key_);
    key_from(fields[4], owner, input.end_key_);
    input.border_flag_.set_inclusive_start(); input.border_flag_.set_inclusive_end();
    ObSEArray<ObNewRange, 1> ranges;
    ASSERT_EQ(OB_SUCCESS, ranges.push_back(input));
    ObAllVirtualTabletSSTableMacroInfo scan;
    ObVirtualTableIterator &base = scan;
    const int ret = base.set_key_ranges(ranges);
    ASSERT_EQ(std::stoi(fields[5]), ret);
    ObTabletMeta tablet;
    tablet.tablet_id_ = ObTabletID(10);
    ObITable::TableKey key;
    key.tablet_id_ = ObTabletID(10); key.scn_range_.end_scn_ = SCN::min_scn();
    EXPECT_EQ(std::stoi(fields[6]) != 0, (scan.*access(TabletTag{}))(tablet));
    EXPECT_EQ(std::stoi(fields[7]) != 0, (scan.*access(SSTableTag{}))(key));
  }
}
} // namespace
