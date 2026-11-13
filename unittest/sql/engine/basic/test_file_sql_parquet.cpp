/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Behavioural tests for the Parquet reader. The fixtures cover the page
// versions, the encodings, the codecs, the row groups and the physical types
// that the official sample does not exercise.
//
// The tests use the product sources directly, no query engine and no
// substitute implementation is involved.

#include "file_sql_parquet.h"

#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase::sql::file_sql;

namespace {

int g_failures = 0;

void check(bool condition, const char *message)
{
  if (!condition) {
    std::printf("FAIL %s\n", message);
    ++g_failures;
  }
}

// Reads a whole file into a vector of rows.
bool read_all(const std::string &path, std::vector<ScalarRow> &rows,
              std::vector<ColumnSchema> &columns)
{
  FILE *input = fopen(path.c_str(), "rb");
  if (input == nullptr) {
    std::printf("FAIL cannot open %s\n", path.c_str());
    ++g_failures;
    return false;
  }
  ParquetReader reader;
  const FileStatus status = reader.init(input, ParquetOptions());
  if (!status.ok()) {
    std::printf("FAIL %s: init failed: %s\n", path.c_str(), status.message.c_str());
    ++g_failures;
    fclose(input);
    return false;
  }
  columns = reader.columns();
  while (true) {
    std::vector<ScalarRow> batch;
    const FileStatus next = reader.next_batch(1000, batch);
    if (!next.ok()) {
      std::printf("FAIL %s: next_batch failed: %s\n", path.c_str(), next.message.c_str());
      ++g_failures;
      fclose(input);
      return false;
    }
    if (batch.empty()) { break; }
    for (auto &row : batch) { rows.push_back(std::move(row)); }
  }
  fclose(input);
  return true;
}

// Checks a column of INT32 values against the expected sequence.
void check_int32(const std::vector<ScalarRow> &rows, size_t column,
                 int64_t first, int64_t count, const char *label)
{
  if (rows.size() != static_cast<size_t>(count)) {
    std::printf("FAIL %s: expected %lld rows, got %zu\n", label,
                (long long)count, rows.size());
    ++g_failures;
    return;
  }
  for (size_t i = 0; i < rows.size(); ++i) {
    if (rows[i][column].is_null) {
      std::printf("FAIL %s: row %zu is null\n", label, i);
      ++g_failures;
      return;
    }
    const int64_t expected = first + static_cast<int64_t>(i);
    if (rows[i][column].integer != expected) {
      std::printf("FAIL %s: row %zu is %lld, expected %lld\n", label, i,
                  (long long)rows[i][column].integer, (long long)expected);
      ++g_failures;
      return;
    }
  }
}

// A fixture whose single column holds the values 0 to 6.
void test_official_shape(const std::string &dir, const char *name)
{
  const std::string path = dir + "/" + name;
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  if (columns.size() != 1) {
    std::printf("FAIL %s: expected one column, got %zu\n", name, columns.size());
    ++g_failures;
    return;
  }
  check_int32(rows, 0, 0, 7, name);
}

// A fixture whose single column holds null values.
void test_nullable(const std::string &dir, const char *name)
{
  const std::string path = dir + "/" + name;
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  if (rows.size() != 7) {
    std::printf("FAIL %s: expected 7 rows, got %zu\n", name, rows.size());
    ++g_failures;
    return;
  }
  const bool expected_null[7] = {false, true, false, false, true, false, false};
  const int64_t expected_value[7] = {0, 0, 2, 3, 0, 5, 6};
  for (size_t i = 0; i < 7; ++i) {
    if (rows[i][0].is_null != expected_null[i]) {
      std::printf("FAIL %s: row %zu null is %d, expected %d\n", name, i,
                  (int)rows[i][0].is_null, (int)expected_null[i]);
      ++g_failures;
      return;
    }
    if (!expected_null[i] && rows[i][0].integer != expected_value[i]) {
      std::printf("FAIL %s: row %zu is %lld, expected %lld\n", name, i,
                  (long long)rows[i][0].integer, (long long)expected_value[i]);
      ++g_failures;
      return;
    }
  }
}

// A fixture whose single column spans several row groups.
void test_row_groups(const std::string &dir, const char *name)
{
  const std::string path = dir + "/" + name;
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  check_int32(rows, 0, 0, 300, name);
}

// A fixture that holds the other physical types.
void test_types(const std::string &dir, const char *name)
{
  const std::string path = dir + "/" + name;
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  if (rows.size() != 4) {
    std::printf("FAIL %s: expected 4 rows, got %zu\n", name, rows.size());
    ++g_failures;
    return;
  }
  if (columns.size() != 5) {
    std::printf("FAIL %s: expected 5 columns, got %zu\n", name, columns.size());
    ++g_failures;
    return;
  }
  // The columns are the boolean, the int32, the int64, the double and the
  // byte array.
  check(rows[0][0].integer == 1, "types: the boolean true");
  check(rows[1][0].integer == 0, "types: the boolean false");
  check(rows[0][1].integer == 1, "types: the int32 value");
  check(rows[1][1].integer == -2, "types: the negative int32 value");
  check(rows[0][2].integer == 1, "types: the int64 value");
  check(rows[1][2].integer == -2, "types: the negative int64 value");
  check(rows[0][3].real > 1.4 && rows[0][3].real < 1.6, "types: the double value");
  check(rows[0][4].text == "a", "types: the string value");
  check(rows[2][4].text == "ccc", "types: the longer string value");
  check(rows[0][4].is_null == false, "types: the string is not null");
  check(rows[3][4].is_null == true, "types: the null string");
}

// A fixture with two columns, so that the column pruning can be verified.
void test_multi_column(const std::string &dir, const char *name)
{
  const std::string path = dir + "/" + name;
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  if (columns.size() != 2) {
    std::printf("FAIL %s: expected 2 columns, got %zu\n", name, columns.size());
    ++g_failures;
    return;
  }
  check_int32(rows, 0, 0, 7, name);
  check(rows[0][1].text == "a", "types: the name of the first row");
  check(rows[6][1].text == "g", "types: the name of the last row");

  // The projection reads only the second column.
  FILE *input = fopen(path.c_str(), "rb");
  if (input != nullptr) {
    ParquetOptions options;
    options.projection.push_back("name");
    ParquetReader reader;
    const FileStatus status = reader.init(input, options);
    check(status.ok(), "the projection is accepted");
    std::vector<ScalarRow> projected;
    while (true) {
      std::vector<ScalarRow> batch;
      const FileStatus next = reader.next_batch(1000, batch);
      check(next.ok(), "the projection reads the rows");
      if (!next.ok() || batch.empty()) { break; }
      for (auto &row : batch) { projected.push_back(std::move(row)); }
    }
    check(projected.size() == 7, "the projection keeps the row count");
    if (projected.size() == 7) {
      check(projected[0][1].text == "a", "the projection reads the projected column");
      check(projected[0][0].is_null, "the projection leaves the other column null");
    }
    fclose(input);
  }
}

// A fixture with many rows, so that the batch boundary is exercised.
void test_large(const std::string &dir, const char *name, int64_t expected_rows)
{
  const std::string path = dir + "/" + name;
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  check_int32(rows, 0, 0, expected_rows, name);
}

// The official sample, which the mysqltest suite uses. It ships base64
// encoded, so the test decodes it first.
void test_official_sample(const std::string &path)
{
  // Read the encoded file and decode it into a temporary file.
  FILE *encoded = fopen(path.c_str(), "rb");
  if (encoded == nullptr) {
    std::printf("FAIL cannot open %s\n", path.c_str());
    ++g_failures;
    return;
  }
  std::string text;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), encoded)) > 0) {
    text.append(chunk, read);
  }
  fclose(encoded);

  static const char *alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string decoded;
  int value = 0;
  int bits = 0;
  for (const char c : text) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') { continue; }
    const char *found = std::strchr(alphabet, c);
    if (found == nullptr) { continue; }
    value = (value << 6) | static_cast<int>(found - alphabet);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<char>((value >> bits) & 0xff));
    }
  }

  const std::string scratch = path + ".decoded";
  FILE *out = fopen(scratch.c_str(), "wb");
  if (out == nullptr) {
    std::printf("FAIL cannot write %s\n", scratch.c_str());
    ++g_failures;
    return;
  }
  fwrite(decoded.data(), 1, decoded.size(), out);
  fclose(out);

  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  const bool ok = read_all(scratch, rows, columns);
  remove(scratch.c_str());
  if (!ok) { return; }
  if (columns.size() != 1) {
    std::printf("FAIL the official sample: expected one column, got %zu\n",
                columns.size());
    ++g_failures;
    return;
  }
  check_int32(rows, 0, 0, 7, "the official sample");
}

// The official sample in its plain form.
void test_official_sample_plain(const std::string &path)
{
  std::vector<ScalarRow> rows;
  std::vector<ColumnSchema> columns;
  if (!read_all(path, rows, columns)) { return; }
  if (columns.size() != 1) {
    std::printf("FAIL the official sample: expected one column, got %zu\n",
                columns.size());
    ++g_failures;
    return;
  }
  check_int32(rows, 0, 0, 7, "the official sample");
}


// A projection that names a column that the file does not have is an error.
// A scan that reads nothing would produce rows of NULLs, which is a wrong
// result instead of a diagnosis.
void test_unknown_projection(const std::string &dir)
{
  const std::string path = dir + "/multi_column.parquet";
  ParquetReader reader;
  ParquetOptions options;
  options.projection.push_back("nosuchcolumn");
  FILE *input = fopen(path.c_str(), "rb");
  if (input == nullptr) {
    std::printf("FAIL cannot open %s\n", path.c_str());
    ++g_failures;
    return;
  }
  const FileStatus status = reader.init(input, options);
  check(!status.ok(), "a projection of a column that is not in the file is rejected");
  // The reader has to stay usable after a failed initialization. It used to
  // go on with a half built reader and crash.
  std::vector<ScalarRow> rows;
  const FileStatus next = reader.next_batch(100, rows);
  check(!next.ok(), "the reader refuses to read after a failed initialization");
  check(rows.empty(), "the reader produces no row after a failed initialization");
  fclose(input);
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 2) {
    std::printf("usage: test_file_sql_parquet <fixture-directory>\n");
    return 2;
  }
  const std::string dir = argv[1];

  // The official sample ships base64 encoded and the mysqltest include
  // decodes it. The test accepts either form.
  const std::string official = dir + "/../simple.parquet.b64";
  const std::string official_plain = dir + "/../simple.parquet";
  std::printf("--- the official sample ---\n");
  if (access(official_plain.c_str(), R_OK) == 0) {
    test_official_sample_plain(official_plain);
  } else if (access(official.c_str(), R_OK) == 0) {
    test_official_sample(official);
  }

  std::printf("--- the official shape ---\n");
  test_official_shape(dir, "v1_plain_snappy.parquet");
  test_official_shape(dir, "v1_plain_none.parquet");
  test_official_shape(dir, "v2_plain_snappy.parquet");
  test_official_shape(dir, "v2_plain_none.parquet");
  test_official_shape(dir, "v2_dict_snappy.parquet");
  test_official_shape(dir, "v2_dict_none.parquet");
  test_official_shape(dir, "v1_dict_snappy.parquet");
  test_official_shape(dir, "v1_dict_none.parquet");
  test_official_shape(dir, "v1_writer.parquet");
  test_official_shape(dir, "required.parquet");

  std::printf("--- nullable columns ---\n");
  test_nullable(dir, "nullable.parquet");
  test_nullable(dir, "nullable_v1.parquet");

  std::printf("--- several row groups ---\n");
  test_row_groups(dir, "multi_row_group.parquet");
  test_row_groups(dir, "multi_row_group_v2.parquet");

  std::printf("--- the other physical types ---\n");
  test_types(dir, "types_v1.parquet");
  test_types(dir, "types_v2.parquet");

  std::printf("--- several columns and the column pruning ---\n");
  test_multi_column(dir, "multi_column.parquet");
  test_multi_column(dir, "multi_column_v2.parquet");

  std::printf("--- an unknown projection ---\n");
  test_unknown_projection(dir);

  std::printf("--- a large file ---\n");
  test_large(dir, "small.parquet", 2000);
  test_large(dir, "large_dict.parquet", 5000);

  std::printf("\n%s (failures=%d)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
  return g_failures == 0 ? 0 : 1;
}
