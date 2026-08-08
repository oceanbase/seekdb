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

#ifndef OCEANBASE_SQL_SESSION_OB_INNER_SQL_CONNECTION_H_
#define OCEANBASE_SQL_SESSION_OB_INNER_SQL_CONNECTION_H_

#include "common/mysqlclient/ob_isql_connection.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{

// SQL-owned seam for the nested-session behavior required by foreign-key
// execution.  All ordinary SQL and transaction operations stay on the lower
// common::sqlclient::ObISQLConnection interface.
class ObIInnerSQLConnection : public common::sqlclient::ObISQLConnection
{
public:
  class SavedValue
  {
  public:
    SavedValue() { reset(); }
    void reset()
    {
      read_context_ = nullptr;
      execute_start_timestamp_ = 0;
      execute_end_timestamp_ = 0;
    }

  public:
    // Opaque implementation state; only the Observer adapter may interpret it.
    void *read_context_;
    int64_t execute_start_timestamp_;
    int64_t execute_end_timestamp_;
  };

  virtual int begin_nested_session(
      ObSQLSessionInfo::StmtSavedValue &saved_session,
      SavedValue &saved_connection,
      bool skip_current_statement_tables) = 0;
  virtual int end_nested_session(
      ObSQLSessionInfo::StmtSavedValue &saved_session,
      SavedValue &saved_connection) = 0;
};

inline ObIInnerSQLConnection *as_inner_sql_connection(
    common::sqlclient::ObISQLConnection *connection)
{
  return dynamic_cast<ObIInnerSQLConnection *>(connection);
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_SESSION_OB_INNER_SQL_CONNECTION_H_
