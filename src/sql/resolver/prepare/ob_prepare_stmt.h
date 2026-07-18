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

#ifndef OCEANBASE_SRC_SQL_RESOLVER_PREPARE_OB_PREPARE_STMT_H_
#define OCEANBASE_SRC_SQL_RESOLVER_PREPARE_OB_PREPARE_STMT_H_

#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"
#include "sql/resolver/cmd/ob_cmd_stmt.h"


namespace oceanbase
{
namespace sql
{
class ObPrepareStmt : public ObCMDStmt
{
public:
  ObPrepareStmt() : ObCMDStmt(stmt::T_PREPARE), prepare_name_(), prepare_sql_(NULL) {}
  virtual ~ObPrepareStmt() {}

  inline void set_prepare_name(const common::ObString &name) { prepare_name_ = name; }
  inline const common::ObString &get_prepare_name() const { return prepare_name_; }
  inline void set_prepare_sql(ObRawExpr *stmt) { prepare_sql_ = stmt; }
  inline const ObRawExpr *get_prepare_sql() const { return prepare_sql_; }

  TO_STRING_KV(N_STMT_NAME, prepare_name_, N_PREPARE_SQL, prepare_sql_);
private:
  common::ObString prepare_name_;
  ObRawExpr *prepare_sql_;
  DISALLOW_COPY_AND_ASSIGN(ObPrepareStmt);
};

}//end of sql
}//end of oceanbase



#endif /* OCEANBASE_SRC_SQL_RESOLVER_PREPARE_OB_PREPARE_STMT_H_ */
