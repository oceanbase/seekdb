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

#ifndef OCEANBASE_SQL_ENGINE_FILE_TABLE_OB_FILE_TABLE_UTILS_H_
#define OCEANBASE_SQL_ENGINE_FILE_TABLE_OB_FILE_TABLE_UTILS_H_

#include <stdio.h>

#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"
#include "common/object/ob_object.h"
#include "common/object/ob_obj_type.h"

namespace oceanbase
{
namespace sql
{

// Core helpers for the "file as a database" table functions
// (read_csv / read_json / read_ndjson / read_parquet / glob / COPY).
// Pure file-format logic lives here so it can be unit tested without the
// executor; the scan operator wraps these with streaming row iteration.

enum class ObFileTableFormat
{
  CSV = 0,
  NDJSON,
  PARQUET,
  MAX,
};

// Inferred column set, shared by CSV / JSONL schema detection.
struct ObFileColumnSchema
{
  void reset();
  int add_column(common::ObIAllocator &allocator, const common::ObString &name, common::ObObjType type);
  int64_t column_cnt() const { return names_.count(); }
  const common::ObString &column_name(int64_t idx) const { return names_.at(idx); }
  common::ObObjType column_type(int64_t idx) const { return types_.at(idx); }
  bool column_has_null(int64_t idx) const { return null_flags_.at(idx); }
  void mark_null(int64_t idx) { null_flags_.at(idx) = true; }
  // Widen the stored type when a new value does not fit (BIGINT -> DOUBLE ->
  // VARCHAR, per the deterministic inference rules).
  int widen_column(int64_t idx, common::ObObjType incoming);
  // DuckDB style duplicate names get a __2/__3 suffix at detection time.
  static int dedup_column_name(common::ObIAllocator &allocator,
                               const common::ObString &raw_name,
                               const common::ObIArray<common::ObString> &existing,
                               common::ObString &final_name);
public:
  common::ObArray<common::ObString> names_;
  common::ObArray<common::ObObjType> types_;
  common::ObArray<bool> null_flags_;
};

// One parsed CSV record. field_ptrs_ point into the row buffer owned by the
// reader; they are valid until the next read_row() call.
struct ObCsvRecord
{
  void reset();
  common::ObArray<const char *> field_ptrs_;
  common::ObArray<int64_t> field_lens_;
};

class ObCsvReader
{
public:
  ObCsvReader() : fd_(nullptr), buf_(nullptr), buf_len_(0), buf_pos_(0), buf_size_(0),
                  eof_(false), row_start_pos_(0) {}
  ~ObCsvReader() { close(); }
  // Read the whole file lazily in chunks; rows are returned as pointers into
  // an internal buffer so no full-file materialization happens.
  int open(const char *path, int64_t max_buffer_bytes);
  void close();
  bool is_opened() const { return nullptr != fd_; }
  // Parses one CSV row honoring quotes, escaped quotes, embedded newlines and
  // LF/CRLF line endings. Empty unquoted fields yield NULL (is_null).
  // Plain-text line reader for formats where quotes have no CSV meaning
  // (e.g. NDJSON): no quote processing, no field splitting.
  int read_line(common::ObIAllocator &allocator, char *&line_buf, int64_t &line_len, bool &has_more);
  int read_row(common::ObIAllocator &allocator, ObCsvRecord &record,
               common::ObArray<bool> &field_is_null, bool &has_more);
  int64_t line_no() const { return line_no_; }
private:
  int fill_buffer();
  int read_char_locked(char &c);  // internal single char over the chunk buffer
private:
  FILE *fd_;
  char *buf_;
  int64_t buf_len_;
  int64_t buf_pos_;
  int64_t buf_size_;
  bool eof_;
  int64_t line_no_;
  int64_t row_start_pos_;
};

// Deterministic type inference per the competition spec: BIGINT, DOUBLE,
// BOOLEAN, VARCHAR, DATE, DATETIME (NULL only for all-null columns).
class ObFileTypeInferrer
{
public:
  static common::ObObjType merge_type(common::ObObjType a, common::ObObjType b);
  // Classifies one raw (unquoted) field. Returns the narrowest type.
  static common::ObObjType classify(const char *data, int64_t len, bool is_null);
  static bool parse_boolean(const char *data, int64_t len, bool &value);
  static bool parse_date(const char *data, int64_t len);
  static bool parse_datetime(const char *data, int64_t len);
  static bool is_integer_text(const char *data, int64_t len);
};

// JSONL: field union over all objects, missing fields become NULL; nested
// objects/arrays are rejected with the line number for diagnostics.
class ObNdjsonScanner
{
public:
  static int detect_schema(common::ObIAllocator &allocator, const char *path,
                           ObFileColumnSchema &schema, int64_t &row_cnt);
};

// Execution-time scanner. The resolver keeps only schema and options; rows are
// produced from the file on demand and every rescan starts from the beginning.
class ObFileScanReader
{
public:
  ObFileScanReader();
  ~ObFileScanReader();
  int open(common::ObIAllocator &allocator, const char *path,
           ObFileTableFormat format, bool header, const ObFileColumnSchema &schema,
           const common::ObIArray<int64_t> *projected_columns = NULL);
  int next_row(common::ObIAllocator &allocator,
               common::ObIArray<common::ObObjParam> &row, bool &has_row);
  void close();
private:
  struct Impl;
  Impl *impl_;
};

// glob() support: shell-style * ? matching over one directory level set.
class ObFileGlobUtil
{
public:
  static int expand(common::ObIAllocator &allocator, const common::ObString &pattern,
                    common::ObArray<common::ObString> &files);
  static bool pattern_match(const char *pattern, int64_t plen, const char *str, int64_t slen);
};

// COPY ... TO CSV writer with header/quote/escape/nullstr options.
class ObCsvWriter
{
public:
  ObCsvWriter() : fd_(nullptr) {}
  ~ObCsvWriter() { close(); }
  int open(const char *path);
  void close();
  int write_header(const common::ObIArray<common::ObString> &names);
  int write_row(const common::ObIArray<common::ObString> &cells,
                const common::ObArray<bool> &is_null,
                const common::ObString &null_text);
private:
  int write_field(const char *data, int64_t len, bool need_quote);
  FILE *fd_;
};

// Footer-only metadata inspection and page-at-a-time Parquet execution.
class ObParquetStreamReader
{
public:
  ObParquetStreamReader();
  ~ObParquetStreamReader();
  int open(common::ObIAllocator &allocator, const char *path,
           ObFileColumnSchema &schema,
           const common::ObIArray<int64_t> *projected_columns = NULL);
  int next_row(common::ObIAllocator &allocator,
               common::ObIArray<common::ObObjParam> &row, bool &has_row);
  int64_t row_count() const;
  void close();
  struct Impl;
private:
  Impl *impl_;
  DISALLOW_COPY_AND_ASSIGN(ObParquetStreamReader);
};

class ObParquetReader
{
public:
  static int read_metadata(common::ObIAllocator &allocator, const char *path,
                           ObFileColumnSchema &schema, int64_t &row_count);
  static int read_file(common::ObIAllocator &allocator, const char *path,
                       ObFileColumnSchema &schema,
                       common::ObIArray<common::ObObjParam> &flat_cells,
                       int64_t &row_count);
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_FILE_TABLE_OB_FILE_TABLE_UTILS_H_ */
