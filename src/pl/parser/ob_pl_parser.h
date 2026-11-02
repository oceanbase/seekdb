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

#ifndef OCEANBASE_SRC_PL_PARSER_OB_PL_PARSER_H_
#define OCEANBASE_SRC_PL_PARSER_OB_PL_PARSER_H_

#include "pl/parser/parse_stmt_node.h"
#include "share/ob_define.h"
#include "share/schema/ob_trigger_info.h"
#include "sql/parser/ob_parser_utils.h"
#include "lib/ob_date_unit_type.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObString;
}  // namespace common
namespace sql
{
class ObSQLSessionInfo;
}

namespace pl
{
class ObPLParser
{
public:
  static constexpr int64_t MAX_PRINT_LEN = 64;

  ObPLParser(common::ObIAllocator &allocator, sql::ObCharsets4Parser charsets4parser, ObSQLMode sql_mode = 0)
    : allocator_(allocator),
      charsets4parser_(charsets4parser),
      sql_mode_(sql_mode)
  {}
  int fast_parse(const ObString &stmt_block,
                 ParseResult &parse_result);
  int parse(const common::ObString &stmt_block,
            const common::ObString &orig_stmt_block,
            ParseResult &parse_result,
            bool is_inner_parse = false);
  int parse_routine_body(const common::ObString &routine_body,
                         ObStmtNodeTree *&routine_stmt,
                         bool is_for_trigger,
                         bool need_unwrap = true /* for wrapped procedure/function */);
  int parse_package(const common::ObString &source,
                    ObStmtNodeTree *&package_stmt,
                    const ObDataTypeCastParams &dtc_params,
                    share::schema::ObSchemaGetterGuard *schema_guard,
                    bool is_for_trigger,
                    const share::schema::ObTriggerInfo *trg_info = NULL,
                    bool need_unwrap = true /* for wrapped package */);
private:
  int parse_procedure(const common::ObString &stmt_block,
                      const common::ObString &orig_stmt_block,
                      ObStmtNodeTree *&multi_stmt,
                      ObQuestionMarkCtx &question_mark_ctx,
                      bool is_for_trigger,
                      bool is_dynamic,
                      bool is_inner_parse,
                      bool &is_include_old_new_in_trigger,
                      bool &contain_sensitive_data);
  int parse_stmt_block(ObParseCtx &parse_ctx,
                       ObStmtNodeTree *&multi_stmt);

private:
  common::ObIAllocator &allocator_;
  sql::ObCharsets4Parser charsets4parser_;
  ObSQLMode sql_mode_;
};
}  // namespace pl
}  // namespace oceanbase
#endif /* OCEANBASE_SRC_PL_PARSER_OB_PL_PARSER_H_ */
