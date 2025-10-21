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

#ifndef OCEANBASE_SQL_RESOVLER_PREPARE_DEALLOCATE_STMT_H_
#define OCEANBASE_SQL_RESOVLER_PREPARE_DEALLOCATE_STMT_H_

#include "sql/resolver/cmd/ob_cmd_stmt.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
class ObDeallocateStmt : public ObCMDStmt
{
public:
  ObDeallocateStmt() : ObCMDStmt(stmt::T_DEALLOCATE), prepare_name_(), prepare_id_(OB_INVALID_ID) {}
  virtual ~ObDeallocateStmt() {}

  inline void set_prepare_name(const common::ObString &name) { prepare_name_ = name; }
  const common::ObString &get_prepare_name() const { return prepare_name_; }
  inline void set_prepare_id(ObPsStmtId id) { prepare_id_ = id; }
  inline ObPsStmtId get_prepare_id() const { return prepare_id_; }

  TO_STRING_KV(N_STMT_NAME, prepare_name_, N_SQL_ID, prepare_id_);

private:
  common::ObString  prepare_name_;
  ObPsStmtId prepare_id_;
  DISALLOW_COPY_AND_ASSIGN(ObDeallocateStmt);
};

}//namespace sql
}//namespace oceanbase

#endif //OCEANBASE_SQL_RESOLVER_PREPARE_DEALLOCATE_STMT_H_
