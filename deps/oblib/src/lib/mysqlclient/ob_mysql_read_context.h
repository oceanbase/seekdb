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

#ifndef OCEANBASE_MYSQLCLIENT_OB_MYSQL_READ_CONTEXT_H_
#define OCEANBASE_MYSQLCLIENT_OB_MYSQL_READ_CONTEXT_H_

#include "lib/mysqlclient/ob_isql_result_handler.h"
#include "lib/mysqlclient/ob_mysql_statement.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObMySQLResult;

class ObMySQLReadContext : public ObISQLResultHandler
{
public:
  ObMySQLReadContext() : result_(NULL), stmt_() {}
  virtual ~ObMySQLReadContext() {}

  virtual ObMySQLResult *mysql_result() { return result_; }

public:
  ObMySQLResult *result_;
  ObMySQLStatement stmt_;
};

} // end namespace sqlclient
} // end namespace common
} // end namespace oceanbase

#endif // OCEANBASE_MYSQLCLIENT_OB_MYSQL_READ_CONTEXT_H_
