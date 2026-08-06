/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_SQL_CATALOG_H_
#define OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_SQL_CATALOG_H_

#include <functional>
#include <string>
#include <vector>

#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "common/mysqlclient/ob_mysql_result.h"

namespace oceanbase
{
namespace share
{

// A small SQL-catalog adapter used by the plugin manager.  It deliberately
// exposes the same narrow operations the catalog needs, while using seekdb's
// normal SQL system-catalog connection and transaction/WAL machinery.  No
// No file-backed database handle or private SQL dialect is involved here.
class ObPluginSqlBinder
{
public:
  ObPluginSqlBinder() = default;
  int bind_int(int32_t value);
  int bind_int64(int64_t value);
  int bind_text(const char *value);
  int bind_text(const char *value, int value_len);
  int bind_blob(const void *value);
  int bind_blob(const void *value, int value_len);

  const std::vector<std::string> &values() const { return values_; }

private:
  int append_text(const char *value, int value_len);
  std::vector<std::string> values_;
};

class ObPluginSqlRowReader
{
public:
  explicit ObPluginSqlRowReader(common::sqlclient::ObMySQLResult *result = nullptr)
      : result_(result) {}

  void set_result(common::sqlclient::ObMySQLResult *result) { result_ = result; }
  int64_t get_int64(int column) const;
  int32_t get_int(int column) const;
  const char *get_text(int column, int *len = nullptr) const;
  common::ObString get_string(int column) const;
  const void *get_blob(int column, int *len = nullptr) const;

private:
  common::sqlclient::ObMySQLResult *result_;
};

class ObPluginSqlConnection
{
public:
  explicit ObPluginSqlConnection(common::ObISQLClient *client = nullptr);
  ~ObPluginSqlConnection();

  bool is_valid() const { return nullptr != client_; }
  int query(const char *sql,
            const std::function<int(ObPluginSqlBinder &)> &binder,
            const std::function<int(ObPluginSqlRowReader &)> &row_processor);
  int execute(const char *sql,
              const std::function<int(ObPluginSqlBinder &)> &binder = nullptr,
              int64_t *affected_rows = nullptr);
  int begin_transaction();
  int commit();
  int rollback();
  bool is_in_transaction() const { return transaction_.is_started(); }

private:
  int render_sql(const char *sql,
                 const std::function<int(ObPluginSqlBinder &)> &binder,
                 std::string &rendered) const;
  common::ObISQLClient *executor() const;

  common::ObISQLClient *client_;
  mutable common::ObMySQLTransaction transaction_;
};

class ObPluginSqlConnectionGuard
{
public:
  explicit ObPluginSqlConnectionGuard(common::ObISQLClient *client)
      : connection_(client) {}
  ObPluginSqlConnection *get_connection() { return &connection_; }
  const ObPluginSqlConnection *get_connection() const { return &connection_; }
  bool is_valid() const { return connection_.is_valid(); }
  ObPluginSqlConnection *operator->() { return &connection_; }
  explicit operator bool() const { return is_valid(); }

private:
  ObPluginSqlConnection connection_;
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_SQL_CATALOG_H_
