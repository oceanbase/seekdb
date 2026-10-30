#include "query/protocol/ob_mysql_protocol_util.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/ob_field.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::obmysql;

namespace {
std::vector<std::string> columns(const std::string &line)
{
  std::vector<std::string> result;
  size_t begin = 0;
  for (;;) {
    const size_t end = line.find('\t', begin);
    result.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos) { return result; }
    begin = end + 1;
  }
}
std::string hex(const char *bytes, int64_t count)
{
  static const char digits[] = "0123456789abcdef";
  std::string result;
  for (int64_t i = 0; i < count; ++i) {
    const unsigned char ch = bytes[i];
    result += digits[ch >> 4]; result += digits[ch & 15];
  }
  return result.empty() ? "-" : result;
}
std::string unhex(const std::string &text)
{
  if (text == "-") { return {}; }
  const char *digits = "0123456789abcdef";
  std::string result;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    const char *high = std::strchr(digits, text[i]);
    const char *low = std::strchr(digits, text[i + 1]);
    if (high == nullptr || low == nullptr) { return {}; }
    result += char((high - digits) * 16 + low - digits);
  }
  return result;
}
class RejectOwner final : public ObIAllocator {
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
int seed(const std::vector<std::string> &row, ObIAllocator &owner, DatumValue &value)
{
  const std::string &name = row[1];
  if (name.rfind("integer_", 0) == 0) {
    const size_t separator = name.find('_', 8);
    const int type = std::stoi(name.substr(8, separator - 8));
    const int ret = value.set_int(std::stoll(name.substr(separator + 1)), owner);
    if (ret == OB_SUCCESS) { value.meta_.set_type(static_cast<SqlValueType>(type)); }
    return ret;
  }
  if (name.rfind("unsupported_", 0) == 0) {
    const int ret = value.set_int(1, owner);
    if (ret == OB_SUCCESS) { value.meta_.set_type(static_cast<SqlValueType>(std::stoi(name.substr(12)))); }
    return ret;
  }
  if (name == "invalid") { value.meta_.set_type(ObMaxType); return OB_SUCCESS; }
  const std::string bytes = unhex(row[5]);
  if (bytes.empty()) { return OB_INVALID_DATA; }
  int64_t pos = 0;
  const int ret = DatumParameterCodec::decode_value(bytes.data(), bytes.size(), pos,
                                                    owner, value.datum_, value.meta_);
  return ret == OB_SUCCESS && pos != bytes.size() ? OB_INVALID_DATA : ret;
}
std::string neutral(int ret, const ObMySQLCellValue &out)
{
  std::ostringstream result;
  result << ret << '\t' << int(out.kind_) << '\t' << out.value_
         << '\t' << out.days_ << '\t' << out.microseconds_ << '\t' << out.bit_len_
         << '\t' << out.year_ << '\t' << int(out.month_) << '\t' << int(out.day_)
         << '\t' << int(out.hour_) << '\t' << int(out.minute_) << '\t' << int(out.second_)
         << '\t' << int(out.is_negative_) << '\t' << out.get_bytes_len()
         << '\t' << hex(out.get_bytes(), out.get_bytes_len());
  return result.str();
}

bool output_pack_scalar_datum_supported(const SqlTypeMeta &meta,
                                        const SqlValueDatumMapType map)
{
  bool supported = false;
  SqlValueDatumMapType expected_map = OBJ_DATUM_MAPPING_MAX;
  switch (meta.get_type_class()) {
  case ObNullTC:
    expected_map = OBJ_DATUM_NULL;
    supported = true;
    break;
  case ObIntTC:
  case ObUIntTC:
  case ObDoubleTC:
  case ObDateTimeTC:
  case ObTimeTC:
  case ObBitTC:
  case ObMySQLDateTimeTC:
    expected_map = OBJ_DATUM_8BYTE_DATA;
    supported = true;
    break;
  case ObFloatTC:
  case ObDateTC:
  case ObMySQLDateTC:
    expected_map = OBJ_DATUM_4BYTE_DATA;
    supported = true;
    break;
  case ObYearTC:
    expected_map = OBJ_DATUM_1BYTE_DATA;
    supported = true;
    break;
  case ObNumberTC:
    expected_map = OBJ_DATUM_NUMBER;
    supported = true;
    break;
  case ObDecimalIntTC:
    expected_map = OBJ_DATUM_DECIMALINT;
    supported = true;
    break;
  default:
    break;
  }
  return supported && map == expected_map
         && ObDatum::get_sql_value_datum_map_type(meta.get_type()) == expected_map;
}

uint64_t cell_checksum(const ObMySQLCellValue &cell)
{
  uint64_t checksum = static_cast<uint64_t>(cell.kind_) + cell.value_
      + cell.days_ + cell.microseconds_ + cell.bit_len_ + cell.year_
      + cell.month_ + cell.day_ + cell.hour_ + cell.minute_ + cell.second_
      + cell.is_negative_;
  const char *bytes = cell.get_bytes();
  for (int64_t i = 0; i < cell.get_bytes_len(); ++i) {
    checksum = checksum * 33 + static_cast<unsigned char>(bytes[i]);
  }
  return checksum;
}

struct CellFormatterBenchCase
{
  DatumValue value_;
  SqlValueDatumMapType map_;
  ObField field_;
  MYSQL_PROTOCOL_TYPE protocol_;
};

struct BenchSample
{
  double wall_ns_per_cell_;
  double cpu_ns_per_cell_;
};

template <typename Fn>
BenchSample measure_cells(Fn &&fn, int64_t cell_count, uint64_t &checksum)
{
  const auto wall_start = std::chrono::steady_clock::now();
  const std::clock_t cpu_start = std::clock();
  fn(checksum);
  const std::clock_t cpu_end = std::clock();
  const auto wall_end = std::chrono::steady_clock::now();
  const double wall_ns = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();
  const double cpu_ns = static_cast<double>(cpu_end - cpu_start) * 1.0e9 / CLOCKS_PER_SEC;
  return {wall_ns / cell_count, cpu_ns / cell_count};
}

double median(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  const size_t middle = values.size() / 2;
  return values.size() % 2 == 0
      ? (values[middle - 1] + values[middle]) / 2.0
      : values[middle];
}
}

TEST(NativeMysqlProtocol, all_old_formatter_payloads_and_errors_match_without_object_materialization)
{
  std::ifstream input("unittest/observer/data/mysql_protocol_values.golden");
  ASSERT_TRUE(input.good());
  sql::ObSQLSessionInfo session;
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto row = columns(line);
    ASSERT_EQ(21, row.size());
    SCOPED_TRACE(row[1] + ":protocol=" + row[2] + ":field=" + row[3] + ":oom=" + row[4]);
    ObArenaAllocator source, scratch;
    RejectOwner reject;
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, seed(row, source, value));
    const int mode = std::stoi(row[3]);
    ObField field;
    field.accuracy_.set_scale(mode == 1 ? -1 : mode == 2 ? 0 : mode == 3 ? 2 : 6);
    field.accuracy_.set_precision(mode == 4 ? 64 : mode == 3 ? 1 : 10);
    field.flags_ = mode == 5 ? ZEROFILL_FLAG : 0;
    field.length_ = mode == 5 ? 24 : 0;
    ObTimeZoneInfo zone;
    ObDataTypeCastParams parameters(&zone);
    ObIAllocator &owner = row[4] == "1" ? static_cast<ObIAllocator &>(reject)
                                         : static_cast<ObIAllocator &>(scratch);
    ObMySQLCellValue out;
    const int ret = ObSMUtils::build_cell_value(value,
        static_cast<MYSQL_PROTOCOL_TYPE>(std::stoi(row[2])), owner, out,
        parameters, mode == 0 ? nullptr : &field, session);
    std::string expected;
    for (size_t i = 6; i < row.size(); ++i) {
      if (i > 6) { expected += '\t'; }
      expected += row[i];
    }
    EXPECT_EQ(expected, neutral(ret, out));
    ++checked;
  }
  EXPECT_EQ(1784, checked);
}

TEST(NativeMysqlProtocol, typed_null_uses_datum_nullness_without_reading_a_scalar_payload)
{
  sql::ObSQLSessionInfo session;
  RejectOwner owner;
  ObTimeZoneInfo zone;
  ObDataTypeCastParams parameters(&zone);
  for (SqlValueType type : {ObIntType, ObNumberType, ObDateTimeType, ObVarcharType}) {
    DatumValue value;
    value.meta_.set_type(type);
    ASSERT_TRUE(value.datum_.is_null());
    for (auto protocol : {TEXT, BINARY}) {
      ObMySQLCellValue out;
      ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_cell_value(value, protocol,
          owner, out, parameters, nullptr, session));
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, out.kind_);
    }
  }
}

TEST(NativeMysqlProtocol, field_list_marker_and_anonymous_collection_type_are_metadata_only)
{
  ObField field;
  field.type_meta_.set_type(ObExtendType);
  field.type_meta_.set_extend_type(pl::PL_RECORD_TYPE);
  field.field_list_default_type_ = static_cast<uint32_t>(MYSQL_TYPE_LONG);

  ObMySQLField mysql_field;
  ASSERT_EQ(OB_SUCCESS, observer::ObMySQLResultSet::to_mysql_field(
      field, mysql_field, true));
  EXPECT_EQ(MYSQL_TYPE_COMPLEX, mysql_field.type_);
  EXPECT_EQ(MYSQL_TYPE_LONG, mysql_field.default_value_);
}

TEST(NativeMysqlProtocol, output_pack_scalar_native_and_generic_datum_formatters_match)
{
  std::ifstream input("unittest/observer/data/mysql_protocol_values.golden");
  ASSERT_TRUE(input.good());
  sql::ObSQLSessionInfo session;
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto row = columns(line);
    ASSERT_EQ(21, row.size());
    if (row[4] == "1") { continue; } // this test compares values, not allocator failure injection
    ObArenaAllocator source, datum_scratch, generic_scratch;
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, seed(row, source, value));
    const SqlValueType obj_type = value.meta_.get_type();
    if (obj_type < 0 || obj_type >= ObMaxType) { continue; }
    const SqlValueDatumMapType map = ObDatum::get_sql_value_datum_map_type(obj_type);
    const bool supported = output_pack_scalar_datum_supported(value.meta_, map);
    if (supported) {
      ObField field;
      field.type_meta_.set_type(obj_type); // output_pack's native BINARY path requires an exact field type
      const int mode = std::stoi(row[3]);
      field.accuracy_.set_scale(mode == 1 ? -1 : mode == 2 ? 0 : mode == 3 ? 2 : 6);
      field.accuracy_.set_precision(mode == 4 ? 64 : mode == 3 ? 1 : 10);
      field.flags_ = mode == 5 ? ZEROFILL_FLAG : 0;
      field.length_ = mode == 5 ? 24 : 0;
      ObTimeZoneInfo zone;
      ObDataTypeCastParams parameters(&zone);
      for (auto protocol : {TEXT, BINARY}) {
        SCOPED_TRACE(row[1] + ", datum_type=" + std::to_string(static_cast<int>(obj_type))
                     + ", protocol=" + std::to_string(static_cast<int>(protocol))
                     + ", mode=" + std::to_string(mode)
                     + ", map=" + std::to_string(static_cast<int>(map)));
        ObMySQLCellValue datum_out, generic_out;
        const int datum_ret = ObSMUtils::build_datum_cell_value(
            value.datum_, value.meta_, protocol, datum_scratch, datum_out,
            &field, parameters);
        const int generic_ret = ObSMUtils::build_cell_value(
            value, protocol, generic_scratch, generic_out, parameters, &field, session);
        EXPECT_EQ(generic_ret, datum_ret);
        EXPECT_EQ(neutral(generic_ret, generic_out), neutral(datum_ret, datum_out));
        ++checked;
      }
    }
  }
  EXPECT_GT(checked, 0);
}

TEST(NativeMysqlProtocol, DISABLED_output_pack_scalar_formatter_ab)
{
  constexpr int64_t ITERATIONS = 20000;
  constexpr int64_t PAIRS = 7;
  std::ifstream input("unittest/observer/data/mysql_protocol_values.golden");
  ASSERT_TRUE(input.good());
  ObArenaAllocator source, preflight_datum_scratch, preflight_generic_scratch;
  sql::ObSQLSessionInfo session;
  ObTimeZoneInfo zone;
  ObDataTypeCastParams parameters(&zone);
  std::vector<CellFormatterBenchCase> cases;
  std::vector<int64_t> seen;
  std::string line;
  while (std::getline(input, line)) {
    const auto row = columns(line);
    ASSERT_EQ(21, row.size());
    // One accuracy-zero, successful sample per type and protocol is enough to
    // compare formatter cost; duplicate field settings and injected OOM rows
    // do not improve this microbenchmark.
    if (row[3] != "0" || row[4] != "0") { continue; }
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, seed(row, source, value));
    const SqlValueType type = value.meta_.get_type();
    if (type < 0 || type >= ObMaxType) { continue; }
    const SqlValueDatumMapType map = ObDatum::get_sql_value_datum_map_type(type);
    if (!output_pack_scalar_datum_supported(value.meta_, map)) { continue; }
    const MYSQL_PROTOCOL_TYPE protocol =
        static_cast<MYSQL_PROTOCOL_TYPE>(std::stoi(row[2]));
    const int64_t key = static_cast<int64_t>(type) * 4 + static_cast<int64_t>(protocol);
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) { continue; }

    CellFormatterBenchCase candidate;
    candidate.value_ = value; // payload remains owned by the source arena
    candidate.map_ = map;
    candidate.protocol_ = protocol;
    candidate.field_.type_meta_.set_type(type);
    candidate.field_.accuracy_.set_scale(6);
    candidate.field_.accuracy_.set_precision(10);

    ObMySQLCellValue datum_out, generic_out;
    const int datum_ret = ObSMUtils::build_datum_cell_value(
        candidate.value_.datum_, candidate.value_.meta_, protocol,
        preflight_datum_scratch, datum_out, &candidate.field_, parameters);
    const int generic_ret = ObSMUtils::build_cell_value(
        candidate.value_, protocol, preflight_generic_scratch, generic_out,
        parameters, &candidate.field_, session);
    if (datum_ret != OB_SUCCESS || generic_ret != OB_SUCCESS) {
      EXPECT_EQ(generic_ret, datum_ret) << row[1] << ", protocol=" << int(protocol);
      continue; // only successful encodings are useful throughput samples
    }
    ASSERT_EQ(neutral(generic_ret, generic_out), neutral(datum_ret, datum_out));
    // Restrict the timed sample set to paths that do not allocate per cell.
    if (datum_out.get_bytes_len() > 0) {
      if (!datum_out.uses_local_buffer() || !generic_out.uses_local_buffer()) {
        continue;
      }
    }
    cases.push_back(candidate);
    seen.push_back(key);
  }
  ASSERT_FALSE(cases.empty());

  const int64_t cell_count = ITERATIONS * static_cast<int64_t>(cases.size());
  auto run_generic = [&](uint64_t &checksum) {
    for (int64_t i = 0; i < cell_count; ++i) {
      const CellFormatterBenchCase &item = cases[i % cases.size()];
      ObMySQLCellValue cell;
      const int ret = ObSMUtils::build_cell_value(
          item.value_, item.protocol_, preflight_generic_scratch,
          cell, parameters, &item.field_, session);
      checksum += static_cast<uint64_t>(ret) + cell_checksum(cell);
    }
  };
  auto run_native = [&](uint64_t &checksum) {
    for (int64_t i = 0; i < cell_count; ++i) {
      const CellFormatterBenchCase &item = cases[i % cases.size()];
      ObMySQLCellValue cell;
      const int ret = ObSMUtils::build_datum_cell_value(
          item.value_.datum_, item.value_.meta_, item.protocol_,
          preflight_datum_scratch, cell, &item.field_, parameters);
      checksum += static_cast<uint64_t>(ret) + cell_checksum(cell);
    }
  };

  // Warm up both paths; benchmark assertions concern semantic equality only.
  uint64_t warm_generic = 0, warm_native = 0;
  run_generic(warm_generic);
  run_native(warm_native);
  ASSERT_EQ(warm_generic, warm_native);

  std::vector<double> generic_wall, generic_cpu, native_wall, native_cpu;
  generic_wall.reserve(PAIRS); generic_cpu.reserve(PAIRS);
  native_wall.reserve(PAIRS); native_cpu.reserve(PAIRS);
  uint64_t checksum_generic = 0, checksum_native = 0;
  for (int64_t pair = 0; pair < PAIRS; ++pair) {
    BenchSample generic_first, native_first, native_second, generic_second;
    if (pair % 2 == 0) {
      generic_first = measure_cells(run_generic, cell_count, checksum_generic);
      native_first = measure_cells(run_native, cell_count, checksum_native);
      native_second = measure_cells(run_native, cell_count, checksum_native);
      generic_second = measure_cells(run_generic, cell_count, checksum_generic);
    } else {
      native_first = measure_cells(run_native, cell_count, checksum_native);
      generic_first = measure_cells(run_generic, cell_count, checksum_generic);
      generic_second = measure_cells(run_generic, cell_count, checksum_generic);
      native_second = measure_cells(run_native, cell_count, checksum_native);
    }
    generic_wall.push_back((generic_first.wall_ns_per_cell_ + generic_second.wall_ns_per_cell_) / 2.0);
    generic_cpu.push_back((generic_first.cpu_ns_per_cell_ + generic_second.cpu_ns_per_cell_) / 2.0);
    native_wall.push_back((native_first.wall_ns_per_cell_ + native_second.wall_ns_per_cell_) / 2.0);
    native_cpu.push_back((native_first.cpu_ns_per_cell_ + native_second.cpu_ns_per_cell_) / 2.0);
  }
  ASSERT_EQ(checksum_generic, checksum_native);

  const double generic_wall_ns = median(generic_wall);
  const double native_wall_ns = median(native_wall);
  const double generic_cpu_ns = median(generic_cpu);
  const double native_cpu_ns = median(native_cpu);
  std::cout << "Datum cell formatter A/B: cases=" << cases.size()
            << " cells/sample=" << cell_count << " paired_samples=" << PAIRS
            << " generic_wall_ns/cell=" << generic_wall_ns
            << " datum_wall_ns/cell=" << native_wall_ns
            << " wall_speedup=" << generic_wall_ns / native_wall_ns
            << " generic_cpu_ns/cell=" << generic_cpu_ns
            << " datum_cpu_ns/cell=" << native_cpu_ns
            << " cpu_speedup=" << generic_cpu_ns / native_cpu_ns
            << " generic_cells/s=" << 1.0e9 / generic_wall_ns
            << " datum_cells/s=" << 1.0e9 / native_wall_ns
            << " checksum=" << checksum_generic << "\n";
}
