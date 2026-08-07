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

#ifndef OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_UTILS_H_
#define OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_UTILS_H_

#include <istream>
#include <memory>
#include <string>
#include <vector>

#include "lib/ob_errno.h"
#include "sql/ob_file_scan_common.h"

namespace oceanbase
{
namespace sql
{
class ObParquetReader;

struct ObFileColumnSchema
{
  ObFileColumnSchema()
    : source_name_(), column_name_(), source_type_name_(),
      type_(ObFileColumnType::NULL_TYPE), nullable_(false), max_length_(0)
  {}
  std::string source_name_;
  std::string column_name_;
  std::string source_type_name_;
  ObFileColumnType type_;
  bool nullable_;
  int64_t max_length_;
};

struct ObFileCell
{
  ObFileCell() { reset(); }
  void reset()
  {
    is_null_ = true;
    int_value_ = 0;
    double_value_ = 0;
    bool_value_ = false;
    date_value_ = 0;
    datetime_value_ = 0;
    string_value_.clear();
  }
  bool is_null_;
  int64_t int_value_;
  double double_value_;
  bool bool_value_;
  int32_t date_value_;
  int64_t datetime_value_;
  std::string string_value_;
};

class ObFileScanUtils
{
public:
  static int parse_format(const std::string &format_name, ObFileFormat &format);
  static int detect_format(const std::string &path, ObFileFormat &format);
  static int canonicalize_path(const std::string &path, std::string &canonical_path);
  static int get_file_fingerprint(const std::string &path,
                                  std::string &canonical_path,
                                  int64_t &file_size,
                                  int64_t &modified_time_ns);
  static int get_file_fingerprint(const std::string &path,
                                  std::string &canonical_path,
                                  uint64_t &device,
                                  uint64_t &inode,
                                  int64_t &file_size,
                                  int64_t &modified_time_ns);
  static int get_directory_fingerprint(const std::string &path,
                                       std::string &canonical_path,
                                       uint64_t &device,
                                       uint64_t &inode,
                                       int64_t &modified_time_ns);
  static int infer_schema(const std::string &path,
                          ObFileFormat requested_format,
                          std::vector<ObFileColumnSchema> &columns,
                          int64_t &row_count,
                          std::string &canonical_path,
                          ObFileFormat &actual_format,
                          int64_t &file_size,
                          int64_t &modified_time_ns);
  static int infer_schema(const std::string &path,
                          ObFileFormat requested_format,
                          std::vector<ObFileColumnSchema> &columns,
                          int64_t &row_count,
                          std::string &canonical_path,
                          ObFileFormat &actual_format,
                          uint64_t &device,
                          uint64_t &inode,
                          int64_t &file_size,
                          int64_t &modified_time_ns);
  static const char *format_name(ObFileFormat format);
  static const char *column_type_name(ObFileColumnType type);
};

class ObFileScanReader
{
public:
  ObFileScanReader();
  ~ObFileScanReader();

  int open(const std::string &path,
           ObFileFormat format,
           const std::vector<ObFileColumnSchema> &columns,
           int64_t expected_file_size,
           int64_t expected_modified_time_ns);
  int open(const std::string &path,
           ObFileFormat format,
           const std::vector<ObFileColumnSchema> &columns,
           uint64_t expected_device,
           uint64_t expected_inode,
           int64_t expected_file_size,
           int64_t expected_modified_time_ns);
  int open(const std::string &path,
           ObFileFormat format,
           const std::vector<ObFileColumnSchema> &columns,
           uint64_t expected_device,
           uint64_t expected_inode,
           int64_t expected_file_size,
           int64_t expected_modified_time_ns,
           const std::vector<int64_t> &projected_column_idxs);
  int get_next_row(std::vector<ObFileCell> &cells);
  int rescan();
  void close();
  int64_t current_row_number() const { return current_row_number_; }

private:
  int open_inner();
  int get_next_csv_row(std::vector<ObFileCell> &cells);
  int get_next_jsonl_row(std::vector<ObFileCell> &cells);
  int get_next_parquet_row(std::vector<ObFileCell> &cells);

private:
  std::string path_;
  ObFileFormat format_;
  std::vector<ObFileColumnSchema> columns_;
  std::vector<bool> projected_columns_;
  std::unique_ptr<std::streambuf> stream_buffer_;
  std::unique_ptr<std::istream> stream_;
  int64_t current_row_number_;
  bool csv_header_read_;
  uint64_t expected_device_;
  uint64_t expected_inode_;
  int64_t expected_file_size_;
  int64_t expected_modified_time_ns_;
  bool end_verified_;
  std::unique_ptr<ObParquetReader> parquet_reader_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_BASIC_OB_FILE_SCAN_UTILS_H_
