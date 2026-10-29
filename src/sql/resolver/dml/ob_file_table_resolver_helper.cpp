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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/dml/ob_file_table_resolver_helper.h"
#include <string.h>

namespace oceanbase
{
namespace sql
{

namespace
{

// Copies one cell into the arena so the materialized row outlives the scanner.
int copy_cell(common::ObIAllocator &allocator,
              const common::ObString &src,
              common::ObString &dst)
{
  int ret = OB_SUCCESS;
  if (src.empty()) {
    dst.reset();
  } else {
    char *buf = static_cast<char *>(allocator.alloc(src.length()));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(buf, src.ptr(), src.length());
      dst.assign_ptr(buf, src.length());
    }
  }
  return ret;
}

// A file table is materialized into a VALUES table while the statement is
// resolved, so the whole result has to fit in the statement allocator. The cap
// turns a would-be out-of-memory kill into a diagnosable error.
static const int64_t FILE_TABLE_MAX_ROWS = 2000000;

int append_row(common::ObIAllocator &allocator,
               const common::ObIArray<common::ObString> &fields,
               const common::ObIArray<bool> &is_null,
               ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  const int64_t row_width = fields.count() > 0 ? fields.count() : 1;
  if (OB_UNLIKELY(rows.cells_.count() / row_width >= FILE_TABLE_MAX_ROWS)) {
    ret = OB_SIZE_OVERFLOW;
    LOG_USER_ERROR(OB_SIZE_OVERFLOW);
    LOG_WARN("file table exceeds the row limit", K(ret), K(rows.cells_.count()),
             K(fields.count()), K(FILE_TABLE_MAX_ROWS));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < fields.count(); ++i) {
    common::ObString cell;
    if (OB_FAIL(copy_cell(allocator, fields.at(i), cell))) {
    } else if (OB_FAIL(rows.cells_.push_back(cell))) {
    } else if (OB_FAIL(rows.is_null_.push_back(is_null.at(i)))) {
    }
  }
  return ret;
}

// Fills rows.columns_ / rows.column_types_ from an inferred schema.
int fill_columns(common::ObIAllocator &allocator,
                 const ObFileSchema &schema,
                 ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < schema.columns_.count(); ++i) {
    const ObFileColumn &src = schema.columns_.at(i);
    ObFileColumn col;
    col.type_ = src.type_;
    col.nullable_ = src.nullable_;
    col.ordinal_ = src.ordinal_;
    if (OB_FAIL(copy_cell(allocator, src.name_, col.name_))) {
    } else if (OB_FAIL(rows.columns_.push_back(col))) {
    } else if (OB_FAIL(rows.column_types_.push_back(ob_file_column_to_obj_type(src.type_)))) {
    }
  }
  return ret;
}

// glob() exposes a single `file` column with one row per matched path.
int load_glob_rows(common::ObIAllocator &allocator,
                   const common::ObString &pattern,
                   const common::ObString &secure_file_priv,
                   ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  common::ObSEArray<common::ObString, 16> paths;
  if (OB_FAIL(ob_file_sql_resolve_paths(allocator, pattern, secure_file_priv, paths))) {
  } else {
    ObFileColumn name_col;
    name_col.name_ = common::ObString::make_string("file");
    name_col.type_ = OB_FILE_TYPE_VARCHAR;
    name_col.nullable_ = false;
    name_col.ordinal_ = 0;
    if (OB_FAIL(rows.columns_.push_back(name_col))) {
    } else if (OB_FAIL(rows.column_types_.push_back(common::ObVarcharType))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < paths.count(); ++i) {
    const char *path = paths.at(i).ptr();
    const char *base = strrchr(path, '/');
    const char *file_name = base == nullptr ? path : base + 1;
    common::ObString cell;
    if (OB_FAIL(copy_cell(allocator, common::ObString::make_string(file_name), cell))) {
    } else if (OB_FAIL(rows.cells_.push_back(cell))) {
    } else if (OB_FAIL(rows.is_null_.push_back(false))) {
    }
  }
  return ret;
}

int scan_csv(common::ObIAllocator &allocator,
             const common::ObString &path,
             const ObFileReadOptions &options,
             const ObFileSchema &schema,
             ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  ObFileCsvScanner scanner;
  common::ObSEArray<common::ObString, 24> fields;
  common::ObSEArray<bool, 24> is_null;
  if (OB_FAIL(scanner.init(allocator, path, options, schema))) {
  }
  while (OB_SUCC(ret)) {
    fields.reuse();
    is_null.reuse();
    if (OB_FAIL(scanner.next_row(fields, is_null))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      }
      LOG_WARN("failed to read csv row", K(ret));
    } else if (OB_FAIL(append_row(allocator, fields, is_null, rows))) {
    }
  }
  scanner.release();
  return ret;
}

int scan_json(common::ObIAllocator &allocator,
              const common::ObString &path,
              const ObFileSchema &schema,
              ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  ObFileJsonScanner scanner;
  common::ObSEArray<common::ObString, 24> fields;
  common::ObSEArray<bool, 24> is_null;
  if (OB_FAIL(scanner.init(allocator, path, schema))) {
  }
  while (OB_SUCC(ret)) {
    fields.reuse();
    is_null.reuse();
    if (OB_FAIL(scanner.next_row(fields, is_null))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      }
      LOG_WARN("failed to read json row", K(ret));
    } else if (OB_FAIL(append_row(allocator, fields, is_null, rows))) {
    }
  }
  scanner.release();
  return ret;
}

int scan_parquet(common::ObIAllocator &allocator,
                 const common::ObString &path,
                 const ObFileSchema &schema,
                 ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  ObFileParquetScanner scanner;
  common::ObSEArray<common::ObString, 24> fields;
  common::ObSEArray<bool, 24> is_null;
  if (OB_FAIL(scanner.init(allocator, path, schema))) {
  }
  while (OB_SUCC(ret)) {
    fields.reuse();
    is_null.reuse();
    if (OB_FAIL(scanner.next_row(fields, is_null))) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      }
      LOG_WARN("failed to read parquet row", K(ret));
    } else if (OB_FAIL(append_row(allocator, fields, is_null, rows))) {
    }
  }
  scanner.release();
  return ret;
}

int load_file_rows(common::ObIAllocator &allocator,
                   const ObFileReadOptions &options,
                   const common::ObString &path,
                   const common::ObString &secure_file_priv,
                   const ObFileFormat format,
                   ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  ObFileSchema schema;
  common::ObSEArray<common::ObString, 4> paths;
  if (OB_FAIL(ob_file_sql_resolve_paths(allocator, path, secure_file_priv, paths))) {
  } else if (OB_UNLIKELY(1 != paths.count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("expected exactly one file", K(ret), K(paths.count()));
  } else if (OB_FILE_FORMAT_CSV == format) {
    if (OB_FAIL(ob_file_sql_infer_csv(allocator, paths.at(0), options, schema))) {
    } else if (OB_FAIL(fill_columns(allocator, schema, rows))) {
    } else if (OB_FAIL(scan_csv(allocator, paths.at(0), options, schema, rows))) {
    }
  } else if (OB_FILE_FORMAT_JSON == format) {
    if (OB_FAIL(ob_file_sql_infer_json(allocator, paths.at(0), schema))) {
    } else if (OB_FAIL(fill_columns(allocator, schema, rows))) {
    } else if (OB_FAIL(scan_json(allocator, paths.at(0), schema, rows))) {
    }
  } else if (OB_FILE_FORMAT_PARQUET == format) {
    if (OB_FAIL(ob_file_sql_infer_parquet(allocator, paths.at(0), schema))) {
    } else if (OB_FAIL(fill_columns(allocator, schema, rows))) {
    } else if (OB_FAIL(scan_parquet(allocator, paths.at(0), schema, rows))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected file format", K(ret), K(format));
  }
  return ret;
}

} // anonymous namespace

int ob_file_table_load_rows(common::ObIAllocator &allocator,
                            const ObFileReadOptions &options,
                            const common::ObString &path,
                            const common::ObString &secure_file_priv,
                            const bool enable_file_sql,
                            const bool is_glob,
                            const ObFileFormat format,
                            ObFileTableRows &rows)
{
  int ret = OB_SUCCESS;
  rows.columns_.reuse();
  rows.column_types_.reuse();
  rows.cells_.reuse();
  rows.is_null_.reuse();
  if (!enable_file_sql) {
    // Checked before touching the path so the switch hides even the existence of
    // the file.
    ret = OB_NOT_SUPPORTED;
    LOG_USER_ERROR(OB_NOT_SUPPORTED,
                   "file table function is disabled, set enable_file_sql=ON first");
  } else if (path.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "file table function requires a file path");
  } else if (is_glob) {
    if (OB_FAIL(load_glob_rows(allocator, path, secure_file_priv, rows))) {
    }
  } else if (OB_FAIL(load_file_rows(allocator, options, path, secure_file_priv, format, rows))) {
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
