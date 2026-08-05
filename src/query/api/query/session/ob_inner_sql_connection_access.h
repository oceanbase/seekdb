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

#ifndef OCEANBASE_QUERY_API_SESSION_OB_INNER_SQL_CONNECTION_ACCESS_H_
#define OCEANBASE_QUERY_API_SESSION_OB_INNER_SQL_CONNECTION_ACCESS_H_

#include <stdint.h>
#include "common/mysqlclient/ob_isql_connection.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{
class ObISQLConnection;
}
}
namespace sql
{
class ObSQLSessionInfo;
}
namespace transaction
{
enum class ObTxDataSourceType : int64_t;
struct ObRegisterMdsFlag;
namespace tablelock
{
class ObLockObjRequest;
}
}
namespace query
{

// Transitional adapter for capabilities that exist only on Observer's inner
// SQL connection. Data-plane callers retain OBLib connection types and never
// depend on the native connection or SQL session definitions.
class ObInnerSQLConnectionAccess
{
public:
  static int create_connection_with_external_session(
      sql::ObSQLSessionInfo *session,
      common::sqlclient::ObISQLConnectionGuard &connection);

  static int create_spi_connection_with_external_session(
      sql::ObSQLSessionInfo *session,
      common::sqlclient::ObISQLConnectionGuard &connection);

  static sql::ObSQLSessionInfo *get_session(
      common::sqlclient::ObISQLConnection *connection);

  static int lock_obj(
      const transaction::tablelock::ObLockObjRequest &request,
      common::sqlclient::ObISQLConnection *connection);

  static int register_multi_data_source(
      common::sqlclient::ObISQLConnection *connection,
      transaction::ObTxDataSourceType type,
      const char *buffer,
      int64_t buffer_size);

  static int register_multi_data_source(
      common::sqlclient::ObISQLConnection *connection,
      transaction::ObTxDataSourceType type,
      const char *buffer,
      int64_t buffer_size,
      const transaction::ObRegisterMdsFlag &flag);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_SESSION_OB_INNER_SQL_CONNECTION_ACCESS_H_
