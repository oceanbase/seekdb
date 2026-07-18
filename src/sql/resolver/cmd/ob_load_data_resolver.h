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

#ifndef OCEANBASE_SQL_RESOLVER_CMD_OB_LOAD_DATA_RESOLVER_H
#define OCEANBASE_SQL_RESOLVER_CMD_OB_LOAD_DATA_RESOLVER_H
#include "lib/ob_define.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/resolver/cmd/ob_cmd_resolver.h"
namespace oceanbase
{
namespace sql
{
class ObLoadDataStmt;
struct ObDataInFileStruct;

class ObLoadDataResolver : public ObCMDResolver
{
public:
  explicit ObLoadDataResolver(ObResolverParams &params):
        ObCMDResolver(params), current_scope_(T_LOAD_DATA_SCOPE)
  {
  }
  virtual ~ObLoadDataResolver()
  {
  }
  virtual int resolve(const ParseNode &parse_tree);
  int resolve_field_node(const ParseNode &node, const common::ObNameCaseMode case_mode,
                         ObLoadDataStmt &load_stmt);
  int resolve_user_vars_node(const ParseNode &node, ObLoadDataStmt &load_stmt);
  int resolve_field_or_var_list_node(const ParseNode &node, const common::ObNameCaseMode case_mode,
                                     ObLoadDataStmt &load_stmt);
  int resolve_empty_field_or_var_list_node(ObLoadDataStmt &load_stmt);
  int resolve_set_clause(const ParseNode &node, const common::ObNameCaseMode case_mode,
                                  ObLoadDataStmt &load_stmt);
  int resolve_each_set_node(const ParseNode &node, const common::ObNameCaseMode case_mode,
                             ObLoadDataStmt &load_stmt);
  int resolve_string_node(const ParseNode &node, common::ObString &target_str);
  int resolve_field_list_node(const ParseNode &node, ObDataInFileStruct &data_struct_in_file);
  int resolve_line_list_node(const ParseNode &node, ObDataInFileStruct &data_struct_in_file);
  int build_column_ref_expr(ObQualifiedName &q_name, ObRawExpr *&col_expr);
  int validate_stmt(ObLoadDataStmt* stmt);
  int resolve_hints(const ParseNode &node);

  int resolve_filename(ObLoadDataStmt *load_stmt, ParseNode *node);
  int local_infile_enabled(bool &enabled) const;
  int resolve_partitions(const ParseNode &node, ObLoadDataStmt &load_stmt);

  int check_trigger_constraint(const ObTableSchema *table_schema);
private:
  int pattern_match(const ObString& str, const ObString& pattern, bool &matched);
  bool exist_wildcard(const ObString& str);
private:
  enum ParameterEnum {
    ENUM_OPT_LOCAL = 0,
    ENUM_FILE_NAME,
    ENUM_DUPLICATE_ACTION,
    ENUM_TABLE_NAME,
    ENUM_OPT_CHARSET,
    ENUM_OPT_FIELD,
    ENUM_OPT_LINE,
    ENUM_OPT_IGNORE_ROWS,
    ENUM_OPT_FIELD_OR_VAR,
    ENUM_OPT_SET_FIELD,
    ENUM_OPT_HINT,
    ENUM_OPT_EXTENDED_OPTIONS,
    ENUM_OPT_COMPRESSION,
    ENUM_OPT_USE_PARTITION,
    ENUM_OPT_ON_ERROR,
    ENUM_TOTAL_COUNT
  };
  ObStmtScope current_scope_;
  DISALLOW_COPY_AND_ASSIGN(ObLoadDataResolver);
};
}//sql
}//oceanbase
#endif
