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

#ifndef OCEANBASE_SQL_OB_CSV_PARSER_H_
#define OCEANBASE_SQL_OB_CSV_PARSER_H_

#include "lib/string/ob_string.h"
#include "lib/container/ob_se_array.h"
#include "common/object/ob_obj_type.h"

namespace oceanbase
{
namespace sql
{

struct CSVOptions
{
  bool has_header;
  char delimiter;
  char quote_char;
  int64_t sample_size;  // -1 means no limit

  CSVOptions() : has_header(true), delimiter(','), quote_char('"'), sample_size(-1) {}
};

class ObCSVParser
{
public:
  ObCSVParser() {}
  virtual ~ObCSVParser() {}

  // Parse CSV content into rows and headers
  static int parse(const common::ObString& content,
                   const CSVOptions& opts,
                   common::ObIAllocator& allocator,
                   common::ObSEArray<common::ObString, 16>& rows,
                   common::ObSEArray<common::ObString, 16>& headers);

  // Infer column types from sample rows
  static int infer_schema(const common::ObSEArray<common::ObString, 16>& rows,
                          const CSVOptions& opts,
                          common::ObIAllocator& allocator,
                          common::ObSEArray<common::ObObjType, 16>& types);

  // Parse a single row into fields
  static int parse_row(const common::ObString& row,
                       const CSVOptions& opts,
                       common::ObIAllocator& allocator,
                       common::ObSEArray<common::ObString, 16>& fields);

private:
  // Helper functions
  static int skip_whitespace(const common::ObString& str, int64_t& pos);
  static int parse_field(const common::ObString& str,
                         const CSVOptions& opts,
                         int64_t& pos,
                         common::ObString& field);
  static common::ObObjType infer_type_from_value(const common::ObString& value);

  DISALLOW_COPY_AND_ASSIGN(ObCSVParser);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_OB_CSV_PARSER_H_
