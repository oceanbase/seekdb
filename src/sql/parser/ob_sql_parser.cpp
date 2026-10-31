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

#define USING_LOG_PREFIX SQL

#include "ob_sql_parser.h"
#include "sql/parser/parse_node.h"
#include "share/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/oblog/ob_log.h"
#include "sql/parser/parse_malloc.h"
#include "sql/parser/ob_file_sql_rewrite.h"
#include <cstring>
#include <string>


namespace oceanbase
{
using namespace common;
namespace sql
{
int ObSQLParser::parse(const char * str_ptr, const int64_t str_len, ParseResult &result)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(parse_init(&result))) {
    // do nothing
  } else if (OB_UNLIKELY(str_len < 0 || OB_ISNULL(str_ptr))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    std::string rewritten;
    std::string error;
    const std::string original(str_ptr, static_cast<size_t>(str_len));
    if (!native_file_sql::rewrite(original, rewritten, error)) {
      // Do not fall through to an ordinary table lookup when an unsafe file
      // request or unsupported file format has been explicitly requested.
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("file SQL rewrite rejected", "detail", error.c_str());
    } else {
      const char *sql_ptr = str_ptr;
      size_t sql_len = static_cast<size_t>(str_len);
      if (rewritten != original) {
        // Parse nodes may refer to source slices after parse_sql() returns;
        // keep the rewritten source in the parse result's arena.
        char *p = static_cast<char *>(parse_malloc(
            rewritten.size() + 1, result.malloc_pool_));
        if (OB_ISNULL(p)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          std::memcpy(p, rewritten.data(), rewritten.size());
          p[rewritten.size()] = '\0';
          sql_ptr = p;
          sql_len = rewritten.size();
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(parse_sql(&result, sql_ptr, sql_len))) {
        // Keep existing parser-error behavior.
      }
    }
  }
  return ret;
}

}
}
