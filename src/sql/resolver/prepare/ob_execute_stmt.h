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

#ifndef OCEANBASE_SQL_RESOLVER_PREPARE_EXECUTE_STMT_
#define OCEANBASE_SQL_RESOLVER_PREPARE_EXECUTE_STMT_

#include "lib/container/ob_array.h"
#include "sql/resolver/cmd/ob_cmd_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
class ObExecuteStmt : public ObCMDStmt
{
public:
  ObExecuteStmt() : ObCMDStmt(stmt::T_EXECUTE), prepare_id_(common::OB_INVALID_ID), prepare_type_(stmt::T_NONE), params_() {}
  virtual ~ObExecuteStmt() {}

  inline ObPsStmtId get_prepare_id() const { return prepare_id_; }
  inline void set_prepare_id(ObPsStmtId id) { prepare_id_ = id; }
  inline stmt::StmtType get_prepare_type() const { return prepare_type_; }
  inline void set_prepare_type(stmt::StmtType type) { prepare_type_ = type; }
  inline const common::ObIArray<const sql::ObRawExpr*> &get_params() const { return params_; }
  inline int set_params(common::ObIArray<const sql::ObRawExpr*> &params) { return append(params_, params); }
  inline int add_param(const sql::ObRawExpr* param) { return params_.push_back(param); }

  TO_STRING_KV(N_SQL_ID, prepare_id_, N_STMT_TYPE, prepare_type_, N_PARAM, params_);
private:
  ObPsStmtId prepare_id_;
  stmt::StmtType prepare_type_;
  common::ObArray<const sql::ObRawExpr*> params_;
  DISALLOW_COPY_AND_ASSIGN(ObExecuteStmt);
};

}//end of sql
}//end of oceanbase

#endif //OCEANBASE_SQL_RESOLVER_PREPARE_EXECUTE_STMT_
