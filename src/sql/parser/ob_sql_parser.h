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

#ifndef OCEANBASE_SRC_SQL_PARSER_OB_SQL_PARSER_H_
#define OCEANBASE_SRC_SQL_PARSER_OB_SQL_PARSER_H_

#include "sql/parser/parse_node.h"
namespace oceanbase
{
namespace common
{
class ObIAllocator;
}  // namespace common

namespace sql
{
class ObSQLParser
{
public:
  ObSQLParser(common::ObIAllocator &allocator, ObSQLMode mode)
    : allocator_(allocator),
      sql_mode_(mode)
  {}

  int parse(const char *str_ptr, const int64_t str_len, ParseResult &result);

private:
  common::ObIAllocator &allocator_ __attribute__((unused));
  ObSQLMode sql_mode_ __attribute__((unused));
};
}  // namespace pl
}  // namespace oceanbase



#endif /* OCEANBASE_SRC_SQL_PARSER_OB_SQL_PARSER_H_ */
