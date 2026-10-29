/**
 * Copyright (c) 2026 OceanBase
 *
 * OceanBase is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * OceanBase is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OceanBase. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * Unit test of the file source layer (the `file_source` library).
 *
 * The library is free of any OceanBase dependency, therefore the test can be
 * compiled and executed without a full OceanBase build:
 *
 *   g++ -std=c++11 -Isrc -DFILE_SOURCE_STANDALONE_TEST \
 *       -o file_source_unittest \
 *       unittest/sql/engine/file_source/file_source_unittest.cpp \
 *       src/sql/engine/file_source/*.cpp
 *
 *   file_source_unittest <work-dir> <fixture-dir>
 *
 * The `work-dir` receives the copy of the fixtures of the `fixture-dir`, the
 * default fixture directory is the data directory of the mysql test suite of
 * the file sql feature.
 *
 * The `FILE_SOURCE_STANDALONE_TEST` macro guards the `main()` function, so
 * that the file can be compiled (as an empty translation unit) by a build
 * system that globs all the unit test sources.
 */

#include "sql/engine/file_source/file_source_reader.h"
#include "sql/engine/file_source/file_source_util.h"
#include "sql/engine/file_source/file_type_inference.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

using namespace oceanbase;
using namespace oceanbase::sql;
using namespace oceanbase::sql::file_source;

namespace {

int g_failed = 0;
int g_checked = 0;

void check(bool cond, const std::string &what)
{
  ++g_checked;
  if (!cond) {
    ++g_failed;
    std::cerr << "[FAIL] " << what << std::endl;
  }
}

void check_eq_str(const std::string &actual, const std::string &expected, const std::string &what)
{
  ++g_checked;
  if (actual != expected) {
    ++g_failed;
    std::cerr << "[FAIL] " << what << ": expected <" << expected << ">, actual <" << actual << ">" << std::endl;
  }
}

void check_eq_int(int64_t actual, int64_t expected, const std::string &what)
{
  ++g_checked;
  if (actual != expected) {
    ++g_failed;
    std::cerr << "[FAIL] " << what << ": expected <" << expected << ">, actual <" << actual << ">" << std::endl;
  }
}

void write_file(const std::string &path, const std::string &content)
{
  FILE *fp = fopen(path.c_str(), "wb");
  if (NULL == fp) {
    std::cerr << "cannot write " << path << std::endl;
    exit(2);
  }
  fwrite(content.data(), 1, content.size(), fp);
  fclose(fp);
}

bool read_file(const std::string &path, std::string &content)
{
  FILE *fp = fopen(path.c_str(), "rb");
  if (NULL == fp) {
    return false;
  }
  char buffer[65536];
  size_t n = fread(buffer, 1, sizeof(buffer), fp);
  content.assign(buffer, n);
  fclose(fp);
  return true;
}

bool copy_file(const std::string &source, const std::string &target)
{
  std::string content;
  if (!read_file(source, content)) {
    return false;
  }
  write_file(target, content);
  return true;
}

/// Create the working directory of the test when it does not exist yet.
void make_directory(const std::string &path)
{
  if (path.empty() || "." == path) {
    return;
  }
#if defined(_WIN32)
  ::_mkdir(path.c_str());
#else
  ::mkdir(path.c_str(), 0755);
#endif
}

std::string base64_decode(const std::string &input)
{
  static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  int value = 0;
  int bits = -8;
  for (size_t i = 0; i < input.size(); ++i) {
    const char c = input[i];
    if ('\r' == c || '\n' == c || ' ' == c || '=' == c) {
      continue;
    }
    const char *pos = strchr(table, c);
    if (NULL == pos) {
      continue;
    }
    value = (value << 6) + static_cast<int>(pos - table);
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<char>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return output;
}

void test_type_inference()
{
  // the widening lattice of a single column
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::infer_literal_type("123")),
               static_cast<int64_t>(ColumnType::BIGINT), "literal 123 is a bigint");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::infer_literal_type("98.5")),
               static_cast<int64_t>(ColumnType::DOUBLE), "literal 98.5 is a double");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::infer_literal_type("true")),
               static_cast<int64_t>(ColumnType::BOOLEAN), "literal true is a boolean");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::infer_literal_type("2026-08-01")),
               static_cast<int64_t>(ColumnType::DATE), "literal date is a date");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::infer_literal_type("2026-08-01 10:11:12")),
               static_cast<int64_t>(ColumnType::DATETIME), "literal datetime is a datetime");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::infer_literal_type("Alice")),
               static_cast<int64_t>(ColumnType::VARCHAR), "literal text is a varchar");

  // bigint + double widens to double, an integer keeps the bigint type
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::merge_types(ColumnType::BIGINT, ColumnType::DOUBLE)),
               static_cast<int64_t>(ColumnType::DOUBLE), "bigint + double widens to double");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::merge_types(ColumnType::DATE, ColumnType::DATETIME)),
               static_cast<int64_t>(ColumnType::DATETIME), "date + datetime widens to datetime");
  check_eq_int(static_cast<int64_t>(ColumnTypeAccumulator::merge_types(ColumnType::BIGINT, ColumnType::DATE)),
               static_cast<int64_t>(ColumnType::VARCHAR), "a number and a date widen to varchar");

  // a column that only holds NULL values stays a null column
  ColumnTypeAccumulator accumulator;
  accumulator.feed_null();
  accumulator.feed_null();
  check(accumulator.is_all_null(), "a column with only null values is a null column");
  check(accumulator.has_null(), "a column with null values is nullable");
  accumulator.feed_value("7");
  check_eq_int(static_cast<int64_t>(accumulator.type()),
               static_cast<int64_t>(ColumnType::BIGINT), "null + bigint is a bigint");
  check(accumulator.has_null(), "the column is still nullable");

  // the format names of the sql layer
  FileFormat format = FileFormat::AUTO;
  check(parse_file_format("csv", format) && FileFormat::CSV == format, "csv format");
  check(parse_file_format("newline_delimited", format) && FileFormat::JSONL == format, "newline_delimited format");
  check(parse_file_format("json", format) && FileFormat::JSON == format, "json format");
  check(!parse_file_format("xml", format), "xml is not a supported format");
}

void test_secure_path()
{
  std::string error;
  // an empty secure_file_priv denies every access
  check(!check_secure_path("", "/tmp/a.csv", error), "an empty secure_file_priv denies the access");
  // "/" allows every absolute path
  check(check_secure_path("/", "/tmp/a.csv", error), "the root secure_file_priv allows every path");
  // the path has to be located below the configured directory
  check(check_secure_path("/tmp/allowed", "/tmp/allowed/a.csv", error), "a path below the directory is allowed");
  check(!check_secure_path("/tmp/allowed", "/tmp/other/a.csv", error), "a path outside of the directory is denied");
  // the path traversal is rejected
  check(!check_secure_path("/tmp/allowed", "/tmp/allowed/../other/a.csv", error),
        "a path traversal is denied");
}

void test_extension()
{
  check_eq_str(file_extension("/tmp/data/basic.csv"), "csv", "the extension of a csv file");
  check_eq_str(file_extension("/tmp/data/BASIC.CSV"), "csv", "the extension is lower case");
  check_eq_str(file_extension("/tmp/data/basic.jsonl"), "jsonl", "the extension of a jsonl file");
  check_eq_str(file_extension("/tmp/data/simple.parquet"), "parquet", "the extension of a parquet file");
  check_eq_str(file_extension("/tmp/data/no_extension"), "", "a file without extension");
  check_eq_str(file_extension("/tmp.data/file"), "", "the extension is the suffix of the last path component");
  check_eq_int(static_cast<int64_t>(detect_format_by_extension("/tmp/data/basic.csv")),
               static_cast<int64_t>(FileFormat::CSV), "a csv file is detected");
  check_eq_int(static_cast<int64_t>(detect_format_by_extension("/tmp/data/invalid.txt")),
               static_cast<int64_t>(FileFormat::AUTO), "an unsupported extension is not detected");
}

void test_csv(const std::string &dir)
{
  const std::string path = dir + "/basic.csv";
  FileSourceReader reader;
  FileReadOptions options;
  options.format = FileFormat::CSV;
  FileSourceError error = reader.open(path, options);
  check(error.is_ok(), "the csv file is opened: " + error.message());

  FileSchema schema;
  error = reader.infer_schema(schema);
  check(error.is_ok(), "the csv schema is inferred: " + error.message());
  check_eq_int(schema.column_count(), 8, "the csv file has eight columns");
  if (8 == schema.column_count()) {
    check_eq_str(schema.column_at(0)->name, "id", "the first column of the csv file");
    check_eq_str(schema.column_at(7)->name, "id__2", "the duplicated header is renamed");
    check_eq_int(static_cast<int64_t>(schema.column_at(0)->type),
                 static_cast<int64_t>(ColumnType::BIGINT), "the id column is a bigint");
    check_eq_int(static_cast<int64_t>(schema.column_at(1)->type),
                 static_cast<int64_t>(ColumnType::VARCHAR), "the name column is a varchar");
    check_eq_int(static_cast<int64_t>(schema.column_at(2)->type),
                 static_cast<int64_t>(ColumnType::DOUBLE), "the score column is a double");
    check_eq_int(static_cast<int64_t>(schema.column_at(3)->type),
                 static_cast<int64_t>(ColumnType::BOOLEAN), "the active column is a boolean");
    check_eq_int(static_cast<int64_t>(schema.column_at(4)->type),
                 static_cast<int64_t>(ColumnType::DATE), "the day column is a date");
    check_eq_int(static_cast<int64_t>(schema.column_at(5)->type),
                 static_cast<int64_t>(ColumnType::DATETIME), "the created column is a datetime");
    check(!schema.column_at(0)->nullable, "the id column is not nullable");
    check(schema.column_at(4)->nullable, "the day column is nullable");
    check(schema.column_at(5)->nullable, "the created column is nullable");
  }

  std::vector<std::vector<std::string> > rows;
  std::vector<std::vector<bool> > nulls;
  FileRow row;
  while (reader.next_row(row, error)) {
    rows.push_back(row.values);
    nulls.push_back(row.nulls);
  }
  check(error.is_ok(), "the csv file is scanned: " + error.message());
  check_eq_int(static_cast<int64_t>(rows.size()), 3, "the csv file has three rows");
  if (3 == rows.size()) {
    check_eq_str(rows[0][0], "1", "the first id");
    check_eq_str(rows[0][6], "hello,\nworld", "the quoted field holds a newline");
    check_eq_str(rows[1][0], "2", "the second id");
    check(nulls[1][4], "the empty day field is null");
    check(nulls[1][5], "the empty created field is null");
    check_eq_str(rows[2][5], "2026-08-03T12:13:14", "the iso datetime is kept as it is");
    check_eq_str(rows[2][6], "say \"hi\"", "the doubled quote is unescaped");
  }
}

void test_jsonl(const std::string &dir)
{
  const std::string path = dir + "/basic.jsonl";
  FileSourceReader reader;
  FileReadOptions options;
  options.format = FileFormat::JSONL;
  FileSourceError error = reader.open(path, options);
  check(error.is_ok(), "the jsonl file is opened: " + error.message());

  FileSchema schema;
  error = reader.infer_schema(schema);
  check(error.is_ok(), "the jsonl schema is inferred: " + error.message());
  check_eq_int(schema.column_count(), 6, "the jsonl file has six columns");
  if (6 == schema.column_count()) {
    check_eq_str(schema.column_at(0)->name, "id", "the first column of the jsonl file");
    check_eq_str(schema.column_at(4)->name, "day", "the day column of the jsonl file");
    check_eq_str(schema.column_at(5)->name, "note", "the last column of the jsonl file");
    check_eq_int(static_cast<int64_t>(schema.column_at(2)->type),
                 static_cast<int64_t>(ColumnType::DOUBLE), "the score column is a double");
    check_eq_int(static_cast<int64_t>(schema.column_at(3)->type),
                 static_cast<int64_t>(ColumnType::BOOLEAN), "the active column is a boolean");
  }

  std::vector<std::vector<std::string> > rows;
  std::vector<std::vector<bool> > nulls;
  FileRow row;
  while (reader.next_row(row, error)) {
    rows.push_back(row.values);
    nulls.push_back(row.nulls);
  }
  check(error.is_ok(), "the jsonl file is scanned: " + error.message());
  check_eq_int(static_cast<int64_t>(rows.size()), 3, "the jsonl file has three rows");
  if (3 == rows.size()) {
    check_eq_str(rows[0][1], "Alice", "the name of the first row");
    check(nulls[0][4], "the missing day of the first row is null");
    check(nulls[1][2], "the missing score of the second row is null");
    check_eq_str(rows[1][4], "2026-08-02", "the day of the second row");
    check_eq_str(rows[2][5], "ok", "the note of the third row");
  }
}

void test_parquet(const std::string &dir)
{
  const std::string path = dir + "/simple.parquet";
  FileSourceReader reader;
  FileReadOptions options;
  options.format = FileFormat::PARQUET;
  FileSourceError error = reader.open(path, options);
  check(error.is_ok(), "the parquet file is opened: " + error.message());
  if (!error.is_ok()) {
    return;
  }
  FileSchema schema;
  error = reader.infer_schema(schema);
  check(error.is_ok(), "the parquet schema is inferred: " + error.message());
  check_eq_int(schema.column_count(), 1, "the parquet file has one column");

  int64_t rows = 0;
  int64_t sum = 0;
  int64_t min_value = 0;
  int64_t max_value = 0;
  FileRow row;
  while (reader.next_row(row, error)) {
    const int64_t value = strtoll(row.values[0].c_str(), NULL, 10);
    if (0 == rows) {
      min_value = value;
      max_value = value;
    }
    min_value = value < min_value ? value : min_value;
    max_value = value > max_value ? value : max_value;
    sum += value;
    ++rows;
  }
  check(error.is_ok(), "the parquet file is scanned: " + error.message());
  check_eq_int(rows, 7, "the parquet file has seven rows");
  check_eq_int(min_value, 0, "the minimum value of the parquet file");
  check_eq_int(max_value, 6, "the maximum value of the parquet file");
  check_eq_int(sum, 21, "the sum of the parquet file");
}

void test_glob(const std::string &dir)
{
  std::vector<std::string> files;
  FileSourceError error = FileSourceReader::list_files(dir + "/*", files);
  check(error.is_ok(), "the glob pattern is expanded: " + error.message());
  check_eq_int(static_cast<int64_t>(files.size()), 4, "the glob pattern matches four files");
  if (4 == files.size()) {
    check(files[0].find("basic.csv") != std::string::npos, "the first file of the glob pattern");
    check(files[1].find("basic.jsonl") != std::string::npos, "the second file of the glob pattern");
    check(files[2].find("invalid.txt") != std::string::npos, "the third file of the glob pattern");
    check(files[3].find("simple.parquet") != std::string::npos, "the fourth file of the glob pattern");
  }

  // the glob reader reports every file as one row with a single file column
  FileSourceReader reader;
  error = reader.open_glob(dir + "/*");
  check(error.is_ok(), "the glob reader is opened: " + error.message());
  FileSchema schema;
  error = reader.infer_schema(schema);
  check(error.is_ok(), "the glob schema is inferred: " + error.message());
  check_eq_int(schema.column_count(), 1, "the glob result has one column");
  if (1 == schema.column_count()) {
    check_eq_str(schema.column_at(0)->name, "file", "the glob result has a file column");
  }
  int64_t rows = 0;
  FileRow row;
  while (reader.next_row(row, error)) {
    ++rows;
  }
  check_eq_int(rows, 4, "the glob result has four rows");

  // a pattern that does not match any file is an error
  std::vector<std::string> empty;
  error = FileSourceReader::list_files(dir + "/*.not_exists", empty);
  check(!error.is_ok(), "a pattern without any match is an error");
  check(FileErrorCode::FILE_NOT_FOUND == error.code(), "a pattern without any match reports a file not found error");
}

void test_unsupported_format(const std::string &dir)
{
  FileSourceReader reader;
  FileReadOptions options;
  options.format = FileFormat::AUTO;
  FileSourceError error = reader.open(dir + "/invalid.txt", options);
  check(!error.is_ok(), "an unsupported extension is rejected");
  check(FileErrorCode::UNSUPPORTED_FORMAT == error.code(), "an unsupported extension reports an unsupported format error");

  // a file that does not exist is reported as a missing file
  error = reader.open(dir + "/not_exists.csv", options);
  check(!error.is_ok(), "a missing file is rejected");
  check(FileErrorCode::FILE_NOT_FOUND == error.code(), "a missing file reports a file not found error");
}

}  // namespace

#ifdef FILE_SOURCE_STANDALONE_TEST
int main(int argc, char **argv)
{
  const std::string dir = (argc > 1) ? argv[1] : ".";
  const std::string fixture_dir = (argc > 2)
                                      ? argv[2]
                                      : std::string("tools/deploy/mysql_test/test_suite/file_sql/data");
  // the fixtures of the mysql test suite are the reference of the file sql
  // feature, the test works on a copy of them
  make_directory(dir);
  const std::string csv_path = dir + "/basic.csv";
  const std::string jsonl_path = dir + "/basic.jsonl";
  if (!copy_file(fixture_dir + "/basic.csv", csv_path)) {
    std::cerr << "[FAIL] the fixture " << fixture_dir << "/basic.csv is not readable" << std::endl;
    return 2;
  }
  if (!copy_file(fixture_dir + "/basic.jsonl", jsonl_path)) {
    std::cerr << "[FAIL] the fixture " << fixture_dir << "/basic.jsonl is not readable" << std::endl;
    return 2;
  }
  copy_file(fixture_dir + "/invalid.txt", dir + "/invalid.txt");
  std::string parquet_fixture;
  if (!read_file(fixture_dir + "/simple.parquet.b64", parquet_fixture)) {
    std::cerr << "[FAIL] the fixture " << fixture_dir << "/simple.parquet.b64 is not readable" << std::endl;
    return 2;
  }
  write_file(dir + "/simple.parquet", base64_decode(parquet_fixture));

  test_type_inference();
  test_secure_path();
  test_extension();
  test_csv(dir);
  test_jsonl(dir);
  test_parquet(dir);
  test_glob(dir);
  test_unsupported_format(dir);

  std::cout << "[INFO] " << (g_checked - g_failed) << "/" << g_checked << " checks passed" << std::endl;
  if (0 != g_failed) {
    std::cout << "[FAIL] " << g_failed << " checks failed" << std::endl;
    return 1;
  }
  std::cout << "[PASS] all checks passed" << std::endl;
  return 0;
}
#endif  // FILE_SOURCE_STANDALONE_TEST
