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

// Behavioural tests for the JSONL and Parquet readers. The tests use the
// product sources directly; no query engine and no substitute implementation
// is involved.

#include "file_sql_jsonl.h"
#include "file_sql_parquet.h"

#include <cstdio>
#include <cstring>
#include <string>

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

std::string type_name(ScalarType type) { return scalar_type_name(type); }

// The column index of a named column, or -1.
int column_index(const std::vector<ColumnSchema> &columns, const std::string &name)
{
  for (size_t i = 0; i < columns.size(); ++i) {
    if (columns[i].name == name) { return static_cast<int>(i); }
  }
  return -1;
}

void test_jsonl(const char *path)
{
  std::printf("--- jsonl %s ---\n", path);
  FILE *input = fopen(path, "rb");
  if (input == nullptr) {
    std::printf("FAIL cannot open %s\n", path);
    ++g_failures;
    return;
  }
  JsonlTableScan scan;
  FileStatus status = scan.prepare(input, JsonlOptions(), -1);
  check(status.ok(), "jsonl prepare succeeds");
  if (!status.ok()) {
    std::printf("  prepare failed: %s\n", status.message.c_str());
    fclose(input);
    return;
  }

  // The field union is id, name, score, active, day, note.
  check(scan.columns().size() == 6, "jsonl exposes the field union");
  check(scan.row_count() == 3, "jsonl counts every record");

  const int id = column_index(scan.columns(), "id");
  const int name = column_index(scan.columns(), "name");
  const int score = column_index(scan.columns(), "score");
  const int active = column_index(scan.columns(), "active");
  const int day = column_index(scan.columns(), "day");
  const int note = column_index(scan.columns(), "note");
  check(id >= 0 && name >= 0 && score >= 0 && active >= 0 && day >= 0 && note >= 0,
        "jsonl exposes every field of the union");

  // The integer and the floating point of the score column widen to DOUBLE.
  check(scan.columns()[score].type == ScalarType::DOUBLE, "jsonl widens the score column to DOUBLE");
  check(scan.columns()[id].type == ScalarType::BIGINT, "jsonl infers a BIGINT id column");
  check(scan.columns()[active].type == ScalarType::BOOLEAN, "jsonl infers a BOOLEAN active column");
  check(scan.columns()[day].type == ScalarType::DATE, "jsonl infers a DATE day column");
  // The score column is missing in one record, so it is nullable.
  check(scan.columns()[score].nullable, "jsonl marks the score column nullable");
  check(scan.columns()[id].nullable == false, "jsonl keeps a present column non nullable");

  std::vector<ScalarRow> rows;
  status = scan.next_batch(100, rows);
  check(status.ok(), "jsonl next_batch succeeds");
  check(rows.size() == 3, "jsonl returns every record");

  if (rows.size() == 3) {
    // The first record carries a score, the second one does not.
    check(!rows[0][score].is_null, "jsonl keeps a present score");
    check(rows[0][score].real > 98.4 && rows[0][score].real < 98.6, "jsonl keeps the score value");
    check(rows[1][score].is_null, "jsonl turns a missing score into NULL");
    check(rows[1][day].type == ScalarType::DATE, "jsonl keeps the day of the second record");
    check(rows[1][note].is_null, "jsonl turns a missing note into NULL");
    check(rows[2][note].text == "ok", "jsonl keeps the note of the third record");
    check(rows[0][active].integer == 1, "jsonl decodes a true value");
    check(rows[1][active].integer == 0, "jsonl decodes a false value");
  }

  // A rescan must deliver the records again.
  status = scan.rescan();
  check(status.ok(), "jsonl rescan succeeds");
  std::vector<ScalarRow> again;
  status = scan.next_batch(100, again);
  check(status.ok(), "jsonl next_batch after rescan succeeds");
  check(again.size() == 3, "jsonl rescan returns every record");

  // A sample size other than -1 is not supported.
  JsonlTableScan limited;
  FileStatus limited_status = limited.prepare(input, JsonlOptions(), 10);
  check(!limited_status.ok(), "jsonl rejects a sample size other than -1");
  fclose(input);
}

// The official fixture ships the parquet file base64 encoded and the mysqltest
// include decodes it before the suite runs. The test decodes it as well when
// it is given the encoded file.
FILE *open_parquet(const char *path, std::string &scratch)
{
  FILE *input = fopen(path, "rb");
  if (input == nullptr) { return nullptr; }
  // A parquet file starts with the magic bytes. Anything else is the base64
  // form of the same file.
  char magic[4] = {0, 0, 0, 0};
  if (fread(magic, 1, 4, input) == 4 && std::memcmp(magic, "PAR1", 4) == 0) {
    rewind(input);
    return input;
  }
  fclose(input);
  input = fopen(path, "rb");
  if (input == nullptr) { return nullptr; }
  std::string encoded;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), input)) > 0) {
    encoded.append(chunk, read);
  }
  fclose(input);
  static const char *alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int value = 0;
  int bits = 0;
  for (const char c : encoded) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') { continue; }
    const char *found = std::strchr(alphabet, c);
    if (found == nullptr) { return nullptr; }
    value = (value << 6) | static_cast<int>(found - alphabet);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      scratch.push_back(static_cast<char>((value >> bits) & 0xff));
    }
  }
  const std::string scratch_path = std::string(path) + ".decoded";
  FILE *scratch_file = fopen(scratch_path.c_str(), "wb");
  if (scratch_file == nullptr) { return nullptr; }
  fwrite(scratch.data(), 1, scratch.size(), scratch_file);
  fclose(scratch_file);
  FILE *result = fopen(scratch_path.c_str(), "rb");
  remove(scratch_path.c_str());
  return result;
}

void test_parquet(const char *path)
{
  std::printf("--- parquet %s ---\n", path);
  std::string scratch;
  FILE *input = open_parquet(path, scratch);
  if (input == nullptr) {
    std::printf("FAIL cannot open %s\n", path);
    ++g_failures;
    return;
  }
  ParquetReader reader;
  FileStatus status = reader.init(input, ParquetOptions());
  check(status.ok(), "parquet init succeeds");
  if (!status.ok()) {
    std::printf("  init failed: %s\n", status.message.c_str());
    fclose(input);
    return;
  }

  check(reader.columns().size() == 1, "parquet exposes one leaf column");
  check(reader.columns()[0].name == "c1", "parquet reads the column name");
  check(reader.columns()[0].type == ScalarType::BIGINT, "parquet maps an INT32 column to BIGINT");
  // The column of the official sample is required, so it never holds a null.
  check(reader.columns()[0].nullable == false,
        "parquet marks a required column as not nullable");

  std::vector<ScalarRow> rows;
  status = reader.next_batch(100, rows);
  check(status.ok(), "parquet next_batch succeeds");
  check(rows.size() == 7, "parquet returns every row");
  check(reader.row_count() == 7, "parquet counts every row");

  if (rows.size() == 7) {
    int64_t sum = 0;
    int64_t minimum = 0;
    int64_t maximum = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
      check(!rows[i][0].is_null, "parquet returns a defined value");
      if (i == 0) { minimum = maximum = rows[i][0].integer; }
      if (rows[i][0].integer < minimum) { minimum = rows[i][0].integer; }
      if (rows[i][0].integer > maximum) { maximum = rows[i][0].integer; }
      sum += rows[i][0].integer;
    }
    check(minimum == 0, "parquet reads the minimum");
    check(maximum == 6, "parquet reads the maximum");
    check(sum == 21, "parquet reads the sum");
  }

  // A rescan must deliver the rows again.
  status = reader.rescan();
  check(status.ok(), "parquet rescan succeeds");
  std::vector<ScalarRow> again;
  status = reader.next_batch(100, again);
  check(status.ok(), "parquet next_batch after rescan succeeds");
  check(again.size() == 7, "parquet rescan returns every row");

  // A projection that names a column that the file does not have is an
  // error. A scan that reads nothing would produce rows of NULLs, which is
  // a wrong result instead of a diagnosis.
  ParquetOptions projection;
  projection.projection.push_back("missing");
  ParquetReader projected;
  std::string second_scratch;
  FILE *second = open_parquet(path, second_scratch);
  if (second != nullptr) {
    status = projected.init(second, projection);
    check(!status.ok(), "parquet rejects a projection of a column that is not in the file");
    // The reader has to stay usable after a failed initialization. It used
    // to go on with a half built reader and crash.
    std::vector<ScalarRow> projected_rows;
    status = projected.next_batch(100, projected_rows);
    check(!status.ok(), "parquet refuses to read after a failed initialization");
    check(projected_rows.empty(), "parquet produces no row after a failed initialization");
    fclose(second);
  }
  fclose(input);
}

// Writes a file with the given content and returns it opened for reading.
FILE *write_scratch(const char *name, const std::string &content)
{
  const std::string path = std::string("/tmp/") + name;
  FILE *file = fopen(path.c_str(), "wb");
  if (file == nullptr) { return nullptr; }
  fwrite(content.data(), 1, content.size(), file);
  fclose(file);
  return fopen(path.c_str(), "rb");
}

// A column that holds an integer and a floating point number at the same
// time is DOUBLE for every one of its values.
void test_jsonl_type_conflict()
{
  FILE *input = write_scratch("file_sql_type_conflict.jsonl",
                              "{\"score\":98.5,\"id\":1}\n"
                              "{\"score\":90,\"id\":2}\n");
  if (input == nullptr) {
    std::printf("FAIL cannot write the scratch file\n");
    ++g_failures;
    return;
  }
  JsonlTableScan scan;
  const FileStatus status = scan.prepare(input, JsonlOptions(), -1);
  check(status.ok(), "the jsonl with a type conflict is prepared");
  if (status.ok()) {
    const int score = column_index(scan.columns(), "score");
    check(score >= 0, "the jsonl has a score column");
    if (score >= 0) {
      check(scan.columns()[score].type == ScalarType::DOUBLE,
            "a column of an integer and a floating point number is DOUBLE");
    }
    std::vector<ScalarRow> rows;
    const FileStatus next = scan.next_batch(100, rows);
    check(next.ok() && rows.size() == 2, "the jsonl with a type conflict has two rows");
    if (rows.size() == 2 && score >= 0) {
      check(rows[0][score].type == ScalarType::DOUBLE, "the first score is DOUBLE");
      check(rows[0][score].real == 98.5, "the first score is 98.5");
      check(rows[1][score].type == ScalarType::DOUBLE, "the second score is DOUBLE");
      check(rows[1][score].real == 90.0, "the second score is 90");
    }
  }
  fclose(input);
}

// A field that a record does not have is NULL.
void test_jsonl_missing_field()
{
  FILE *input = write_scratch("file_sql_missing_field.jsonl",
                              "{\"a\":1,\"b\":2}\n"
                              "{\"a\":3}\n");
  if (input == nullptr) {
    std::printf("FAIL cannot write the scratch file\n");
    ++g_failures;
    return;
  }
  JsonlTableScan scan;
  const FileStatus status = scan.prepare(input, JsonlOptions(), -1);
  check(status.ok(), "the jsonl with a missing field is prepared");
  if (status.ok()) {
    std::vector<ScalarRow> rows;
    const FileStatus next = scan.next_batch(100, rows);
    check(next.ok() && rows.size() == 2, "the jsonl with a missing field has two rows");
    if (rows.size() == 2) {
      check(!rows[0][1].is_null, "the first record has the second field");
      check(rows[1][1].is_null, "the second record has a NULL in the second field");
    }
  }
  fclose(input);
}

// A nested object and an array are rejected.
void test_jsonl_rejects_nested()
{
  {
    FILE *input = write_scratch("file_sql_nested_object.jsonl", "{\"a\":{\"b\":1}}\n");
    if (input == nullptr) {
      std::printf("FAIL cannot write the scratch file\n");
      ++g_failures;
      return;
    }
    JsonlTableScan scan;
    const FileStatus status = scan.prepare(input, JsonlOptions(), -1);
    check(!status.ok(), "a nested object is rejected");
    fclose(input);
  }
  {
    FILE *input = write_scratch("file_sql_nested_array.jsonl", "{\"a\":[1,2]}\n");
    if (input == nullptr) {
      std::printf("FAIL cannot write the scratch file\n");
      ++g_failures;
      return;
    }
    JsonlTableScan scan;
    const FileStatus status = scan.prepare(input, JsonlOptions(), -1);
    check(!status.ok(), "an array is rejected");
    fclose(input);
  }
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 3) {
    std::printf("usage: test_file_sql_jsonl_parquet <jsonl> <parquet>\n");
    return 2;
  }
  test_jsonl(argv[1]);
  test_parquet(argv[2]);
  test_jsonl_type_conflict();
  test_jsonl_missing_field();
  test_jsonl_rejects_nested();
  std::printf("\n%s (failures=%d)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
  return g_failures == 0 ? 0 : 1;
}
