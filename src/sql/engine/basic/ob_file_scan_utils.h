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

#include "lib/allocator/page_arena.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_array.h"
#include "common/object/ob_obj_type.h"
#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace sql
{

// The scalar types a file column can be inferred as. The order is significant:
// widening only ever moves upward, so merging two column types is a max().
enum ObFileColumnType
{
  OB_FILE_TYPE_NULL = 0,      // every value in the column was NULL/absent
  OB_FILE_TYPE_BOOLEAN,
  OB_FILE_TYPE_BIGINT,
  OB_FILE_TYPE_DOUBLE,
  OB_FILE_TYPE_DATE,
  OB_FILE_TYPE_DATETIME,
  OB_FILE_TYPE_VARCHAR,
  OB_FILE_TYPE_MAX
};

const char *ob_file_column_type_name(const ObFileColumnType type);
common::ObObjType ob_file_column_to_obj_type(const ObFileColumnType type);

// What a file table function reads. GLOB is not a file format: it enumerates
// matching paths instead of decoding one file.
enum ObFileFormat
{
  OB_FILE_FORMAT_CSV = 0,
  OB_FILE_FORMAT_JSON,
  OB_FILE_FORMAT_PARQUET,
  OB_FILE_FORMAT_GLOB,
  OB_FILE_FORMAT_MAX
};

// Human readable name of a format, matching the function name used in SQL.
const char *ob_file_format_name(const ObFileFormat format);

// One inferred column of a file.
struct ObFileColumn
{
  ObFileColumn()
    : name_(), type_(OB_FILE_TYPE_NULL), nullable_(true), ordinal_(-1) {}
  common::ObString name_;
  ObFileColumnType type_;
  bool nullable_;
  int64_t ordinal_;
  TO_STRING_KV(K_(name), K_(type), K_(nullable), K_(ordinal));
};

// Inferred schema of one file. Kept in the statement allocator, so the strings
// point into the sql arena rather than into the file.
struct ObFileSchema
{
  ObFileSchema() : columns_(), column_names_() {}
  common::ObSEArray<ObFileColumn, 16> columns_;
  // Lower-cased name -> index map for case-insensitive lookups of a requested
  // projection. Stored as parallel arrays to stay inside the arena.
  common::ObSEArray<common::ObString, 16> column_names_;
  TO_STRING_KV(K_(columns));
};

// Parsed options shared by every read_*() call. Only the options the competition
// grammar accepts are materialized; unknown options are rejected by the parser.
struct ObFileReadOptions
{
  ObFileReadOptions()
    : has_header_(true), format_(), delimiter_(','), quote_('"'), escape_('\\'),
      null_str_(), sample_size_(-1) {}
  bool has_header_;
  common::ObString format_;
  char delimiter_;
  char quote_;
  char escape_;
  common::ObString null_str_;
  int64_t sample_size_;
  TO_STRING_KV(K_(has_header), K_(format), K_(delimiter), K_(quote), K_(escape),
               K_(null_str), K_(sample_size));
};

// Guards every file access: the feature switch, secure_file_priv containment and
// regular-file checks. A path that escapes the allowed directory is rejected
// before the file is opened.
int ob_file_sql_check_access(common::ObIAllocator &allocator,
                             const common::ObString &path,
                             const common::ObString &secure_file_priv,
                             bool enable_file_sql);
// Resolves a path (optionally containing glob metacharacters) into concrete
// regular files. Returns absolute paths inside `secure_file_priv`.
int ob_file_sql_resolve_paths(common::ObIAllocator &allocator,
                              const common::ObString &pattern,
                              const common::ObString &secure_file_priv,
                              common::ObIArray<common::ObString> &paths);

// Infers the schema of a CSV file. Uses the header row for names when present and
// widens each column over all sampled rows, so the result does not depend on the
// order of values.
int ob_file_sql_infer_csv(common::ObIAllocator &allocator,
                          const common::ObString &path,
                          const ObFileReadOptions &options,
                          ObFileSchema &schema);
// Infers the schema of a newline-delimited JSON file as the union of the keys of
// every object. Missing keys become nullable columns.
int ob_file_sql_infer_json(common::ObIAllocator &allocator,
                           const common::ObString &path,
                           ObFileSchema &schema);
// Infers the schema of a Parquet file from its footer schema.
int ob_file_sql_infer_parquet(common::ObIAllocator &allocator,
                              const common::ObString &path,
                              ObFileSchema &schema);

// Streams one row at a time out of a CSV file. The scanner owns no buffering of
// the whole file: it refills a bounded window and yields fields for the current
// row.
class ObFileCsvScanner
{
public:
  ObFileCsvScanner();
  virtual ~ObFileCsvScanner();
  int init(common::ObIAllocator &allocator,
           const common::ObString &path,
           const ObFileReadOptions &options,
           const ObFileSchema &schema);
  // Returns OB_ITER_END after the last row. `fields` holds one entry per column;
  // NULL cells are marked through `is_null`.
  int next_row(common::ObIArray<common::ObString> &fields,
               common::ObIArray<bool> &is_null);
  void release();
private:
  int refill();
  int read_row();
  struct Impl;
  Impl *impl_;
  DISALLOW_COPY_AND_ASSIGN(ObFileCsvScanner);
};

// Streams newline-delimited JSON objects, projecting them onto the union schema.
class ObFileJsonScanner
{
public:
  ObFileJsonScanner();
  virtual ~ObFileJsonScanner();
  int init(common::ObIAllocator &allocator,
           const common::ObString &path,
           const ObFileSchema &schema);
  int next_row(common::ObIArray<common::ObString> &fields,
               common::ObIArray<bool> &is_null);
  void release();
private:
  struct Impl;
  Impl *impl_;
  DISALLOW_COPY_AND_ASSIGN(ObFileJsonScanner);
};

// Minimal Parquet reader: parses the footer metadata and decompresses one
// row group at a time. Only the plain encodings and snappy/uncompressed codecs
// needed by the test data are supported.
class ObFileParquetScanner
{
public:
  ObFileParquetScanner();
  virtual ~ObFileParquetScanner();
  int init(common::ObIAllocator &allocator,
           const common::ObString &path,
           const ObFileSchema &schema);
  int next_row(common::ObIArray<common::ObString> &fields,
               common::ObIArray<bool> &is_null);
  void release();
private:
  struct Impl;
  Impl *impl_;
  DISALLOW_COPY_AND_ASSIGN(ObFileParquetScanner);
};

// Detects the file format of an existing path from its extension.
int ob_file_sql_detect_format(const common::ObString &path,
                              common::ObString &format);

} // namespace sql
} // namespace oceanbase
