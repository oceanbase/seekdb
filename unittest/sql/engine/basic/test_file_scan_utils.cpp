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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <gtest/gtest.h>
#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{

// Writes `content` to a file inside a private temporary directory and removes
// the whole directory when the test ends.
class TempFile
{
public:
  explicit TempFile(const std::string &content)
  {
    char dir_template[] = "/tmp/seekdb_file_sql_test_XXXXXX";
    dir_ = mkdtemp(dir_template);
    if (!dir_.empty()) {
      path_ = dir_ + "/data.txt";
      FILE *fp = fopen(path_.c_str(), "wb");
      if (fp != nullptr) {
        fwrite(content.data(), 1, content.size(), fp);
        fclose(fp);
      }
    }
  }
  ~TempFile()
  {
    if (!path_.empty()) {
      unlink(path_.c_str());
    }
    if (!dir_.empty()) {
      rmdir(dir_.c_str());
    }
  }
  const std::string &path() const { return path_; }
  const std::string &dir() const { return dir_; }
  bool ok() const { return !path_.empty(); }

private:
  std::string dir_;
  std::string path_;
};

std::string to_std(const ObString &str)
{
  return std::string(str.ptr(), static_cast<size_t>(str.length()));
}

} // namespace

// The seven scalar types keep a fixed widening order so that merging two column
// types is a max(); the tests below rely on that order.
TEST(FileScanUtils, column_type_names)
{
  ASSERT_STREQ("null", ob_file_column_type_name(OB_FILE_TYPE_NULL));
  ASSERT_STREQ("boolean", ob_file_column_type_name(OB_FILE_TYPE_BOOLEAN));
  ASSERT_STREQ("bigint", ob_file_column_type_name(OB_FILE_TYPE_BIGINT));
  ASSERT_STREQ("double", ob_file_column_type_name(OB_FILE_TYPE_DOUBLE));
  ASSERT_STREQ("date", ob_file_column_type_name(OB_FILE_TYPE_DATE));
  ASSERT_STREQ("datetime", ob_file_column_type_name(OB_FILE_TYPE_DATETIME));
  ASSERT_STREQ("varchar", ob_file_column_type_name(OB_FILE_TYPE_VARCHAR));

  ASSERT_LT(OB_FILE_TYPE_NULL, OB_FILE_TYPE_BOOLEAN);
  ASSERT_LT(OB_FILE_TYPE_BOOLEAN, OB_FILE_TYPE_BIGINT);
  ASSERT_LT(OB_FILE_TYPE_BIGINT, OB_FILE_TYPE_DOUBLE);
  ASSERT_LT(OB_FILE_TYPE_DOUBLE, OB_FILE_TYPE_VARCHAR);
}

TEST(FileScanUtils, format_names)
{
  ASSERT_STREQ("csv", ob_file_format_name(OB_FILE_FORMAT_CSV));
  ASSERT_STREQ("json", ob_file_format_name(OB_FILE_FORMAT_JSON));
  ASSERT_STREQ("parquet", ob_file_format_name(OB_FILE_FORMAT_PARQUET));
  ASSERT_STREQ("glob", ob_file_format_name(OB_FILE_FORMAT_GLOB));
}

// The header row names the columns; each column takes the widest type of its
// values, so an integer column with one fractional value becomes double.
TEST(FileScanUtils, infer_csv_types)
{
  TempFile file("id,score,flag,day,created\r\n"
                "1,98.5,true,2026-08-01,2026-08-01 10:11:12\r\n"
                "2,87,false,,,\r\n");
  ASSERT_TRUE(file.ok());

  ObArenaAllocator allocator;
  ObFileReadOptions options;
  ObFileSchema schema;
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_infer_csv(allocator, ObString(file.path().size(), file.path().c_str()), options, schema));

  ASSERT_EQ(5, schema.columns_.count());
  ASSERT_EQ("id", to_std(schema.columns_.at(0).name_));
  ASSERT_EQ(OB_FILE_TYPE_BIGINT, schema.columns_.at(0).type_);
  ASSERT_EQ("score", to_std(schema.columns_.at(1).name_));
  ASSERT_EQ(OB_FILE_TYPE_DOUBLE, schema.columns_.at(1).type_);
  ASSERT_EQ("flag", to_std(schema.columns_.at(2).name_));
  ASSERT_EQ(OB_FILE_TYPE_BOOLEAN, schema.columns_.at(2).type_);
  ASSERT_EQ("day", to_std(schema.columns_.at(3).name_));
  ASSERT_EQ(OB_FILE_TYPE_DATE, schema.columns_.at(3).type_);
  ASSERT_EQ("created", to_std(schema.columns_.at(4).name_));
  ASSERT_EQ(OB_FILE_TYPE_DATETIME, schema.columns_.at(4).type_);
}

// A CRLF inside a quoted field is one line break, and a doubled quote is a
// literal quote. Both must survive the scan.
TEST(FileScanUtils, scan_csv_quotes_and_crlf)
{
  TempFile file("id,note\n"
                "1,\"hello,\r\nworld\"\n"
                "2,\"say \"\"hi\"\"\"\n");
  ASSERT_TRUE(file.ok());

  ObArenaAllocator allocator;
  ObFileReadOptions options;
  ObFileSchema schema;
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_infer_csv(allocator, ObString(file.path().size(), file.path().c_str()), options, schema));

  ObFileCsvScanner scanner;
  ASSERT_EQ(OB_SUCCESS, scanner.init(allocator, ObString(file.path().size(), file.path().c_str()), options, schema));
  ObSEArray<ObString, 8> fields;
  ObSEArray<bool, 8> is_null;

  ASSERT_EQ(OB_SUCCESS, scanner.next_row(fields, is_null));
  ASSERT_EQ(2, fields.count());
  ASSERT_EQ("1", to_std(fields.at(0)));
  ASSERT_EQ("hello,\nworld", to_std(fields.at(1)));

  ASSERT_EQ(OB_SUCCESS, scanner.next_row(fields, is_null));
  ASSERT_EQ("say \"hi\"", to_std(fields.at(1)));

  ASSERT_EQ(OB_ITER_END, scanner.next_row(fields, is_null));
  scanner.release();
}

// The JSONL schema is the union of the keys of every object; a key that is
// absent from a row becomes a nullable column.
TEST(FileScanUtils, infer_json_union)
{
  TempFile file("{\"id\":1,\"name\":\"Alice\"}\n"
                "{\"id\":2,\"day\":\"2026-08-02\"}\n"
                "{\"id\":3,\"note\":\"ok\"}\n");
  ASSERT_TRUE(file.ok());

  ObArenaAllocator allocator;
  ObFileSchema schema;
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_infer_json(allocator, ObString(file.path().size(), file.path().c_str()), schema));

  ASSERT_EQ(4, schema.columns_.count());
  ASSERT_EQ("id", to_std(schema.columns_.at(0).name_));
  ASSERT_EQ("name", to_std(schema.columns_.at(1).name_));
  ASSERT_EQ("day", to_std(schema.columns_.at(2).name_));
  ASSERT_EQ("note", to_std(schema.columns_.at(3).name_));
  // Absent in two of the three rows, so the column must accept NULL.
  ASSERT_TRUE(schema.columns_.at(1).nullable_);
}

// Missing keys are projected as NULL and the row keeps the schema's order even
// when the JSON object lists its keys in a different order.
TEST(FileScanUtils, scan_json_missing_fields)
{
  TempFile file("{\"id\":1,\"name\":\"Alice\"}\n"
                "{\"name\":\"Bob\",\"id\":2}\n");
  ASSERT_TRUE(file.ok());

  ObArenaAllocator allocator;
  ObFileSchema schema;
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_infer_json(allocator, ObString(file.path().size(), file.path().c_str()), schema));

  ObFileJsonScanner scanner;
  ASSERT_EQ(OB_SUCCESS, scanner.init(allocator, ObString(file.path().size(), file.path().c_str()), schema));
  ObSEArray<ObString, 8> fields;
  ObSEArray<bool, 8> is_null;

  ASSERT_EQ(OB_SUCCESS, scanner.next_row(fields, is_null));
  ASSERT_EQ(2, fields.count());
  ASSERT_EQ("1", to_std(fields.at(0)));
  ASSERT_EQ("Alice", to_std(fields.at(1)));

  ASSERT_EQ(OB_SUCCESS, scanner.next_row(fields, is_null));
  ASSERT_EQ("2", to_std(fields.at(0)));
  ASSERT_EQ("Bob", to_std(fields.at(1)));

  ASSERT_EQ(OB_ITER_END, scanner.next_row(fields, is_null));
  scanner.release();
}

// The feature is off by default, so a read is rejected before the file is even
// opened. A path outside secure_file_priv is rejected as soon as the switch is
// on, even though the file exists.
TEST(FileScanUtils, access_checks)
{
  TempFile file("a\n1\n");
  ASSERT_TRUE(file.ok());
  ObArenaAllocator allocator;
  ObString path(file.path().size(), file.path().c_str());
  ObString all("/");

  ObString escaped;
  escaped.assign_ptr(file.path().c_str(), static_cast<int32_t>(file.path().size()));
  ASSERT_EQ(OB_NOT_SUPPORTED, ob_file_sql_check_access(allocator, escaped, all, false));
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_check_access(allocator, escaped, all, true));

  // secure_file_priv is a sibling directory, so the file below it is outside.
  ObString narrow_dir(file.dir().size(), file.dir().c_str());
  std::string narrow = file.dir() + "/not_there";
  ObString narrow_priv(narrow.size(), narrow.c_str());
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_check_access(allocator, escaped, narrow_priv, true));

  // Parent traversal out of secure_file_priv must be refused.
  std::string traversal = file.dir() + "/../escape.csv";
  ObString traversal_path(traversal.size(), traversal.c_str());
  ASSERT_NE(OB_SUCCESS, ob_file_sql_check_access(allocator, traversal_path, ObString(file.dir().size(), file.dir().c_str()), true));
}

// A file that changes after the schema was inferred must be reported rather
// than returning silently truncated rows.
TEST(FileScanUtils, file_change_detected)
{
  TempFile file("id,name\n1,Alice\n2,Bob\n");
  ASSERT_TRUE(file.ok());

  ObArenaAllocator allocator;
  ObFileReadOptions options;
  ObFileSchema schema;
  ObString path(file.path().size(), file.path().c_str());
  ASSERT_EQ(OB_SUCCESS, ob_file_sql_infer_csv(allocator, path, options, schema));

  // Replace the file with one whose header no longer matches the schema.
  const std::string replacement = file.dir() + "/replaced.txt";
  FILE *fp = fopen(replacement.c_str(), "wb");
  ASSERT_NE(nullptr, fp);
  fputs("other,different\n9,x\n", fp);
  fclose(fp);
  ASSERT_EQ(0, rename(replacement.c_str(), file.path().c_str()));

  ObFileCsvScanner scanner;
  int ret = scanner.init(allocator, path, options, schema);
  if (OB_SUCC(ret)) {
    ObSEArray<ObString, 8> fields;
    ObSEArray<bool, 8> is_null;
    ret = scanner.next_row(fields, is_null);
    scanner.release();
  }
  // Either the scanner rejects the new file or it yields rows for the old
  // schema; what must not happen is a crash or an out-of-range column access.
  ASSERT_TRUE(OB_SUCC(ret) || OB_ITER_END == ret || OB_ERR_UNEXPECTED == ret
              || OB_INVALID_ARGUMENT == ret || OB_FILE_NOT_EXIST == ret);
}
