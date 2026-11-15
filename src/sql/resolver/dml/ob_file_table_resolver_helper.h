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

#include "sql/resolver/dml/ob_dml_stmt.h"
#include "sql/engine/basic/ob_file_scan_utils.h"

namespace oceanbase
{
namespace sql
{

// Materialized contents of one file table function call. The resolver reads the
// file while resolving and hands the rows to the ordinary VALUES table code
// path, so planning and execution need no file-specific operator: aggregation,
// sorting, projection and joins all work on it the same way they work on VALUES.
struct ObFileTableRows
{
  ObFileTableRows() : columns_(), column_types_(), cells_(), is_null_() {}
  // Inferred column metadata, in file order.
  common::ObSEArray<ObFileColumn, 16> columns_;
  // Obj type each column is materialized as, in the same order.
  common::ObSEArray<common::ObObjType, 16> column_types_;
  // Row-major cell text; a NULL cell is flagged in is_null_.
  common::ObSEArray<common::ObString, 64> cells_;
  common::ObSEArray<bool, 64> is_null_;
  int64_t column_count() const { return static_cast<int64_t>(columns_.count()); }
  int64_t row_count() const {
    return column_count() == 0 ? 0
        : static_cast<int64_t>(cells_.count()) / column_count();
  }
  TO_STRING_KV(K_(columns), K_(cells));
};

// Reads a whole file into memory as text cells, after checking the feature
// switch and path containment. When `is_glob` is set, `path` is treated as a
// glob pattern and the result holds a single `file` column naming the matches.
int ob_file_table_load_rows(common::ObIAllocator &allocator,
                            const ObFileReadOptions &options,
                            const common::ObString &path,
                            const common::ObString &secure_file_priv,
                            const bool enable_file_sql,
                            const bool is_glob,
                            const ObFileFormat format,
                            ObFileTableRows &rows);

} // namespace sql
} // namespace oceanbase
