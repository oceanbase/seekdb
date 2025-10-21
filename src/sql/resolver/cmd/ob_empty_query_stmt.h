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

#ifndef OCEANBASE_SQL_RESOLVER_EMPTY_QUERY_STMT_H_
#define OCEANBASE_SQL_RESOLVER_EMPTY_QUERY_STMT_H_

#include "sql/resolver/ob_stmt.h"
#include "sql/resolver/cmd/ob_cmd_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObEmptyQueryStmt : public ObCMDStmt
{
public:
  ObEmptyQueryStmt() : ObCMDStmt(stmt::T_EMPTY_QUERY)
  {
  }

  virtual ~ObEmptyQueryStmt()
  {
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ObEmptyQueryStmt);
};

} // sql
} // oceanbase
#endif /*OCEANBASE_SQL_RESOLVER_EMPTY_QUERY_STMT_H_*/
