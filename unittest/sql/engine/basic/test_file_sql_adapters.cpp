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

// Behavioural tests for the adapters between the framework of member A and
// the format modules of member C. The tests use the product sources
// directly; no query engine and no substitute implementation is involved.
//
// The tests drive the JSONL and the parquet modules through the interface of
// the framework, so they cover the mapping of the options and the reporting
// of the end of a file, which the framework relies on.

#include "file_sql_adapters.h"
#include "file_sql_reader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

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

// The index of a named column, or -1.
int column_index(const std::vector<ColumnSchema> &columns, const std::string &name)
{
  for (size_t i = 0; i < columns.size(); ++i) {
    if (columns[i].name == name) { return static_cast<int>(i); }
  }
  return -1;
}

// Read every row of a scanner through the interface of the framework.
std::vector<FileTableRow> read_all(FileTableScanner *scanner, bool &saw_end)
{
  std::vector<FileTableRow> rows;
  saw_end = false;
  while (true) {
    std::vector<FileTableRow> batch;
    const FileStatus status = scanner->next_batch(100, batch);
    if (FileCode::END == status.code) {
      saw_end = true;
      break;
    }
    check(status.ok(), "next_batch succeeds");
    if (!status.ok()) { break; }
    if (batch.empty()) {
      // A scanner that never reports the end of the file would make the
      // framework ask for a batch forever.
      std::printf("FAIL next_batch returned an empty batch without the END code\n");
      ++g_failures;
      break;
    }
    for (auto &row : batch) { rows.push_back(std::move(row)); }
  }
  return rows;
}

// The official parquet fixture ships base64 encoded and the mysqltest
// include decodes it before the suite runs. The test decodes it as well when
// it is given the encoded file. The decoded file is written next to the
// encoded one and its path is returned.
std::string decode_parquet(const char *path)
{
  std::string result;
  FILE *input = fopen(path, "rb");
  if (input == nullptr) { return result; }
  // A parquet file starts with the magic bytes. Anything else is the base64
  // form of the same file.
  char magic[4] = {0, 0, 0, 0};
  if (fread(magic, 1, 4, input) == 4 && std::memcmp(magic, "PAR1", 4) == 0) {
    fclose(input);
    return std::string(path);
  }
  rewind(input);
  std::string encoded;
  char chunk[4096];
  size_t read = 0;
  while ((read = fread(chunk, 1, sizeof(chunk), input)) > 0) {
    encoded.append(chunk, read);
  }
  fclose(input);

  std::string decoded;
  static const char *alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int value = 0;
  int bits = 0;
  for (const char c : encoded) {
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
  if (decoded.empty()) { return result; }

  char name_template[] = "/tmp/file_sql_adapters_XXXXXX";
  const int fd = ::mkstemp(name_template);
  if (fd < 0) { return result; }
  result = name_template;
  FILE *output = fdopen(fd, "wb");
  if (output == nullptr) {
    ::close(fd);
    ::unlink(result.c_str());
    result.clear();
    return result;
  }
  fwrite(decoded.data(), 1, decoded.size(), output);
  fclose(output);
  return result;
}

void test_jsonl_through_the_framework(const char *path)
{
  std::printf("--- jsonl through the framework %s ---\n", path);
  FileTableScanner *scanner = create_file_scanner(FileFormat::JSONL);
  check(scanner != nullptr, "the framework creates a jsonl scanner");
  if (scanner == nullptr) { return; }

  FILE *input = fopen(path, "rb");
  check(input != nullptr, "the jsonl file opens");
  if (input == nullptr) {
    delete scanner;
    return;
  }

  FileScanOptions options;
  const FileStatus status = scanner->prepare(input, options);
  check(status.ok(), "the jsonl scanner prepares");
  if (!status.ok()) {
    std::printf("  prepare failed: %s\n", status.message.c_str());
    fclose(input);
    delete scanner;
    return;
  }

  // The field union is id, name, score, active, day, note.
  const std::vector<ColumnSchema> &columns = scanner->columns();
  check(columns.size() == 6, "the jsonl scanner exposes the field union");
  const int score = column_index(columns, "score");
  const int day = column_index(columns, "day");
  check(score >= 0 && day >= 0, "the jsonl scanner exposes the expected columns");

  bool saw_end = false;
  const std::vector<FileTableRow> rows = read_all(scanner, saw_end);
  check(saw_end, "the jsonl scanner reports the end of the file");
  check(rows.size() == 3, "the jsonl scanner returns every record");

  if (rows.size() == 3 && score >= 0 && day >= 0) {
    check(!rows[0][score].is_null, "the first record carries a score");
    check(rows[0][score].real > 98.4 && rows[0][score].real < 98.6, "the first score keeps its value");
    check(rows[1][score].is_null, "the missing score becomes NULL");
    check(rows[1][day].type == ScalarType::DATE, "the day of the second record stays a date");
  }

  // A rescan must deliver the records again. The status of the rescan is
  // reported, a failure must not be swallowed.
  const FileStatus rescan_status = scanner->rescan();
  check(rescan_status.ok(), "the jsonl rescan succeeds");
  bool saw_end_again = false;
  const std::vector<FileTableRow> again = read_all(scanner, saw_end_again);
  check(saw_end_again, "the jsonl scanner reports the end of the file again");
  check(again.size() == 3, "the jsonl scanner rescan returns every record");

  fclose(input);
  delete scanner;
}

void test_parquet_through_the_framework(const char *path)
{
  std::printf("--- parquet through the framework %s ---\n", path);
  FileTableScanner *scanner = create_file_scanner(FileFormat::PARQUET);
  check(scanner != nullptr, "the framework creates a parquet scanner");
  if (scanner == nullptr) { return; }

  const std::string decoded = decode_parquet(path);
  if (decoded.empty()) {
    std::printf("FAIL cannot decode the parquet file %s\n", path);
    ++g_failures;
    delete scanner;
    return;
  }
  FILE *input = fopen(decoded.c_str(), "rb");
  check(input != nullptr, "the parquet file opens");
  if (input == nullptr) {
    delete scanner;
    return;
  }

  FileScanOptions options;
  const FileStatus status = scanner->prepare(input, options);
  check(status.ok(), "the parquet scanner prepares");
  if (!status.ok()) {
    std::printf("  prepare failed: %s\n", status.message.c_str());
    fclose(input);
    delete scanner;
    return;
  }

  const std::vector<ColumnSchema> &columns = scanner->columns();
  check(columns.size() == 1, "the parquet scanner exposes the schema of the file");
  check(columns.size() == 1 && columns[0].name == "c1", "the column of the file is c1");

  bool saw_end = false;
  const std::vector<FileTableRow> rows = read_all(scanner, saw_end);
  check(saw_end, "the parquet scanner reports the end of the file");
  check(rows.size() == 7, "the parquet scanner returns every row");

  if (rows.size() == 7 && columns.size() == 1) {
    int64_t sum = 0;
    int64_t minimum = 0;
    int64_t maximum = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
      check(!rows[i][0].is_null, "the parquet scanner returns a defined value");
      if (i == 0) { minimum = maximum = rows[i][0].integer; }
      sum += rows[i][0].integer;
      if (rows[i][0].integer < minimum) { minimum = rows[i][0].integer; }
      if (rows[i][0].integer > maximum) { maximum = rows[i][0].integer; }
    }
    check(minimum == 0, "the minimum of the file is 0");
    check(maximum == 6, "the maximum of the file is 6");
    check(sum == 21, "the sum of the file is 21");
  }

  // A rescan must deliver the rows again. The status of the rescan is
  // reported, a failure must not be swallowed.
  const FileStatus rescan_status = scanner->rescan();
  check(rescan_status.ok(), "the parquet rescan succeeds");
  bool saw_end_again = false;
  const std::vector<FileTableRow> again = read_all(scanner, saw_end_again);
  check(saw_end_again, "the parquet scanner reports the end of the file again");
  check(again.size() == 7, "the parquet scanner rescan returns every row");

  fclose(input);
  delete scanner;
  ::unlink(decoded.c_str());
}

// The limits of the framework reach the modules of the formats.
void test_the_limits_reach_the_module(const char *path)
{
  std::printf("--- the limits of the framework ---\n");
  FileTableScanner *scanner = create_file_scanner(FileFormat::JSONL);
  check(scanner != nullptr, "the framework creates a jsonl scanner");
  if (scanner == nullptr) { return; }

  FILE *input = fopen(path, "rb");
  if (input == nullptr) {
    delete scanner;
    return;
  }

  FileScanOptions options;
  options.limits.max_columns = 2;
  const FileStatus status = scanner->prepare(input, options);
  check(!status.ok(), "the jsonl module rejects a file that is over the column limit");
  check(status.code == FileCode::RESOURCE_LIMIT, "the column limit is a resource limit");
  if (!status.ok()) {
    std::printf("  the limit was reported as: %s\n", status.message.c_str());
  }

  fclose(input);
  delete scanner;
}

// A projection that names a column the file does not have is a diagnosis and
// not a file of NULLs.
void test_an_unknown_projection_is_rejected(const char *path)
{
  std::printf("--- an unknown projection ---\n");
  FileTableScanner *scanner = create_file_scanner(FileFormat::PARQUET);
  check(scanner != nullptr, "the framework creates a parquet scanner");
  if (scanner == nullptr) { return; }

  const std::string decoded = decode_parquet(path);
  if (decoded.empty()) {
    delete scanner;
    return;
  }
  FILE *input = fopen(decoded.c_str(), "rb");
  if (input == nullptr) {
    delete scanner;
    return;
  }

  FileScanOptions options;
  options.projection.push_back("no_such_column");
  const FileStatus status = scanner->prepare(input, options);
  check(!status.ok(), "the parquet module rejects an unknown projection");

  fclose(input);
  delete scanner;
  ::unlink(decoded.c_str());
}

} // namespace

int main(int argc, char **argv)
{
  if (argc < 3) {
    std::printf("usage: %s <basic.jsonl> <simple.parquet>\n", argv[0]);
    return 2;
  }

  test_jsonl_through_the_framework(argv[1]);
  test_parquet_through_the_framework(argv[2]);
  test_the_limits_reach_the_module(argv[1]);
  test_an_unknown_projection_is_rejected(argv[2]);

  std::printf("\n%s (failures=%d)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures);
  return g_failures == 0 ? 0 : 1;
}
