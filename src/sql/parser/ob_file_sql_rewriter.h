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

#ifndef OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITER_H_
#define OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/ob_define.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace sql
{

// Rewrites `read_csv` / `read_json` / `read_parquet` / `glob` table references
// into the JSON table constructor that the engine already understands:
//
//   select id, name from read_csv('/tmp/a.csv') f order by id
//     ->
//   select id, name from json_table('[{...}]', '$[*]' columns(...)) f order by id
//
// The columns are inferred from the file, so the projection, filters,
// aggregates and joins all run on the normal engine path.
class ObFileSQLRewriter
{
public:
  // Returns rewritten == true and fills `out` when the statement references a
  // file table function.  Allocates the result from `alloc`.
  static int maybe_rewrite(ObIAllocator &alloc,
                           const common::ObString &stmt,
                           common::ObString &out,
                           bool &rewritten);
};

}  // namespace sql
}  // namespace oceanbase

#endif  // OCEANBASE_SQL_PARSER_OB_FILE_SQL_REWRITER_H_
