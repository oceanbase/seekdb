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

#ifndef OB_MOCK_STMT_H_
#define OB_MOCK_STMT_H_

#include "sql/resolver/cmd/ob_cmd_stmt.h"

namespace oceanbase
{
namespace sql
{
class ObMockStmt : public ObCMDStmt
{
public:
  ObMockStmt() : ObCMDStmt(stmt::T_NONE)
  {
  }
  virtual ~ObMockStmt() {}

  int add_stmt(stmt::StmtType stmt_type) {
    return stmt_types_.push_back(stmt_type);
  }

  const ObIArray<stmt::StmtType> &get_stmt_type_list() const {
    return stmt_types_;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObMockStmt);
  ObSEArray<stmt::StmtType, 16> stmt_types_;
};
}
}

#endif
