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

#ifndef __OB_COMMON_SQLCLIENT_OB_MYSQL_STATEMENT__
#define __OB_COMMON_SQLCLIENT_OB_MYSQL_STATEMENT__

#include "lib/mysqlclient/ob_mysql_result_impl.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObMySQLConnection;

class ObMySQLStatement
{
public:
  ObMySQLStatement();
  ~ObMySQLStatement();
  ObMySQLConnection *get_connection();
  MYSQL *get_stmt_handler();
  MYSQL *get_conn_handler();
  int init(ObMySQLConnection &conn, const ObString &sql, int64_t param_count = 0);

  /*
   * close statement
   */

  /*
   * execute a SQL command, such as
   *  - set @@session.ob_query_timeout=10
   *  - commit
   *  - insert into t values (v1,v2),(v3,v4)
   */
  int execute_update(int64_t &affected_rows);
  /*
   * same as execute_update(affected_rows)
   * but ignore affected_rows
   */
  int execute_update();
  static bool is_need_disconnect_error(int ret);

  /*
   * ! Deprecated
   * use prepare method to read data instead
   * reference ObMySQLPrepareStatement
   */
  ObMySQLResult *execute_query(bool enable_use_result = false);
  int wait_for_mysql(int &status);
private:
  ObMySQLConnection *conn_;
  ObMySQLResultImpl result_;
  MYSQL *stmt_;
  ObString sql_str_;
};
} //namespace sqlclient
}
}
#endif
