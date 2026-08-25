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

#ifndef OCEANBASE_UNITTEST_SHARE_PLUGIN_SQLITE_SQL_CLIENT_H_
#define OCEANBASE_UNITTEST_SHARE_PLUGIN_SQLITE_SQL_CLIENT_H_

#include <cstring>
#include <new>
#include <string>

#include "common/mysqlclient/ob_isql_result_handler.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "common/mysqlclient/ob_isql_connection.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "share/storage/ob_sqlite_connection_pool.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{
namespace testing
{

// Catalog production code speaks the regular ObISQLClient interface.  Keep
// SQLite strictly inside the unit-test fixture: it supplies isolated durable
// databases and lets existing tests inspect rows and inject corrupt metadata.
inline std::string translate_mysql_catalog_sql(const char *sql)
{
  std::string translated;
  if (nullptr == sql) return translated;
  translated.reserve(std::strlen(sql));
  bool quoted = false;
  for (const char *cursor = sql; '\0' != *cursor; ++cursor) {
    if (quoted && '\\' == *cursor && '\0' != cursor[1]) {
      const char escaped = *++cursor;
      if ('\'' == escaped) {
        translated.append("''");
      } else {
        translated.push_back(escaped);
      }
    } else {
      translated.push_back(*cursor);
      if ('\'' == *cursor) quoted = !quoted;
    }
  }

  static const std::string mysql_upsert(" ON DUPLICATE KEY UPDATE ");
  const size_t upsert_position = translated.find(mysql_upsert);
  if (std::string::npos != upsert_position) {
    static const std::string sqlite_upsert(" ON CONFLICT DO UPDATE SET ");
    translated.replace(upsert_position, mysql_upsert.size(), sqlite_upsert);
    size_t search = upsert_position + sqlite_upsert.size();
    while (std::string::npos !=
           (search = translated.find("VALUES(", search))) {
      const size_t close = translated.find(')', search + 7);
      if (std::string::npos == close) break;
      const std::string column = translated.substr(search + 7,
                                                    close - search - 7);
      translated.replace(search, close - search + 1, "excluded." + column);
      search += 9 + column.size();
    }
  }
  return translated;
}

class SqliteCatalogResult final : public common::sqlclient::ObMySQLResult
{
public:
  explicit SqliteCatalogResult(ObSQLiteConnection *connection)
      : connection_(connection), statement_(nullptr), reader_() {}

  ~SqliteCatalogResult() override { (void)close(); }

  int init(const char *sql)
  {
    if (nullptr == connection_) return common::OB_NOT_INIT;
    const std::string translated = translate_mysql_catalog_sql(sql);
    return connection_->prepare_query(translated.c_str(), nullptr, statement_);
  }

  int64_t get_column_count() const override { return 0; }

  int close() override
  {
    if (nullptr != statement_) {
      connection_->finalize_query(statement_);
      statement_ = nullptr;
    }
    return common::OB_SUCCESS;
  }

  int next() override
  {
    return nullptr == statement_ ? common::OB_ITER_END :
        connection_->step_query(statement_, reader_);
  }

  int get_int(const int64_t index, int64_t &value) const override
  {
    value = reader_.get_int64(static_cast<int>(index));
    return common::OB_SUCCESS;
  }

  int get_varchar(const int64_t index,
                  common::ObString &value) const override
  {
    value = reader_.get_string(static_cast<int>(index));
    return common::OB_SUCCESS;
  }

  int get_uint(const int64_t, uint64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_datetime(const int64_t, int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_date(const int64_t, int32_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_time(const int64_t, int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_year(const int64_t, uint8_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_bool(const int64_t, bool &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_float(const int64_t, float &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_double(const int64_t, double &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_timestamp(const int64_t, const common::ObTimeZoneInfo *,
                    int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_type(const int64_t, common::ObObjMeta &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_obj(const int64_t, common::ObObj &,
              const common::ObTimeZoneInfo *,
              common::ObIAllocator *) const override
  { return common::OB_NOT_SUPPORTED; }

  int get_int(const char *, int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_uint(const char *, uint64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_datetime(const char *, int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_date(const char *, int32_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_time(const char *, int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_year(const char *, uint8_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_bool(const char *, bool &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_varchar(const char *, common::ObString &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_float(const char *, float &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_double(const char *, double &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_timestamp(const char *, const common::ObTimeZoneInfo *,
                    int64_t &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_type(const char *, common::ObObjMeta &) const override
  { return common::OB_NOT_SUPPORTED; }
  int get_obj(const char *, common::ObObj &) const override
  { return common::OB_NOT_SUPPORTED; }

private:
  int inner_get_number(const int64_t, common::number::ObNumber &,
                       IAllocator &) const override
  { return common::OB_NOT_SUPPORTED; }
  int inner_get_number(const char *, common::number::ObNumber &,
                       IAllocator &) const override
  { return common::OB_NOT_SUPPORTED; }

  ObSQLiteConnection *connection_;
  ObSQLiteStmt *statement_;
  ObSQLiteRowReader reader_;
};

class SqliteCatalogResultHandler final
    : public common::sqlclient::ObISQLResultHandler
{
public:
  SqliteCatalogResultHandler(ObSQLiteConnectionPool *pool,
                             ObSQLiteConnection *connection)
      : guard_(nullptr == connection ? pool : nullptr),
        result_(nullptr == connection ? guard_.get_connection() : connection)
  {}

  int init(const char *sql) { return result_.init(sql); }

  common::sqlclient::ObMySQLResult *mysql_result() override
  { return &result_; }

private:
  ObSQLiteConnectionGuard guard_;
  SqliteCatalogResult result_;
};

class SqliteCatalogConnection final
    : public common::sqlclient::ObISQLConnection
{
public:
  explicit SqliteCatalogConnection(ObSQLiteConnectionPool *pool)
      : guard_(pool) {}

  bool is_valid() const { return guard_.is_valid(); }

  int execute_read(const common::ObString &sql,
                   common::ObISQLClient::ReadResult &result,
                   bool) override
  {
    std::string owned_sql(sql.ptr(), sql.length());
    ObSQLiteConnectionPool *pool = nullptr;
    ObSQLiteConnection *connection = guard_.get_connection();
    SqliteCatalogResultHandler *handler = nullptr;
    int ret = result.create_handler(handler, pool, connection);
    if (common::OB_SUCCESS == ret) ret = handler->init(owned_sql.c_str());
    return ret;
  }

  int execute_write(const common::ObString &sql,
                    int64_t &affected_rows, bool) override
  {
    const std::string owned_sql(sql.ptr(), sql.length());
    const std::string translated =
        translate_mysql_catalog_sql(owned_sql.c_str());
    return guard_->execute(translated.c_str(), nullptr, &affected_rows);
  }

  int execute_proc(common::ObIAllocator &, common::ParamStore &,
                   common::ObString &, const share::schema::ObRoutineInfo &,
                   const common::ObIArray<const pl::ObUserDefinedType *> &,
                   const common::ObTimeZoneInfo *, common::ObObj *,
                   bool) override
  { return common::OB_NOT_SUPPORTED; }

  int start_transaction(bool) override
  { return guard_->execute("BEGIN IMMEDIATE", nullptr); }

  int rollback() override { return guard_->rollback(); }
  int commit() override { return guard_->commit(); }

  int get_session_variable(const common::ObString &, int64_t &) override
  { return common::OB_NOT_SUPPORTED; }
  int set_session_variable(const common::ObString &, int64_t) override
  { return common::OB_NOT_SUPPORTED; }
  int set_session_variable(const common::ObString &,
                           const common::ObString &) override
  { return common::OB_NOT_SUPPORTED; }

private:
  ObSQLiteConnectionGuard guard_;
};

class SqliteCatalogClient final : public common::ObISQLClient
{
public:
  explicit SqliteCatalogClient(ObSQLiteConnectionPool *pool) : pool_(pool) {}

  int escape(const char *, const int64_t, char *, const int64_t,
             int64_t &) override
  { return common::OB_NOT_SUPPORTED; }

  int read(ReadResult &result, const char *sql, const int32_t) override
  {
    ObSQLiteConnectionPool *pool = pool_;
    ObSQLiteConnection *connection = nullptr;
    SqliteCatalogResultHandler *handler = nullptr;
    int ret = result.create_handler(handler, pool, connection);
    if (common::OB_SUCCESS == ret) ret = handler->init(sql);
    return ret;
  }

  int write(const char *sql, const int32_t,
            int64_t &affected_rows) override
  {
    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) return common::OB_NOT_INIT;
    const std::string translated = translate_mysql_catalog_sql(sql);
    return guard->execute(translated.c_str(), nullptr, &affected_rows);
  }

  common::sqlclient::ObISQLConnection *get_connection() override
  { return nullptr; }

  int acquire_connection(common::sqlclient::ObISQLConnectionGuard &guard,
                         const int32_t) override
  {
    SqliteCatalogConnection *connection =
        new (std::nothrow) SqliteCatalogConnection(pool_);
    if (nullptr == connection) return common::OB_ALLOCATE_MEMORY_FAILED;
    if (!connection->is_valid()) {
      delete connection;
      return common::OB_NOT_INIT;
    }
    const int ret = guard.assign(
        static_cast<common::sqlclient::ObISQLConnection *>(connection),
        [](common::sqlclient::ObISQLConnection *owned) { delete owned; });
    if (common::OB_SUCCESS != ret) delete connection;
    return ret;
  }

  int create_catalog_tables()
  {
    static const char *const schemas[] = {
        "CREATE TABLE __all_plugin_sequence("
        "sequence_name TEXT PRIMARY KEY,next_value INTEGER NOT NULL)",

        "CREATE TABLE __all_plugin_package("
        "plugin_id TEXT PRIMARY KEY,relative_path TEXT,build_id TEXT,"
        "package_digest TEXT,version_major INTEGER,version_minor INTEGER,"
        "version_patch INTEGER,catalog_version INTEGER,data_format_version INTEGER,"
        "verification_level INTEGER,desired_state INTEGER,actual_state INTEGER,"
        "generation INTEGER,runtime_incarnation TEXT,operation_id TEXT,"
        "last_phase INTEGER,last_status INTEGER,last_error TEXT,operator_id TEXT,"
        "audit_id TEXT,gmt_create INTEGER,gmt_modified INTEGER)",

        "CREATE TABLE __all_plugin_operation("
        "operation_id TEXT PRIMARY KEY,plugin_id TEXT,generation INTEGER,"
        "runtime_incarnation TEXT,kind INTEGER,state INTEGER,relative_path TEXT,"
        "package_digest TEXT,phase INTEGER,status INTEGER,actual_state INTEGER,"
        "start_entered INTEGER,candidate_prepared INTEGER,stop_entered INTEGER,"
        "error TEXT,operator_id TEXT,audit_id TEXT,gmt_create INTEGER,"
        "gmt_modified INTEGER)",

        "CREATE TABLE __all_plugin_service("
        "plugin_id TEXT,generation INTEGER,service_id TEXT,abi_major INTEGER,"
        "abi_minor INTEGER,abi_patch INTEGER,capabilities INTEGER,"
        "PRIMARY KEY(plugin_id,generation,service_id,abi_major))",

        "CREATE TABLE __all_plugin_extension("
        "plugin_id TEXT,generation INTEGER,kind INTEGER,object_id TEXT,"
        "sql_name TEXT,physical_format_id TEXT,source_type_id TEXT,"
        "target_type_id TEXT,static_result_type_id TEXT,hook_point TEXT,"
        "catalog_object_kind TEXT,schema_name TEXT,definition_digest TEXT,"
        "physical_format_version INTEGER,minimum_arity INTEGER,"
        "maximum_arity INTEGER,cast_context INTEGER,cost INTEGER,"
        "priority INTEGER,flags INTEGER,implementation_service_id TEXT,"
        "implementation_min_version_major INTEGER,"
        "implementation_min_version_minor INTEGER,"
        "implementation_min_version_patch INTEGER,"
        "implementation_max_version_major INTEGER,"
        "implementation_max_version_minor INTEGER,"
        "implementation_max_version_patch INTEGER,required_capabilities INTEGER,"
        "PRIMARY KEY(plugin_id,generation,kind,object_id))",

        "CREATE TABLE __all_plugin_dependency("
        "consumer_kind INTEGER,consumer_id TEXT,consumer_plugin_id TEXT,"
        "consumer_generation INTEGER,provider_plugin_id TEXT,"
        "provider_generation INTEGER,dependency_kind INTEGER,dependency_id TEXT,"
        "service_abi_major INTEGER,optional INTEGER,"
        "requested_min_version_major INTEGER,requested_min_version_minor INTEGER,"
        "requested_min_version_patch INTEGER,requested_max_version_major INTEGER,"
        "requested_max_version_minor INTEGER,requested_max_version_patch INTEGER,"
        "provider_version_major INTEGER,provider_version_minor INTEGER,"
        "provider_version_patch INTEGER,required_capabilities INTEGER,"
        "PRIMARY KEY(consumer_kind,consumer_id,consumer_plugin_id,"
        "consumer_generation,provider_plugin_id,provider_generation,"
        "dependency_kind,dependency_id,service_abi_major))",

        "CREATE TABLE __all_sql_extension_type("
        "type_id TEXT PRIMARY KEY,sql_name TEXT,physical_format_id TEXT,"
        "physical_format_version INTEGER,plugin_id TEXT,generation INTEGER,"
        "flags INTEGER)",

        "CREATE TABLE __all_sql_extension_function("
        "function_id TEXT PRIMARY KEY,kind INTEGER,sql_name TEXT,"
        "result_type_id TEXT,minimum_arity INTEGER,maximum_arity INTEGER,"
        "signature_flags INTEGER,plugin_id TEXT,generation INTEGER,flags INTEGER)",

        "CREATE TABLE __all_sql_extension_argument("
        "function_id TEXT,ordinal_position INTEGER,type_id TEXT,"
        "PRIMARY KEY(function_id,ordinal_position))",

        "CREATE TABLE __all_sql_extension_column("
        "function_id TEXT,ordinal_position INTEGER,column_name TEXT,"
        "type_id TEXT,nullable INTEGER,"
        "PRIMARY KEY(function_id,ordinal_position))"};

    ObSQLiteConnectionGuard guard(pool_);
    if (!guard) return common::OB_NOT_INIT;
    int ret = common::OB_SUCCESS;
    for (const char *schema : schemas) {
      if (common::OB_SUCCESS != (ret = guard->execute(schema, nullptr))) {
        break;
      }
    }
    return ret;
  }

private:
  ObSQLiteConnectionPool *pool_;
};

} // namespace testing
} // namespace plugin
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_UNITTEST_SHARE_PLUGIN_SQLITE_SQL_CLIENT_H_
