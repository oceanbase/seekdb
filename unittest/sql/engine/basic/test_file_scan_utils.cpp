/*
 * Copyright (c) 2026 OceanBase.
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

#include <gtest/gtest.h>

#include <fstream>
#include <unistd.h>

#include "share/ob_errno.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

namespace oceanbase
{
namespace sql
{

class TempFile
{
public:
  explicit TempFile(const char *suffix)
  {
    const int suffix_len = static_cast<int>(strlen(suffix));
    const std::string path_template = std::string("/tmp/seekdb_file_scan_XXXXXX") + suffix;
    std::vector<char> path(path_template.begin(), path_template.end());
    path.push_back('\0');
    const int fd = mkstemps(path.data(), suffix_len);
    EXPECT_GE(fd, 0);
    if (fd >= 0) {
      close(fd);
      path_ = path.data();
    }
  }
  ~TempFile()
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
  }
  void write(const std::string &content, bool append = false)
  {
    std::ofstream output(path_, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    ASSERT_TRUE(output.is_open());
    output << content;
    output.close();
  }
  const std::string &path() const { return path_; }

private:
  std::string path_;
};

// Cross-implementation fixture produced by Arrow2: one INT32 column containing 0..6,
// DataPageV2 with a Snappy column codec and an uncompressed value section.
static const unsigned char ARROW2_SNAPPY_V2_PARQUET[] = {
  0x50, 0x41, 0x52, 0x31, 0x15, 0x06, 0x15, 0x3c, 0x15, 0x40, 0x5c, 0x15,
  0x0e, 0x15, 0x00, 0x15, 0x0e, 0x15, 0x00, 0x15, 0x04, 0x15, 0x00, 0x11,
  0x1c, 0x36, 0x00, 0x28, 0x04, 0x06, 0x00, 0x00, 0x00, 0x18, 0x04, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x7f, 0x1c, 0x6c, 0x00, 0x00,
  0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x03, 0x00,
  0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x06, 0x00,
  0x00, 0x00, 0x26, 0x94, 0x01, 0x1c, 0x15, 0x02, 0x19, 0x25, 0x06, 0x00,
  0x19, 0x18, 0x02, 0x63, 0x31, 0x15, 0x02, 0x16, 0x0e, 0x16, 0x88, 0x01,
  0x16, 0x8c, 0x01, 0x26, 0x08, 0x3c, 0x36, 0x00, 0x28, 0x04, 0x06, 0x00,
  0x00, 0x00, 0x18, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15,
  0x04, 0x19, 0x2c, 0x48, 0x04, 0x72, 0x6f, 0x6f, 0x74, 0x15, 0x02, 0x00,
  0x15, 0x02, 0x25, 0x02, 0x18, 0x02, 0x63, 0x31, 0x00, 0x16, 0x0e, 0x19,
  0x1c, 0x19, 0x1c, 0x26, 0x94, 0x01, 0x1c, 0x15, 0x02, 0x19, 0x25, 0x06,
  0x00, 0x19, 0x18, 0x02, 0x63, 0x31, 0x15, 0x02, 0x16, 0x0e, 0x16, 0x88,
  0x01, 0x16, 0x8c, 0x01, 0x26, 0x08, 0x3c, 0x36, 0x00, 0x28, 0x04, 0x06,
  0x00, 0x00, 0x00, 0x18, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x16, 0x8c, 0x01, 0x16, 0x0e, 0x00, 0x19, 0x1c, 0x18, 0x0c, 0x41, 0x52,
  0x52, 0x4f, 0x57, 0x3a, 0x73, 0x63, 0x68, 0x65, 0x6d, 0x61, 0x18, 0xbc,
  0x01, 0x2f, 0x2f, 0x2f, 0x2f, 0x2f, 0x34, 0x51, 0x41, 0x41, 0x41, 0x41,
  0x51, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x4b, 0x41, 0x41, 0x34,
  0x41, 0x44, 0x41, 0x41, 0x4c, 0x41, 0x41, 0x51, 0x41, 0x43, 0x67, 0x41,
  0x41, 0x41, 0x42, 0x51, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
  0x42, 0x42, 0x41, 0x41, 0x4b, 0x41, 0x41, 0x77, 0x41, 0x41, 0x41, 0x41,
  0x49, 0x41, 0x41, 0x51, 0x41, 0x43, 0x67, 0x41, 0x41, 0x41, 0x41, 0x67,
  0x41, 0x41, 0x41, 0x41, 0x49, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
  0x41, 0x41, 0x41, 0x45, 0x41, 0x41, 0x41, 0x41, 0x55, 0x41, 0x41, 0x41, 0x41,
  0x45, 0x41, 0x41, 0x55, 0x41, 0x42, 0x41, 0x41, 0x44, 0x67, 0x41, 0x50,
  0x41, 0x41, 0x51, 0x41, 0x41, 0x41, 0x41, 0x49, 0x41, 0x42, 0x41, 0x41,
  0x41, 0x41, 0x41, 0x59, 0x41, 0x41, 0x41, 0x41, 0x49, 0x41, 0x41, 0x41,
  0x41, 0x41, 0x41, 0x41, 0x41, 0x51, 0x49, 0x63, 0x41, 0x41, 0x41, 0x41,
  0x43, 0x41, 0x41, 0x4d, 0x41, 0x41, 0x51, 0x41, 0x43, 0x77, 0x41, 0x49,
  0x41, 0x41, 0x41, 0x41, 0x49, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41,
  0x41, 0x41, 0x45, 0x41, 0x41, 0x41, 0x41, 0x41, 0x41, 0x67, 0x41, 0x41,
  0x41, 0x47, 0x4d, 0x78, 0x41, 0x41, 0x41, 0x3d, 0x00, 0x18, 0x2c, 0x41,
  0x72, 0x72, 0x6f, 0x77, 0x32, 0x20, 0x2d, 0x20, 0x4e, 0x61, 0x74, 0x69,
  0x76, 0x65, 0x20, 0x52, 0x75, 0x73, 0x74, 0x20, 0x69, 0x6d, 0x70, 0x6c,
  0x65, 0x6d, 0x65, 0x6e, 0x74, 0x61, 0x74, 0x69, 0x6f, 0x6e, 0x20, 0x6f,
  0x66, 0x20, 0x41, 0x72, 0x72, 0x6f, 0x77, 0x00, 0x4e, 0x01, 0x00, 0x00,
  0x50, 0x41, 0x52, 0x31
};

static std::string decode_base64(const char *input)
{
  static const char ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  uint32_t accumulator = 0;
  int bits = 0;
  for (const char *cursor = input; '\0' != *cursor && '=' != *cursor; ++cursor) {
    const char *digit = strchr(ALPHABET, *cursor);
    if (nullptr != digit) {
      accumulator = (accumulator << 6) | static_cast<uint32_t>(digit - ALPHABET);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        output.push_back(static_cast<char>((accumulator >> bits) & 0xff));
      }
    }
  }
  return output;
}

// Impala 1.3 cross-implementation fixtures. Both contain two rows and eleven flat
// primitive columns; one uses Snappy and the other dictionary encoding.
static const char IMPALA_PLAIN_SNAPPY_PARQUET[] =
  "UEFSMRUEFRAVFEwVBBUEAAAIHAYAAAAHAAAAFQAVEhUWLBUEFQQVBhUIAAAJIAIAAAAEAQEDAiZuHBUCGTUGBAAZGAJpZBUC"
  "FgQWXhZmJjYmCAAAFQAVDhUSLBUEFQAVBhUIAAAHGAIAAAAEAQEm3AEcFQAZNQYEABkYCGJvb2xfY29sFQIWBBYwFjQmqAEA"
  "ABUEFRAVFEwVBBUEAAAIHAAAAAABAAAAFQAVEhUWLBUEFQQVBhUIAAAJIAIAAAAEAQEDAiaIAxwVAhk1BgQAGRgLdGlueWlu"
  "dF9jb2wVAhYEFl4WZibQAiaiAgAAFQQVEBUUTBUEFQQAAAgcAAAAAAEAAAAVABUSFRYsFQQVBBUGFQgAAAkgAgAAAAQBAQMC"
  "JsAEHBUCGTUGBAAZGAxzbWFsbGludF9jb2wVAhYEFl4WZiaIBCbaAwAAFQQVEBUUTBUEFQQAAAgcAAAAAAEAAAAVABUSFRYs"
  "FQQVBBUGFQgAAAkgAgAAAAQBAQMCJvoFHBUCGTUGBAAZGAdpbnRfY29sFQIWBBZeFmYmwgUmlAUAABUEFSAVJEwVBBUEAAAQ"
  "PAAAAAAAAAAACgAAAAAAAAAVABUSFRYsFQQVBBUGFQgAAAkgAgAAAAQBAQMCJroHHBUEGTUGBAAZGApiaWdpbnRfY29sFQIW"
  "BBZuFnYmggcmxAYAABUEFRAVFEwVBBUEAAAIHAAAAADNzIw/FQAVEhUWLBUEFQQVBhUIAAAJIAIAAAAEAQEDAibwCBwVCBk1"
  "BgQAGRgJZmxvYXRfY29sFQIWBBZeFmYmuAgmiggAABUEFSAVJEwVBBUEAAAQPAAAAAAAAAAAMzMzMzMzJEAVABUSFRYsFQQV"
  "BBUGFQgAAAkgAgAAAAQBAQMCJrQKHBUKGTUGBAAZGApkb3VibGVfY29sFQIWBBZuFnYm/AkmvgkAABUEFRgVHEwVAhUEAAAM"
  "LAgAAAAwNC8wMS8wORUAFRIVFiwVBBUEFQYVCAAACSACAAAABAEBBAAm8gscFQwZNQYEABkYD2RhdGVfc3RyaW5nX2NvbBUC"
  "FgQWZhZuJroLJoQLAAAVBBUUFRhMFQQVBAAACiQBAAAAMAEAAAAxFQAVEhUWLBUEFQQVBhUIAAAJIAIAAAAEAQEDAia2DRwV"
  "DBk1BgQAGRgKc3RyaW5nX2NvbBUCFgQWYhZqJv4MJswMAAAVBBUwFSxMFQQVBAAAGAAADQE8i3UlAABYR/gNAAAAi3UlABUA"
  "FRIVFiwVBBUEFQYVCAAACSACAAAABAEBAwImhA8cFQYZNQYEABkYDXRpbWVzdGFtcF9jb2wVAhYEFn4WfibMDiaGDgAAFQIZ"
  "zEgGc2NoZW1hFRYAFQIlAhgCaWQAFQAlAhgIYm9vbF9jb2wAFQIlAhgLdGlueWludF9jb2wAFQIlAhgMc21hbGxpbnRfY29s"
  "ABUCJQIYB2ludF9jb2wAFQQlAhgKYmlnaW50X2NvbAAVCCUCGAlmbG9hdF9jb2wAFQolAhgKZG91YmxlX2NvbAAVDCUCGA9k"
  "YXRlX3N0cmluZ19jb2wAFQwlAhgKc3RyaW5nX2NvbAAVBiUCGA10aW1lc3RhbXBfY29sABYEGRwZvCZuHBUCGTUGBAAZGAJp"
  "ZBUCFgQWXhZmJjYmCAAAJtwBHBUAGTUGBAAZGAhib29sX2NvbBUCFgQWMBY0JqgBAAAmiAMcFQIZNQYEABkYC3RpbnlpbnRf"
  "Y29sFQIWBBZeFmYm0AImogIAACbABBwVAhk1BgQAGRgMc21hbGxpbnRfY29sFQIWBBZeFmYmiAQm2gMAACb6BRwVAhk1BgQA"
  "GRgHaW50X2NvbBUCFgQWXhZmJsIFJpQFAAAmugccFQQZNQYEABkYCmJpZ2ludF9jb2wVAhYEFm4WdiaCBybEBgAAJvAIHBUI"
  "GTUGBAAZGAlmbG9hdF9jb2wVAhYEFl4WZia4CCaKCAAAJrQKHBUKGTUGBAAZGApkb3VibGVfY29sFQIWBBZuFnYm/AkmvgkA"
  "ACbyCxwVDBk1BgQAGRgPZGF0ZV9zdHJpbmdfY29sFQIWBBZmFm4mugsmhAsAACa2DRwVDBk1BgQAGRgKc3RyaW5nX2NvbBUC"
  "FgQWYhZqJv4MJswMAAAmhA8cFQYZNQYEABkYDXRpbWVzdGFtcF9jb2wVAhYEFn4WfibMDiaGDgAAFvQIFgQAKE5pbXBhbGEg"
  "dmVyc2lvbiAxLjMuMC1JTlRFUk5BTCAoYnVpbGQgOGE0OGRkYjFlZmY4NDU5MmIzZmMwNmJjNmY1MWVjMTIwZTFmZmZjOSkA"
  "0wIAAFBBUjE=";

static const char IMPALA_DICTIONARY_PARQUET[] =
  "UEFSMRUEFRAVEEwVBBUEAAAAAAAAAQAAABUAFRIVEiwVBBUEFQYVCAAAAgAAAAQBAQMCJmYcFQIZNQYEABkYAmlkFQAWBBZe"
  "Fl4mMiYIAAAVABUOFQ4sFQQVABUGFQgAAAIAAAAEAQEm0AEcFQAZNQYEABkYCGJvb2xfY29sFQAWBBYwFjAmoAEAABUEFRAV"
  "EEwVBBUEAAAAAAAAAQAAABUAFRIVEiwVBBUEFQYVCAAAAgAAAAQBAQMCJvQCHBUCGTUGBAAZGAt0aW55aW50X2NvbBUAFgQW"
  "XhZeJsACJpYCAAAVBBUQFRBMFQQVBAAAAAAAAAEAAAAVABUSFRIsFQQVBBUGFQgAAAIAAAAEAQEDAiakBBwVAhk1BgQAGRgM"
  "c21hbGxpbnRfY29sFQAWBBZeFl4m8AMmxgMAABUEFRAVEEwVBBUEAAAAAAAAAQAAABUAFRIVEiwVBBUEFQYVCAAAAgAAAAQB"
  "AQMCJtYFHBUCGTUGBAAZGAdpbnRfY29sFQAWBBZeFl4mogUm+AQAABUEFSAVIEwVBBUEAAAAAAAAAAAAAAoAAAAAAAAAFQAV"
  "EhUSLBUEFQQVBhUIAAACAAAABAEBAwImjgccFQQZNQYEABkYCmJpZ2ludF9jb2wVABYEFm4WbibaBiagBgAAFQQVEBUQTBUE"
  "FQQAAAAAAADNzIw/FQAVEhUSLBUEFQQVBhUIAAACAAAABAEBAwImvAgcFQgZNQYEABkYCWZsb2F0X2NvbBUAFgQWXhZeJogI"
  "Jt4HAAAVBBUgFSBMFQQVBAAAAAAAAAAAAAAzMzMzMzMkQBUAFRIVEiwVBBUEFQYVCAAAAgAAAAQBAQMCJvgJHBUKGTUGBAAZ"
  "GApkb3VibGVfY29sFQAWBBZuFm4mxAkmigkAABUEFRgVGEwVAhUEAAAIAAAAMDEvMDEvMDkVABUSFRIsFQQVBBUGFQgAAAIA"
  "AAAEAQEEACauCxwVDBk1BgQAGRgPZGF0ZV9zdHJpbmdfY29sFQAWBBZmFmYm+gomyAoAABUEFRQVFEwVBBUEAAABAAAAMAEA"
  "AAAxFQAVEhUSLBUEFQQVBhUIAAACAAAABAEBAwIm6gwcFQwZNQYEABkYCnN0cmluZ19jb2wVABYEFmIWYia2DCaIDAAAFQQV"
  "MBUwTBUEFQQAAAAAAAAAAAAAMXUlAABYR/gNAAAAMXUlABUAFRIVEiwVBBUEFQYVCAAAAgAAAAQBAQMCJrgOHBUGGTUGBAAZ"
  "GA10aW1lc3RhbXBfY29sFQAWBBZ+Fn4mhA4mug0AABUCGcxIBnNjaGVtYRUWABUCJQIYAmlkABUAJQIYCGJvb2xfY29sABUC"
  "JQIYC3RpbnlpbnRfY29sABUCJQIYDHNtYWxsaW50X2NvbAAVAiUCGAdpbnRfY29sABUEJQIYCmJpZ2ludF9jb2wAFQglAhgJ"
  "ZmxvYXRfY29sABUKJQIYCmRvdWJsZV9jb2wAFQwlAhgPZGF0ZV9zdHJpbmdfY29sABUMJQIYCnN0cmluZ19jb2wAFQYlAhgN"
  "dGltZXN0YW1wX2NvbAAWBBkcGbwmZhwVAhk1BgQAGRgCaWQVABYEFl4WXiYyJggAACbQARwVABk1BgQAGRgIYm9vbF9jb2wV"
  "ABYEFjAWMCagAQAAJvQCHBUCGTUGBAAZGAt0aW55aW50X2NvbBUAFgQWXhZeJsACJpYCAAAmpAQcFQIZNQYEABkYDHNtYWxs"
  "aW50X2NvbBUAFgQWXhZeJvADJsYDAAAm1gUcFQIZNQYEABkYB2ludF9jb2wVABYEFl4WXiaiBSb4BAAAJo4HHBUEGTUGBAAZ"
  "GApiaWdpbnRfY29sFQAWBBZuFm4m2gYmoAYAACa8CBwVCBk1BgQAGRgJZmxvYXRfY29sFQAWBBZeFl4miAgm3gcAACb4CRwV"
  "Chk1BgQAGRgKZG91YmxlX2NvbBUAFgQWbhZuJsQJJooJAAAmrgscFQwZNQYEABkYD2RhdGVfc3RyaW5nX2NvbBUAFgQWZhZm"
  "JvoKJsgKAAAm6gwcFQwZNQYEABkYCnN0cmluZ19jb2wVABYEFmIWYia2DCaIDAAAJrgOHBUGGTUGBAAZGA10aW1lc3RhbXBf"
  "Y29sFQAWBBZ+Fn4mhA4mug0AABaoCBYEAChOaW1wYWxhIHZlcnNpb24gMS4zLjAtSU5URVJOQUwgKGJ1aWxkIDhhNDhkZGIx"
  "ZWZmODQ1OTJiM2ZjMDZiYzZmNTFlYzEyMGUxZmZmYzkpANMCAABQQVIx";

TEST(TestFileScanUtils, csv_schema_and_streaming)
{
  TempFile file(".csv");
  file.write("id,score,active,day,created,note,id\r\n"
             "1,1.5,true,2026-07-31,2026-07-31 12:13:14,\"hello,\nworld\",9\r\n"
             "2,2,false,,,plain,10\r\n");
  std::vector<ObFileColumnSchema> columns;
  std::string canonical_path;
  ObFileFormat format = ObFileFormat::INVALID;
  int64_t rows = 0;
  int64_t file_size = 0;
  int64_t modified_time_ns = 0;
  uint64_t device = 0;
  uint64_t inode = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(file.path(), ObFileFormat::AUTO,
                                                      columns, rows, canonical_path, format,
                                                      device, inode, file_size, modified_time_ns));
  EXPECT_NE(0, inode);
  ASSERT_EQ(ObFileFormat::CSV, format);
  ASSERT_EQ(2, rows);
  ASSERT_EQ(7, columns.size());
  EXPECT_EQ("id__2", columns[6].column_name_);
  EXPECT_EQ(ObFileColumnType::BIGINT, columns[0].type_);
  EXPECT_EQ("CSV/BIGINT", columns[0].source_type_name_);
  EXPECT_EQ(ObFileColumnType::DOUBLE, columns[1].type_);
  EXPECT_EQ(ObFileColumnType::BOOLEAN, columns[2].type_);
  EXPECT_EQ(ObFileColumnType::DATE, columns[3].type_);
  EXPECT_EQ(ObFileColumnType::DATETIME, columns[4].type_);
  EXPECT_EQ(ObFileColumnType::VARCHAR, columns[5].type_);
  EXPECT_TRUE(columns[3].nullable_);

  ObFileScanReader reader;
  ASSERT_EQ(OB_SUCCESS, reader.open(canonical_path, format, columns, device, inode,
                                    file_size, modified_time_ns));
  std::vector<ObFileCell> cells;
  ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
  EXPECT_EQ(1, cells[0].int_value_);
  EXPECT_DOUBLE_EQ(1.5, cells[1].double_value_);
  EXPECT_TRUE(cells[2].bool_value_);
  EXPECT_EQ("hello,\nworld", cells[5].string_value_);
  ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
  EXPECT_EQ(2, cells[0].int_value_);
  EXPECT_TRUE(cells[3].is_null_);
  EXPECT_EQ(OB_ITER_END, reader.get_next_row(cells));
}

TEST(TestFileScanUtils, jsonl_union_schema)
{
  TempFile file(".jsonl");
  file.write("{\"id\":1,\"name\":\"alice\",\"flag\":true}\n"
             "{\"id\":2.5,\"extra\":\"2026-07-31\"}\n");
  std::vector<ObFileColumnSchema> columns;
  std::string canonical_path;
  ObFileFormat format = ObFileFormat::INVALID;
  int64_t rows = 0;
  int64_t file_size = 0;
  int64_t modified_time_ns = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(file.path(), ObFileFormat::AUTO,
                                                      columns, rows, canonical_path, format,
                                                      file_size, modified_time_ns));
  ASSERT_EQ(ObFileFormat::JSONL, format);
  ASSERT_EQ(2, rows);
  ASSERT_EQ(4, columns.size());
  auto find_column = [&columns](const char *name) {
    int64_t result = -1;
    for (int64_t i = 0; i < static_cast<int64_t>(columns.size()); ++i) {
      if (columns[i].source_name_ == name) {
        result = i;
        break;
      }
    }
    return result;
  };
  const int64_t id_idx = find_column("id");
  const int64_t name_idx = find_column("name");
  const int64_t flag_idx = find_column("flag");
  const int64_t extra_idx = find_column("extra");
  ASSERT_GE(id_idx, 0);
  ASSERT_GE(name_idx, 0);
  ASSERT_GE(flag_idx, 0);
  ASSERT_GE(extra_idx, 0);
  EXPECT_EQ(ObFileColumnType::DOUBLE, columns[id_idx].type_);
  EXPECT_TRUE(columns[name_idx].nullable_);
  EXPECT_TRUE(columns[flag_idx].nullable_);
  EXPECT_EQ(ObFileColumnType::DATE, columns[extra_idx].type_);

  ObFileScanReader reader;
  ASSERT_EQ(OB_SUCCESS, reader.open(canonical_path, format, columns, file_size, modified_time_ns));
  std::vector<ObFileCell> cells;
  ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
  EXPECT_DOUBLE_EQ(1.0, cells[id_idx].double_value_);
  EXPECT_EQ("alice", cells[name_idx].string_value_);
  EXPECT_TRUE(cells[extra_idx].is_null_);
  ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
  EXPECT_DOUBLE_EQ(2.5, cells[id_idx].double_value_);
  EXPECT_TRUE(cells[name_idx].is_null_);
}

TEST(TestFileScanUtils, rejects_changed_file_and_nested_json)
{
  TempFile file(".jsonl");
  file.write("{\"id\":1}\n");
  std::vector<ObFileColumnSchema> columns;
  std::string canonical_path;
  ObFileFormat format = ObFileFormat::INVALID;
  int64_t rows = 0;
  int64_t file_size = 0;
  int64_t modified_time_ns = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(file.path(), ObFileFormat::AUTO,
                                                      columns, rows, canonical_path, format,
                                                      file_size, modified_time_ns));
  file.write("{\"id\":2}\n", true);
  ObFileScanReader reader;
  EXPECT_EQ(OB_SCHEMA_EAGAIN,
            reader.open(canonical_path, format, columns, file_size, modified_time_ns));

  TempFile nested(".jsonl");
  nested.write("{\"nested\":{\"value\":1}}\n");
  EXPECT_EQ(OB_NOT_SUPPORTED,
            ObFileScanUtils::infer_schema(nested.path(), ObFileFormat::AUTO,
                                          columns, rows, canonical_path, format,
                                          file_size, modified_time_ns));
}

TEST(TestFileScanUtils, detects_mutation_during_scan_and_directory_fingerprint)
{
  TempFile file(".csv");
  file.write("id\n1\n2\n");
  std::vector<ObFileColumnSchema> columns;
  std::string canonical_path;
  ObFileFormat format = ObFileFormat::INVALID;
  uint64_t device = 0;
  uint64_t inode = 0;
  int64_t rows = 0;
  int64_t file_size = 0;
  int64_t modified_time_ns = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(
    file.path(), ObFileFormat::AUTO, columns, rows, canonical_path, format,
    device, inode, file_size, modified_time_ns));
  ObFileScanReader reader;
  ASSERT_EQ(OB_SUCCESS, reader.open(canonical_path, format, columns, device, inode,
                                    file_size, modified_time_ns));
  std::vector<ObFileCell> cells;
  ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
  file.write("3\n", true);
  int ret = OB_SUCCESS;
  while (OB_SUCCESS == ret) {
    ret = reader.get_next_row(cells);
  }
  EXPECT_EQ(OB_SCHEMA_EAGAIN, ret);

  std::string directory_path;
  uint64_t directory_device = 0;
  uint64_t directory_inode = 0;
  int64_t directory_mtime = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::get_directory_fingerprint(
    "/tmp", directory_path, directory_device, directory_inode, directory_mtime));
  EXPECT_FALSE(directory_path.empty());
  EXPECT_NE(0, directory_inode);
}

TEST(TestFileScanUtils, parquet_arrow2_snappy_v2)
{
  TempFile file(".parquet");
  file.write(std::string(reinterpret_cast<const char *>(ARROW2_SNAPPY_V2_PARQUET),
                         sizeof(ARROW2_SNAPPY_V2_PARQUET)));
  std::vector<ObFileColumnSchema> columns;
  std::string canonical_path;
  ObFileFormat format = ObFileFormat::INVALID;
  uint64_t device = 0;
  uint64_t inode = 0;
  int64_t rows = 0;
  int64_t file_size = 0;
  int64_t modified_time_ns = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(
    file.path(), ObFileFormat::AUTO, columns, rows, canonical_path, format,
    device, inode, file_size, modified_time_ns));
  ASSERT_EQ(ObFileFormat::PARQUET, format);
  ASSERT_EQ(1, columns.size());
  ASSERT_EQ(7, rows);
  EXPECT_EQ("INT32", columns[0].source_type_name_);
  ObFileScanReader reader;
  ASSERT_EQ(OB_SUCCESS, reader.open(canonical_path, format, columns, device, inode,
                                    file_size, modified_time_ns));
  std::vector<ObFileCell> cells;
  for (int64_t expected = 0; expected < rows; ++expected) {
    ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
    ASSERT_EQ(1, cells.size());
    EXPECT_EQ(expected, cells[0].int_value_);
  }
  EXPECT_EQ(OB_ITER_END, reader.get_next_row(cells));
  ASSERT_EQ(OB_SUCCESS, reader.rescan());
  ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
  EXPECT_EQ(0, cells[0].int_value_);

  TempFile corrupted(".parquet");
  std::string corrupted_data(reinterpret_cast<const char *>(ARROW2_SNAPPY_V2_PARQUET),
                             sizeof(ARROW2_SNAPPY_V2_PARQUET));
  corrupted_data[0] = 'X';
  corrupted.write(corrupted_data);
  EXPECT_EQ(OB_INVALID_DATA, ObFileScanUtils::infer_schema(
    corrupted.path(), ObFileFormat::PARQUET, columns, rows, canonical_path, format,
    device, inode, file_size, modified_time_ns));
}

TEST(TestFileScanUtils, parquet_impala_plain_snappy_and_dictionary)
{
  TempFile plain_snappy(".parquet");
  TempFile dictionary(".parquet");
  plain_snappy.write(decode_base64(IMPALA_PLAIN_SNAPPY_PARQUET));
  std::string dictionary_data = decode_base64(IMPALA_DICTIONARY_PARQUET);
  // Change the bool column definition levels from an RLE run [1, 1] to a
  // bit-packed run [1, 0]. The page size stays unchanged and row two is null.
  ASSERT_GT(dictionary_data.size(), 101);
  ASSERT_EQ(0x04, static_cast<unsigned char>(dictionary_data[101]));
  dictionary_data[101] = 0x03;
  dictionary.write(dictionary_data);
  const std::string paths[] = {plain_snappy.path(), dictionary.path()};
  for (size_t fixture_idx = 0; fixture_idx < 2; ++fixture_idx) {
    std::vector<ObFileColumnSchema> columns;
    std::string canonical_path;
    ObFileFormat format = ObFileFormat::INVALID;
    uint64_t device = 0;
    uint64_t inode = 0;
    int64_t rows = 0;
    int64_t file_size = 0;
    int64_t modified_time_ns = 0;
    ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(
      paths[fixture_idx], ObFileFormat::AUTO, columns, rows, canonical_path, format,
      device, inode, file_size, modified_time_ns));
    ASSERT_EQ(11, columns.size());
    ASSERT_EQ(2, rows);
    EXPECT_EQ("INT96", columns[10].source_type_name_);
    ObFileScanReader reader;
    ASSERT_EQ(OB_SUCCESS, reader.open(canonical_path, format, columns, device, inode,
                                      file_size, modified_time_ns));
    std::vector<ObFileCell> cells;
    ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
    EXPECT_EQ(fixture_idx == 0 ? 6 : 0, cells[0].int_value_);
    EXPECT_TRUE(cells[1].bool_value_);
    ASSERT_EQ(OB_SUCCESS, reader.get_next_row(cells));
    EXPECT_EQ(fixture_idx == 0 ? 7 : 1, cells[0].int_value_);
    EXPECT_FALSE(cells[1].bool_value_);
    EXPECT_EQ(1 == fixture_idx, cells[1].is_null_);
    EXPECT_EQ(OB_ITER_END, reader.get_next_row(cells));
  }

  TempFile projected_fixture(".parquet");
  std::string malformed_unprojected = decode_base64(IMPALA_DICTIONARY_PARQUET);
  ASSERT_GT(malformed_unprojected.size(), 4);
  malformed_unprojected[4] = static_cast<char>(0xff); // corrupt the id column's first page header
  projected_fixture.write(malformed_unprojected);
  std::vector<ObFileColumnSchema> columns;
  std::string canonical_path;
  ObFileFormat format = ObFileFormat::INVALID;
  uint64_t device = 0, inode = 0;
  int64_t rows = 0, file_size = 0, modified_time_ns = 0;
  ASSERT_EQ(OB_SUCCESS, ObFileScanUtils::infer_schema(
    projected_fixture.path(), ObFileFormat::PARQUET, columns, rows, canonical_path, format,
    device, inode, file_size, modified_time_ns));
  ObFileScanReader projected_reader;
  const std::vector<int64_t> bool_only{1};
  ASSERT_EQ(OB_SUCCESS, projected_reader.open(
    canonical_path, format, columns, device, inode, file_size, modified_time_ns, bool_only));
  std::vector<ObFileCell> projected_cells;
  ASSERT_EQ(OB_SUCCESS, projected_reader.get_next_row(projected_cells));
  EXPECT_TRUE(projected_cells[0].is_null_);
  EXPECT_TRUE(projected_cells[1].bool_value_);
  ASSERT_EQ(OB_SUCCESS, projected_reader.get_next_row(projected_cells));
  EXPECT_FALSE(projected_cells[1].bool_value_);
  EXPECT_EQ(OB_ITER_END, projected_reader.get_next_row(projected_cells));
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
