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

#ifndef OCEANBASE_SRC_SQL_OB_INSERT_STMT_PRINTER_H_
#define OCEANBASE_SRC_SQL_OB_INSERT_STMT_PRINTER_H_

#include "ob_dml_stmt_printer.h"
#include "sql/resolver/dml/ob_insert_stmt.h"
#include "sql/printer/ob_raw_expr_printer.h"

namespace oceanbase
{
namespace sql
{
class ObInsertStmtPrinter : public ObDMLStmtPrinter {

public:
  ObInsertStmtPrinter()=delete;
  ObInsertStmtPrinter(char *buf, int64_t buf_len, int64_t *pos, const ObInsertStmt *stmt,
                      ObSchemaGetterGuard *schema_guard,
                      common::ObObjPrintParams print_params,
                      const ParamStore *param_store = NULL,
                      const ObSQLSessionInfo *session = NULL) :
    ObDMLStmtPrinter(buf, buf_len, pos, stmt, schema_guard, print_params, param_store, session) {}
  virtual ~ObInsertStmtPrinter() {}

  virtual int do_print();

private:
  int print();
  int print_basic_stmt();

  int print_insert();
  int print_into();
  int print_values();

  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObInsertStmtPrinter);
private:
  // data members

};

}
}


#endif /* OCEANBASE_SRC_SQL_OB_INSERT_STMT_PRINTER_H_ */
