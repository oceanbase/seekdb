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

#ifndef __OB_COMMON_SQLCLIENT_OB_MYSQL_PREPARED_RESULT__
#define __OB_COMMON_SQLCLIENT_OB_MYSQL_PREPARED_RESULT__

#include <mysql.h>
#include "lib/string/ob_string.h"
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
class ObMySQLPreparedResult
{
friend ObMySQLPreparedStatement;
public:
  explicit ObMySQLPreparedResult(ObMySQLPreparedStatement &stmt);
  ~ObMySQLPreparedResult();
  int init();
  /*
   * close result
   */
  void close();
  /*
   * move next
   */
  /*
   * get result values
   */
  int64_t get_result_column_count() const { return result_column_count_; }

  int bind_result(ObBindParam &param);
  MYSQL_BIND *&get_bind() { return bind_; }
private:
  ObMySQLPreparedStatement &stmt_;
  common::ObIAllocator *alloc_;
  int64_t result_column_count_;
  MYSQL_BIND *bind_;
};
}
}
}

#endif

