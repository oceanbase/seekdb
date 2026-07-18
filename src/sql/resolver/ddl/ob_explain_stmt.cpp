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

#include "sql/resolver/ddl/ob_explain_stmt.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExplainStmt::ObExplainStmt() : ObDMLStmt(stmt::T_EXPLAIN),
                                 format_(EXPLAIN_UNINITIALIZED),
                                 explain_query_stmt_(NULL)
{
}

ObExplainStmt::~ObExplainStmt()
{
}

bool ObExplainStmt::is_select_explain() const
{
  bool bool_ret = false;
  if (NULL != explain_query_stmt_) {
    bool_ret = explain_query_stmt_->is_select_stmt();
  }
  return bool_ret;
}


int64_t ObExplainStmt::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  if (OB_ISNULL(explain_query_stmt_)) {
    databuff_printf(buf, buf_len, pos, "explain query stmt is null");
  } else {
    J_KV(K_(into_table), K_(statement_id), N_EXPLAIN_STMT, explain_query_stmt_);
  }
  J_OBJ_END();
  return pos;
}

}//end of sql
}//end of oceanbase

//void ObExplainStmt::print(FILE *fp, int32_t level, int32_t index)
//{
//  UNUSED(index);
//  print_indentation(fp, level);
//  fprintf(fp, "ObExplainStmt %d Begin\n", index);
//  //if (verbose_) {
//  //  print_indentation(fp, level + 1);
//  //  fprintf(fp, "VERBOSE\n");
//  //}
//  print_indentation(fp, level + 1);
//  fprintf(fp, "Explain Query ::= <%p>\n", explain_query_stmt_);
//  print_indentation(fp, level);
//  fprintf(fp, "ObExplainStmt %d End\n", index);
//}
