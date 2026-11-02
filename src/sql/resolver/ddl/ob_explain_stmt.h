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

#ifndef OCEANBASE_SQL_RESOLVER_DML_EXPLAIN_STMT_
#define OCEANBASE_SQL_RESOLVER_DML_EXPLAIN_STMT_

#include "sql/resolver/dml/ob_dml_stmt.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{

struct ObExplainDisplayOpt
{
  ObExplainDisplayOpt() 
    : with_tree_line_(false), 
    with_color_(false) ,
    with_real_info_(false)
    {}

  bool with_tree_line_;
  bool with_color_;
  bool with_real_info_;
};

class ObExplainStmt : public ObDMLStmt
{
public:
  ObExplainStmt();
  virtual ~ObExplainStmt();
  void set_explain_format(ExplainType format) { format_ = format; }
  ExplainType get_explain_type() const { return format_; }
  void set_display_opt(const ObExplainDisplayOpt &opt) { display_opt_ = opt; }
  const ObExplainDisplayOpt &get_display_opt() const { return display_opt_; }
  ObDMLStmt* get_explain_query_stmt() const { return explain_query_stmt_; }
  void set_explain_query_stmt(ObDMLStmt *stmt) { explain_query_stmt_ = stmt; }
  bool is_select_explain() const;
  const common::ObString& get_into_table() const { return into_table_; }
  void set_into_table(const common::ObString& into_table) { into_table_ = into_table; }
  const common::ObString& get_statement_id() const { return statement_id_; }
  void set_statement_id(const common::ObString& statement_id) { statement_id_ = statement_id; }
  virtual bool is_affect_found_rows() const { return is_select_explain(); }
  bool is_explain_extended() const { return EXPLAIN_EXTENDED == format_
                                            || EXPLAIN_EXTENDED_NOADDR == format_; }

  DECLARE_VIRTUAL_TO_STRING;
private:
  //bool  verbose_;
  ExplainType format_;
  ObExplainDisplayOpt display_opt_;
  ObDMLStmt *explain_query_stmt_;
  common::ObString into_table_;
  common::ObString statement_id_;
  DISALLOW_COPY_AND_ASSIGN(ObExplainStmt);
};

}
}

#endif //OCEANBASE_SQL_RESOLVER_DML_EXPLAIN_STMT_
