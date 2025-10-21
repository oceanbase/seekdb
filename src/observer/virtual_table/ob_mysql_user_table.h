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

#ifndef OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_MYSQL_USER_TABLE_
#define OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_MYSQL_USER_TABLE_

#include "share/ob_virtual_table_scanner_iterator.h"

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace observer
{
class ObMySQLUserTable : public common::ObVirtualTableScannerIterator
{
private:
  enum MySQLUserTableColumns {
    HOST = 16,
    USER_NAME,
    PASSWD,
    SELECT_PRIV,
    INSERT_PRIV,
    UPDATE_PRIV,
    DELETE_PRIV,
    CREATE_PRIV,
    DROP_PRIV,
    RELOAD_PRIV,
    SHUTDOWN_PRIV,
    PROCESS_PRIV,
    FILE_PRIV,
    GRANT_PRIV,
    REFERENCES_PRIV,
    INDEX_PRIV,
    ALTER_PRIV,
    SHOW_DB_PRIV,
    SUPER_PRIV,
    CREATE_TMP_TABLE_PRIV,
    LOCK_TABLE_PRIV,
    EXECUTE_PRIV,
    REPL_SLAVE_PRIV,
    REPL_CLIENT_PRIV,
    CREATE_VIEW_PRIV,
    SHOW_VIEW_PRIV,
    CREATE_ROUTINE_PRIV,
    ALTER_ROUTINE_PRIV,
    CREATE_USER_PRIV,
    EVENT_PRIV,
    TRIGGER_PRIV,
    CREATE_TABLESPACE_PRIV,
    SSL_TYPE,
    SSL_CIPHER,
    X509_ISSUER,
    X509_SUBJECT,
    MAX_QUESTIONS,
    MAX_UPDATES,
    MAX_CONNECTIONS,
    MAX_USER_CONNECTIONS,
    PLUGIN,
    AUTHENTICATION_STRING,
    PASSWORD_EXPIRED,
    ACCOUNT_LOCKED,
    DROP_DATABASE_LINK_PRIV,
    CREATE_DATABASE_LINK_PRIV,
    CREATE_ROLE_PRIV,
    DROP_ROLE_PRIV,
  };
public:
  ObMySQLUserTable();
  virtual ~ObMySQLUserTable();

  virtual int inner_get_next_row(common::ObNewRow *&row);
  virtual void reset();

  inline void set_tenant_id(const uint64_t tenant_id) { tenant_id_ = tenant_id; }

private:
  uint64_t tenant_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMySQLUserTable);
};
}
}
#endif // OCEANBASE_OBSERVER_VIRTUAL_TABLE_OB_MYSQL_USER_TABLE_
