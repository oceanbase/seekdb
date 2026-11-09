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

#pragma once

#include "common/datum/ob_datum.h"
#include "common/object/ob_obj_type.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_array.h"
#include "lib/file/ob_file.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{

/// Logical column types a file scan can produce.
enum FileScanType : int8_t
{
  FILE_SCAN_TYPE_NULL = 0,
  FILE_SCAN_TYPE_BIGINT,
  FILE_SCAN_TYPE_DOUBLE,
  FILE_SCAN_TYPE_BOOLEAN,
  FILE_SCAN_TYPE_VARCHAR,
  FILE_SCAN_TYPE_DATE,
  FILE_SCAN_TYPE_DATETIME,
};

enum FileScanFormat : int8_t
{
  FILE_SCAN_FORMAT_CSV = 0,
  FILE_SCAN_FORMAT_JSONL = 1,
  FILE_SCAN_FORMAT_PARQUET = 2,
};

/// The kind of one raw cell, used to infer the column type.
enum FileScanValueType : int8_t
{
  FILE_SCAN_VALUE_NULL = 0,
  FILE_SCAN_VALUE_BOOL,
  FILE_SCAN_VALUE_INT,
  FILE_SCAN_VALUE_DOUBLE,
  FILE_SCAN_VALUE_VARCHAR,
  FILE_SCAN_VALUE_DATE,
  FILE_SCAN_VALUE_DATETIME,
};

/// Bit mask of the value kinds observed in one column.
enum FileScanValueMask : uint32_t
{
  FILE_SCAN_MASK_NULL = 1u << FILE_SCAN_VALUE_NULL,
  FILE_SCAN_MASK_BOOL = 1u << FILE_SCAN_VALUE_BOOL,
  FILE_SCAN_MASK_INT = 1u << FILE_SCAN_VALUE_INT,
  FILE_SCAN_MASK_DOUBLE = 1u << FILE_SCAN_VALUE_DOUBLE,
  FILE_SCAN_MASK_VARCHAR = 1u << FILE_SCAN_VALUE_VARCHAR,
  FILE_SCAN_MASK_DATE = 1u << FILE_SCAN_VALUE_DATE,
  FILE_SCAN_MASK_DATETIME = 1u << FILE_SCAN_VALUE_DATETIME,
};

struct FileScanColumn
{
  ObString name_;
  FileScanType type_;
  bool nullable_;
  FileScanColumn() : type_(FILE_SCAN_TYPE_NULL), nullable_(true) {}
};

/// Options accepted by read_csv()/read_json()/read_parquet().
struct FileScanOptions
{
  FileScanFormat format_;
  bool has_header_;
  char field_delim_;
  char quote_char_;
  char escape_char_;
  ObString null_str_;
  int64_t sample_size_;
  common::ObSEArray<ObString, 4, common::ModulePageAllocator, true> column_names_;
  common::ObSEArray<FileScanType, 4, common::ModulePageAllocator, true> column_types_;
  FileScanOptions();
};

/// A row of raw, untyped string fields.
typedef common::ObSEArray<ObString, 16, common::ModulePageAllocator, true> FileScanFields;

/// Streaming reader over one file.
class FileScanReader
{
public:
  virtual ~FileScanReader() {}
  /// Open the file described by a options.
  virtual int open(const ObString &path, const FileScanOptions &options, ObIAllocator &allocator) = 0;
  /// Fetch the next row. Returns OB_ITER_END when the file is exhausted.
  virtual int next_row() = 0;
  /// The fields of the row fetched by the last next_row() call.
  virtual const FileScanFields &fields() const = 0;
  virtual void close() = 0;
  /// Number of rows the file holds, or -1 when it is not known upfront.
  virtual int64_t row_count() const { return -1; }
};

class FileScanUtils
{
public:
  /// Deduce the schema of a csv/jsonl/parquet file.
  static int infer_schema(const ObString &path,
                          const FileScanOptions &options,
                          common::ObIArray<FileScanColumn> &columns,
                          ObIAllocator &allocator);
  /// Discover the files matching a glob pattern.
  static int glob_files(const ObString &pattern,
                        common::ObIArray<ObString> &files,
                        ObIAllocator &allocator);
  /// Classify one raw csv cell.
  static FileScanValueType classify_csv_value(const ObString &value);
  /// Merge the observed value kinds of a column into its final type.
  static FileScanType merge_types(uint32_t value_mask);
  /// The file scan type that carries the given sql type.
  static FileScanType to_file_scan_type(ObObjType type);
  /// The SQL type used to carry a file scan column.
  static ObObjType to_obj_type(FileScanType type);
  /// The name shown by describe select.
  static const char *type_name(FileScanType type);
  /// Create a reader that streams the rows of one file.
  static int create_reader(const ObString &path,
                           const FileScanOptions &options,
                           ObIAllocator &allocator,
                           FileScanReader *&reader);
  static void destroy_reader(FileScanReader *reader);
  /// Convert a raw cell into a datum of the given type.
  static int set_datum(const ObString &value, FileScanType type, ObDatum &datum);
  /// Parse a date literal into year/month/day. Returns OB_SUCCESS when valid.
  static int parse_date(const ObString &value, int32_t &year, int32_t &month, int32_t &day);
  /// Parse a datetime literal into its parts. Returns OB_SUCCESS when valid.
  static int parse_datetime(const ObString &value,
                            int32_t &year,
                            int32_t &month,
                            int32_t &day,
                            int32_t &hour,
                            int32_t &minute,
                            int32_t &second,
                            int32_t &microsecond);
};

} // namespace sql
} // namespace oceanbase
