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

#ifndef __OB_COMMON_SQLCLIENT_MYSQL_PREPARED_PARAM__
#define __OB_COMMON_SQLCLIENT_MYSQL_PREPARED_PARAM__

#include <mysql.h>
#include "lib/string/ob_string.h"
#include "lib/mysqlclient/ob_isql_connection.h"
#include "lib/mysqlclient/ob_mysql_statement.h"
// #include "lib/mysqlclient/ob_mysql_connection.h"
#include "lib/mysqlclient/ob_mysql_result.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
namespace sqlclient
{
class ObBindParam;
class ObMySQLPreparedStatement;
class ObMySQLPreparedParam
{
friend ObMySQLPreparedStatement;
public:
  explicit ObMySQLPreparedParam(ObMySQLPreparedStatement &stmt);
  ~ObMySQLPreparedParam();
  int init();
  int bind_param();
  void close();
  int bind_param(ObBindParam &param);
  int64_t get_stmt_param_count() const { return param_count_; }

private:
  ObMySQLPreparedStatement &stmt_;
  common::ObIAllocator *alloc_;
  int64_t param_count_;
  MYSQL_BIND *bind_;
};
}
}
}
#endif
