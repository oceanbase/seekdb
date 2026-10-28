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

#ifndef OCEANBASE_SQL_OB_JSON_PARSE
#define OCEANBASE_SQL_OB_JSON_PARSE

#include "ob_json_base.h"

namespace oceanbase {
namespace common {

#define HAS_FLAG(flags, specific) (((flags) & (specific)) == (specific))
#define ADD_FLAG_IF_NEED(expr, flags, specific) \
  if (expr) {                                   \
    (flags) |= (specific);                      \
  }

const int JSON_DOCUMENT_MAX_DEPTH = 100;

// JSON text is always standard RFC 8259 syntax. These flags control only
// independent object-key and JSON Schema policies.
class ObJsonParser final
{
public:
  static const uint32_t JSN_DEFAULT_FLAG = 0;
  static const uint32_t JSN_UNIQUE_FLAG = 4;
  static const uint32_t JSN_SCHEMA_FLAG = 8;
  static const uint32_t JSN_PRESERVE_DUP_FLAG = 16;

  static int get_binary(ObIAllocator *allocator, const char *text, uint64_t length,
                        ObString &binary, uint32_t parse_flag = 0,
                        uint32_t max_depth_config = JSON_DOCUMENT_MAX_DEPTH);

  static int check_json_syntax(const ObString &j_doc, ObIAllocator *allocator = NULL,
                               uint32_t parse_flag = 0,
                               uint32_t max_depth_config = JSON_DOCUMENT_MAX_DEPTH);
private:
  DISALLOW_COPY_AND_ASSIGN(ObJsonParser);
};

} // namespace common
} // namespace oceanbase

#endif  // OCEANBASE_SQL_OB_JSON_PARSE
