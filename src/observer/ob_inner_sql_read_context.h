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

#ifndef OCEANBASE_OBSERVER_OB_INNER_SQL_READ_CONTEXT_H_
#define OCEANBASE_OBSERVER_OB_INNER_SQL_READ_CONTEXT_H_

#include "lib/mysqlclient/ob_isql_result_handler.h"
#include "observer/virtual_table/ob_virtual_table_iterator_factory.h"
#include "ob_inner_sql_result.h"
#include "ob_inner_sql_connection.h"

namespace oceanbase
{
namespace sql
{
class ObResultSet;
}

namespace observer
{
class ObInnerSQLConnection;

class ObInnerSQLReadContext : public common::sqlclient::ObISQLResultHandler
{
public:
  explicit ObInnerSQLReadContext(ObInnerSQLConnection &conn);
  virtual ~ObInnerSQLReadContext();
  virtual common::sqlclient::ObMySQLResult *mysql_result() { return &result_; }
  ObInnerSQLResult &get_result() { return result_; }
  ObVirtualTableIteratorFactory &get_vt_iter_factory() { return vt_iter_factory_; }

private:
  // define order dependent:
  // %conn_ref_ (session info) need be destructed after %result_
  // %vt_iter_factory_ need be destructed after %result_
  ObInnerSQLConnection::RefGuard conn_ref_;
  ObVirtualTableIteratorFactory vt_iter_factory_;
  ObInnerSQLResult result_;

  DISALLOW_COPY_AND_ASSIGN(ObInnerSQLReadContext);
};

} // end of namespace observer
} // end of namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_INNER_SQL_READ_CONTEXT_H_
