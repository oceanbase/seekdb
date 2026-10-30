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

#include "lib/ob_define.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "common/object/ob_object.h"

namespace oceanbase
{
namespace sql
{

enum ObFileSqlFuncKind
{
  FILE_SQL_FUNC_INVALID = 0,
  FILE_SQL_FUNC_READ_CSV = 1,
  FILE_SQL_FUNC_READ_JSON = 2,
  FILE_SQL_FUNC_READ_PARQUET = 3,
  FILE_SQL_FUNC_GLOB = 4,
};

// column kinds inferred from the file content
enum ObFileSqlColumnKind
{
  FILE_SQL_COL_UNKNOWN = -1,
  FILE_SQL_COL_NULL = 0,
  FILE_SQL_COL_VARCHAR,
  FILE_SQL_COL_BIGINT,
  FILE_SQL_COL_DOUBLE,
  FILE_SQL_COL_BOOLEAN,
  FILE_SQL_COL_DATE,
  FILE_SQL_COL_DATETIME,
};

// options parsed from the table function argument list, e.g.
//   read_csv('/path/a.csv', header = true, sample_size = -1)
struct ObFileSqlOptions
{
  ObFileSqlOptions()
    : path_(), header_(true), sample_size_(-1), format_(), null_str_()
  {}
  common::ObString path_;
  bool header_;
  int64_t sample_size_;
  common::ObString format_;
  common::ObString null_str_;
};

struct ObFileSqlColumnMeta
{
  ObFileSqlColumnMeta()
    : name_(), type_(common::ObNullType), kind_(FILE_SQL_COL_NULL),
      cs_type_(common::CS_TYPE_UTF8MB4_GENERAL_CI), nullable_(true)
  {}
  common::ObString name_;
  common::ObObjType type_;
  ObFileSqlColumnKind kind_;
  common::ObCollationType cs_type_;
  bool nullable_;
};

// The materialized content of one local file (or one glob pattern).
// cells are row-major: the cell of row i, column j is cells_.at(i * col_cnt + j)
class ObFileSqlTableData
{
public:
  explicit ObFileSqlTableData(common::ObIAllocator &alloc)
    : row_cnt_(0), alloc_(alloc)
  {}
  common::ObSEArray<ObFileSqlColumnMeta, 16> cols_;
  common::ObSEArray<common::ObObj, 64> cells_;
  int64_t row_cnt_;
  common::ObIAllocator &alloc_;
};

class ObFileSqlReader
{
public:
  static int read_table(const ObFileSqlFuncKind kind,
                        const ObFileSqlOptions &opts,
                        ObFileSqlTableData &table);
  // helper used by the resolver to validate the "format" option
  static bool is_valid_csv_format(const common::ObString &format);
  static bool is_valid_json_format(const common::ObString &format);
private:
  static int read_csv(const ObFileSqlOptions &opts, ObFileSqlTableData &table);
  static int read_json(const ObFileSqlOptions &opts, ObFileSqlTableData &table);
  static int read_parquet(const ObFileSqlOptions &opts, ObFileSqlTableData &table);
  static int read_glob(const ObFileSqlOptions &opts, ObFileSqlTableData &table);
};

} // namespace sql
} // namespace oceanbase
