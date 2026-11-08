/**
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

#include "lib/string/ob_string.h"
#include "common/object/ob_object.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/ob_allocator.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;

namespace file_sql
{

enum FileSqlFormat
{
  FILE_SQL_CSV = 0,
  FILE_SQL_JSON,
  FILE_SQL_NDJSON,
  FILE_SQL_PARQUET,
  FILE_SQL_GLOB,
};

enum FileSqlType
{
  FILE_SQL_BIGINT = 0,
  FILE_SQL_DOUBLE,
  FILE_SQL_BOOLEAN,
  FILE_SQL_VARCHAR,
  FILE_SQL_DATE,
  FILE_SQL_DATETIME,
  FILE_SQL_NULL,
};

struct FileSqlColumn
{
  common::ObString name_;
  FileSqlType type_;
  int64_t length_; // byte length for varchar columns
  bool nullable_;
  FileSqlColumn()
    : type_(FILE_SQL_VARCHAR), length_(0), nullable_(true) {}
};

struct FileSqlTable
{
  common::ObSEArray<FileSqlColumn, 8> columns_;
  // Row-major values, row_cnt_ * columns_.count() objects.
  common::ObSEArray<common::ObObjParam, 64> values_;
  int64_t row_cnt_;
  int64_t column_cnt_;
  FileSqlTable() : row_cnt_(0), column_cnt_(0) {}
};

struct FileSqlOptions
{
  bool header_;
  char delimiter_;
  char quote_;
  char escape_;
  common::ObString null_str_;
  int64_t sample_size_;
  int32_t json_format_; // 0: auto, 1: array, 2: newline delimited
  FileSqlOptions()
    : header_(true), delimiter_(','), quote_('"'), escape_('"'),
      null_str_(), sample_size_(-1), json_format_(0) {}
};

// Reads a local file into an in-memory table. The values are stored with their
// inferred types, so the caller only needs to create a values table item.
int read_file_table(const FileSqlFormat format,
                    const common::ObString &path,
                    const FileSqlOptions &options,
                    const ObSQLSessionInfo *session_info,
                    common::ObIAllocator &allocator,
                    FileSqlTable &table);

// Expands a glob pattern into a single "file" column.
int glob_files(const common::ObString &pattern,
               common::ObIAllocator &allocator,
               FileSqlTable &table);

// Returns the name of an inferred type, used for diagnostics.
const char *get_type_name(const FileSqlType type);

} // namespace file_sql
} // namespace sql
} // namespace oceanbase
