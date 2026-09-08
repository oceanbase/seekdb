// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
#include "share/ob_global_stat_proxy.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/inner_table/ob_dump_inner_table_schema.h"
#include "rootserver/ob_runtime_ddl_service.h"
#include "sql/optimizer/stat/ob_dbms_stats_preferences.h"
#include "sql/optimizer/stat/ob_opt_stat_sql_service.h"
#include "lib/oblog/ob_log_time_fmt.h"
#include "share/schema/ob_schema_service.h"
#include "share/redolog/ob_log_file_handler.h"
#include "share/ob_io_device_helper.h"
#include "share/ob_max_id_fetcher.h"

namespace bootstrap_sql_checks {
using namespace oceanbase;
using namespace oceanbase::common;

inline bool check_slog_file_ids()
{
  char path[40] = {};
  const int64_t values[] = {1, INT64_C(2147483648), INT64_C(4294967294)};
  for (const int64_t value : values) {
    if (ObLogFileHandler::format_file_path(path, sizeof(path), "/slog", value) != OB_SUCCESS
        || std::string(path) != "/slog/" + std::to_string(value)) return false;
  }
  if (ObLogFileHandler::format_file_path(path, 8, "/slog", 1) != OB_SUCCESS
      || ObLogFileHandler::format_file_path(path, 7, "/slog", 1) != OB_BUF_NOT_ENOUGH
      || ObLogFileHandler::format_file_path(path, sizeof(path), "/slog", 0) != OB_INVALID_ARGUMENT
      || ObLogFileHandler::format_file_path(path, sizeof(path), "/slog", UINT32_MAX) != OB_INVALID_ARGUMENT) return false;
  share::ObGetFileIdRangeFunctor range("/slog");
  const auto visit = [](share::ObGetFileIdRangeFunctor &range, const char *name) {
    dirent entry = {};
    std::strcpy(entry.d_name, name);
    return range.func(&entry);
  };
  if (visit(range, ".") != OB_SUCCESS || visit(range, "temporary") != OB_SUCCESS
      || visit(range, "2147483648") != OB_SUCCESS || visit(range, "4294967294") != OB_SUCCESS
      || range.get_min_file_id() != UINT32_C(2147483648) || range.get_max_file_id() != UINT32_C(4294967294)
      || visit(range, "1") != OB_SUCCESS || range.get_min_file_id() != 1) return false;
  for (const char *invalid : {"", "0", "4294967295", "4294967296", "18446744073709551616"}) {
    if (visit(range, invalid) != OB_INVALID_ARGUMENT
        || range.get_min_file_id() != 1 || range.get_max_file_id() != UINT32_C(4294967294)) return false;
  }
  return true;
}

inline bool check_log_timestamps()
{
  // Exercise a sentinel before initializing the thread-local calendar cache,
  // matching the background freeze worker that originally trapped in Date.
  if (std::string(ObTime2Str::ob_timestamp_str(INT64_MAX)) != "9223372036854775807"
      || std::string(ObTime2Str::ob_timestamp_str(INT64_MIN)) != "-9223372036854775808"
      || std::string(ObTime2Str::ob_timestamp_str(-1)) != "-1"
      || std::string(ObTime2Str::ob_timestamp_str_range<HOUR, USECOND>(INT64_MAX)) != "9223372036854775807") return false;
  const std::string first = ObTime2Str::ob_timestamp_str(INT64_C(1700000000000001));
  const std::string cached = ObTime2Str::ob_timestamp_str(INT64_C(1700000000999999));
  const std::string next = ObTime2Str::ob_timestamp_str(INT64_C(1700000001000000));
  const std::string future = ObTime2Str::ob_timestamp_str(INT64_C(2200000000000001));
  return first.size() == 26 && first.substr(19) == ".000001"
      && cached.substr(0, 19) == first.substr(0, 19) && cached.substr(19) == ".999999"
      && next.size() == 26 && next.substr(19) == ".000000"
      && future.size() == 26 && future.substr(19) == ".000001"
      && std::string(ObTime2Str::ob_timestamp_str_range<MSECOND, USECOND>(INT64_C(1700000000000001))) == "000001"
      && std::string(ObTime2Str::ob_timestamp_str_range<HOUR, USECOND>(-1)) == "-1";
}

inline bool check_integer_literals()
{
  const int64_t signed_values[] = {INT64_MIN, -INT64_C(4294967419), -1, 0, INT64_C(4294967419), INT64_MAX};
  const uint64_t unsigned_values[] = {0, UINT64_C(4294967419), UINT64_MAX};
  const auto check = [](const ObObj &obj, const std::string &expected) {
    char buffer[80] = {};
    int64_t pos = 0;
    if (obj.print_sql_literal(buffer, sizeof(buffer), pos) != OB_SUCCESS
        || std::string(buffer, pos) != expected) return false;
    pos = 0;
    if (obj.print_plain_str_literal(buffer, sizeof(buffer), pos) != OB_SUCCESS
        || std::string(buffer, pos) != expected) return false;
    pos = 0;
    return obj.print_varchar_literal(buffer, sizeof(buffer), pos) == OB_SUCCESS
        && std::string(buffer, pos) == "'" + expected + "'";
  };
  ObObj obj;
  for (const auto value : signed_values) {
    obj.set_int(value);
    if (!check(obj, std::to_string(value))) return false;
  }
  for (const auto value : unsigned_values) {
    obj.set_uint64(value);
    if (!check(obj, std::to_string(value))) return false;
    obj.set_enum(value);
    if (!check(obj, std::to_string(value))) return false;
    obj.set_set(value);
    if (!check(obj, std::to_string(value))) return false;
  }
  char buffer[80] = {};
  int64_t pos = 0;
  obj.set_bit(UINT64_MAX);
  if (obj.print_plain_str_literal(buffer, sizeof(buffer), pos) != OB_SUCCESS
      || std::string(buffer, pos) != "18446744073709551615") return false;
  ObObjPrintParams hex;
  hex.binary_string_print_hex_ = true;
  pos = 0;
  if (obj.print_plain_str_literal(buffer, sizeof(buffer), pos, hex) != OB_SUCCESS
      || std::string(buffer, pos) != "FFFFFFFFFFFFFFFF") return false;
  obj.set_unknown(INT64_C(4294967419));
  pos = 0;
  return obj.print_sql_literal(buffer, sizeof(buffer), pos) == OB_SUCCESS
      && std::string(buffer, pos) == ":4294967419";
}

// Capture generated SQL only. This fixture does not execute or emulate SQL.
class SqlCapture : public ObISQLClient {
public:
  std::string sql;
  int64_t affected = 1;
  int writes = 0;
  int reads = 0;
  int escape(const char *, int64_t, char *, int64_t, int64_t &) override { return OB_NOT_SUPPORTED; }
  int read(ReadResult &, const char *text, int32_t) override {
    sql = text;
    ++reads;
    return OB_NOT_SUPPORTED;
  }
  int write(const char *text, int32_t, int64_t &rows) override {
    sql = text;
    rows = affected;
    ++writes;
    return OB_SUCCESS;
  }
  sqlclient::ObISQLConnection *get_connection() override { return nullptr; }
};

inline bool check_schema_refresh_sql(share::schema::ObSchemaService &service)
{
  using namespace share::schema;
  SqlCapture capture;
  ObArray<uint64_t> ids;
  if (ids.push_back(1) != OB_SUCCESS || ids.push_back(UINT64_C(4294967419)) != OB_SUCCESS
      || ids.push_back(UINT64_MAX) != OB_SUCCESS) return false;
  const ObRefreshSchemaStatus status(OB_INVALID_TIMESTAMP, OB_INVALID_VERSION);
  bool changed = false;
  const int64_t old_version = INT64_C(4294967419);
  const int64_t new_version = INT64_C(4294967420);
  const std::string expected = "SELECT 1 FROM __all_ddl_operation WHERE SCHEMA_VERSION > 4294967419 "
      "AND SCHEMA_VERSION <= 4294967420 AND OPERATION_TYPE > "
      + std::to_string(static_cast<int>(OB_DDL_TABLE_OPERATION_BEGIN))
      + " AND OPERATION_TYPE < " + std::to_string(static_cast<int>(OB_DDL_TABLE_OPERATION_END))
      + " AND TABLE_ID IN (1,4294967419,18446744073709551615)";
  // The real service generates the query; stop at the client boundary without executing it.
  if (service.check_sys_schema_change(capture, status, ids, old_version, new_version, changed) != OB_NOT_SUPPORTED
      || capture.reads != 1 || capture.sql != expected) return false;
  if (service.check_sys_schema_change(capture, status, ids, new_version, new_version, changed) != OB_SUCCESS
      || capture.reads != 1 || changed) return false;
  ObSchemaService::SchemaOperationSetWithAlloc operations;
  if (service.get_increment_schema_operations(status, old_version, new_version, capture, operations) != OB_NOT_SUPPORTED
      || capture.reads != 2
      || capture.sql != "SELECT * FROM __all_ddl_operation WHERE schema_version > 4294967419 AND schema_version <= 4294967420 ORDER BY schema_version ASC") return false;
  ObSimpleSysVariableSchema variables;
  if (service.get_sys_variable(capture, status, new_version, variables) != OB_NOT_SUPPORTED
      || capture.sql != "SELECT max(schema_version) as max_schema_version FROM __all_sys_variable_history WHERE schema_version <= 4294967420") return false;
  ObArray<ObSimpleUserSchema> users;
  if (service.get_all_users(capture, status, new_version, users) != OB_NOT_SUPPORTED
      || capture.sql.find("SELECT * FROM __all_user_history WHERE SCHEMA_VERSION <= 4294967420 ORDER BY user_id desc,") != 0) return false;
  ObArray<ObSchemaIdVersion> versions;
  if (service.get_table_schema_versions(capture, ids, versions) != OB_NOT_SUPPORTED
      || capture.sql.find("WHERE table_id IN (1)") == std::string::npos
      || capture.sql.find("WHERE table_id IN (4294967419,18446744073709551615)") == std::string::npos) return false;
  return capture.reads == 5;
}

inline bool check_metadata_updates()
{
  SqlCapture capture;
  ObMySQLProxy proxy;
  share::ObMaxIdFetcher fetcher(proxy);
  for (const uint64_t id : {UINT64_C(1), UINT64_C(4294967419), static_cast<uint64_t>(INT64_MAX)}) {
    if (fetcher.update_max_id(capture, share::OB_MAX_USED_OBJECT_ID_TYPE, id) != OB_SUCCESS
        || capture.sql != "UPDATE __all_sys_stat SET VALUE = '" + std::to_string(id)
            + "', gmt_modified = now(6) WHERE NAME = 'ob_max_used_object_id'") return false;
  }
  int64_t affected = 0;
  share::SCN scn;
  if (scn.convert_for_inner_table_field(UINT64_C(4294967419)) != OB_SUCCESS) return false;
  if (share::ObGlobalStatProxy::update_snapshot_gc_scn(capture, scn, affected) != OB_SUCCESS
      || capture.sql != "UPDATE __all_core_table SET column_value = 4294967419 WHERE table_name = '__all_global_stat' AND column_name = 'snapshot_gc_scn' AND column_value < 4294967419") return false;
  if (share::ObGlobalStatProxy::advance_change_stream_refresh_scn(capture, scn, affected) != OB_SUCCESS
      || capture.sql != "UPDATE __all_core_table SET column_value = 4294967419 WHERE table_name = '__all_global_stat' AND column_name = 'change_stream_refresh_scn' AND column_value < 4294967419") return false;
  if (share::ObGlobalStatProxy::advance_change_stream_min_dep_lsn(capture, INT64_C(4294967419), affected) != OB_SUCCESS
      || capture.sql != "UPDATE __all_core_table SET column_value = 4294967419 WHERE table_name = '__all_global_stat' AND column_name = 'change_stream_min_dep_lsn' AND column_value < 4294967419") return false;
  const int writes = capture.writes;
  return share::ObGlobalStatProxy::advance_change_stream_min_dep_lsn(capture, -1, affected) == OB_INVALID_ARGUMENT
      && capture.writes == writes;
}

inline bool check_schema_rows()
{
  ObArenaAllocator allocator;
  share::ObCoreTableLoadInfoConstructor constructor(ObString::make_string("__all_table_history"), allocator);
  share::ObDMLSqlSplicer splicer(share::ObDMLSqlSplicer::NAKED_VALUE_MODE);
  if (splicer.add_column("wide", UINT64_C(4294967419)) != OB_SUCCESS
      || splicer.add_column("label", ObString::make_string("alpha")) != OB_SUCCESS
      || splicer.add_column(true, "optional") != OB_SUCCESS
      || constructor.add_lines(1, splicer) != OB_SUCCESS
      || constructor.add_lines(2, splicer) != OB_SUCCESS) return false;
  const auto &rows = constructor.get_rows();
  const char *expected[] = {
    "'__all_table_history', 1, 'wide', '4294967419'",
    "'__all_table_history', 1, 'label', 'alpha'",
    "'__all_table_history', 1, 'optional', NULL",
    "'__all_table_history', 2, 'wide', '4294967419'",
    "'__all_table_history', 2, 'label', 'alpha'",
    "'__all_table_history', 2, 'optional', NULL",
  };
  if (rows.count() != 6) return false;
  for (int64_t i = 0; i < rows.count(); ++i) {
    if (rows.at(i) != ObString::make_string(expected[i])) return false;
  }
  share::ObDMLSqlSplicer hex;
  if (hex.add_column("binary", ObHexEscapeSqlStr(ObString::make_string("ab"))) != OB_SUCCESS
      || constructor.add_lines(3, hex) != OB_SUCCESS || rows.count() != 7) return false;
  return rows.at(6) == ObString::make_string("'__all_table_history', 3, 'binary', X'6162'");
}

inline bool check_runtime_stats()
{
  rootserver::ObSysStat stats;
  SqlCapture capture;
  capture.affected = stats.item_list_.get_size();
  for (auto *item = stats.item_list_.get_first(); item != stats.item_list_.get_header(); item = item->get_next()) {
    item->value_.set_int(INT64_C(4294967419));
  }
  if (rootserver::ObRuntimeDDLService::replace_sys_stat(stats, capture) != OB_SUCCESS) return false;
  for (auto *item = stats.item_list_.get_first(); item != stats.item_list_.get_header(); item = item->get_next()) {
    if (capture.sql.find(item->name_) == std::string::npos
        || capture.sql.find(item->info_) == std::string::npos) return false;
  }
  size_t offset = 0;
  int64_t values = 0;
  while ((offset = capture.sql.find("'4294967419'", offset)) != std::string::npos) {
    ++values;
    ++offset;
  }
  return values == capture.affected;
}

inline bool check_optimizer_defaults()
{
  ObArray<int64_t> partitions;
  ObSqlString partition_sql;
  for (const int64_t id : {INT64_MIN, INT64_C(4294967419), INT64_MAX}) {
    if (partitions.push_back(id) != OB_SUCCESS) return false;
  }
  if (ObOptStatSqlService::generate_in_list(partitions, partition_sql) != OB_SUCCESS
      || std::string(partition_sql.ptr()) != "(-9223372036854775808, 4294967419, 9223372036854775807)") return false;
  ObSqlString initial_sql;
  ObSqlString reset_sql;
  int64_t initial_rows = 0;
  int64_t reset_rows = 0;
  if (ObDbmsStatsPreferences::gen_init_global_prefs_sql(initial_sql, false, &initial_rows) != OB_SUCCESS
      || ObDbmsStatsPreferences::gen_init_global_prefs_sql(reset_sql, true, &reset_rows) != OB_SUCCESS) return false;
  const std::string initial(initial_sql.ptr(), initial_sql.length());
  const std::string reset(reset_sql.ptr(), reset_sql.length());
  const std::string prefix = "REPLACE INTO __all_optstat_global_prefs(sname, sval1, sval2, spare4) VALUES ";
  const std::string retention = "('STATS_RETENTION', '31', CURRENT_TIMESTAMP, NULL), ";
  // Reset must preserve the retention setting while initializing the same remaining preferences.
  return initial.compare(0, prefix.size() + retention.size(), prefix + retention) == 0
      && reset.compare(0, prefix.size(), prefix) == 0
      && initial.substr(prefix.size() + retention.size()) == reset.substr(prefix.size())
      && reset.find("STATS_RETENTION") == std::string::npos
      && initial.find("('DEGREE', NULL, CURRENT_TIMESTAMP, NULL)") != std::string::npos
      && initial_rows == reset_rows + 1 && reset_rows > 0;
}
}
