// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Row/transport fixture ONLY. Production catalog code and Rust validation run
// unchanged, but this does not execute SQL or implement transaction isolation.
#pragma once
#include "mysqlclient/ob_mysql_transaction.h"
#include "mysqlclient/ob_mysql_result.h"
#include "mysqlclient/ob_isql_result_handler.h"
#include <string>
#include <vector>
#include <functional>
#include <map>

namespace oceanbase { namespace common {
class ExtensionVersionRows final : public ObMySQLTransaction
{
public:
  struct Row {
    int64_t id = 91;
    int64_t owner = 123;
    std::string version = "1.0";
    std::string module;
    std::string dependency;
    // Opt-in positional rows for catalog tests with a different SELECT shape.
    // Once populated, missing or wrongly typed fields fail rather than falling
    // back to the extension-instance defaults above.
    std::map<int64_t, int64_t> integers{};
    std::map<int64_t, std::string> strings{};
  };
  std::vector<Row> rows{Row{}};
  bool active = false;
  int read_status = OB_SUCCESS;
  int close_status = OB_SUCCESS;
  int fail_next_at = -1;
  int fail_field = -1;
  bool routine_name_column = false; // Opt-in named column for routine ACL deletion fixtures only.
  bool core_value_column = false; // Opt-in core-table scalar, stored in Row::dependency.
  int reads = 0, closes = 0, starts = 0, ends = 0, writes = 0;
  std::string sql;
  std::vector<std::string> queries, written;
  std::vector<bool> commits;
  std::function<void(ExtensionVersionRows &)> on_read;
  std::function<int64_t(const std::string &)> affected_rows;
  int transaction_status = OB_ERR_UNEXPECTED, write_status = OB_ERR_UNEXPECTED;
  int fail_write_at = -1;
  bool is_started() const override { return active; }
  int start(ObISQLClient *, bool, int32_t) override { ++starts; if (transaction_status == OB_SUCCESS) active = true; return transaction_status; }
  int start(ObISQLClient *, const int64_t &, bool) override { ++starts; if (transaction_status == OB_SUCCESS) active = true; return transaction_status; }
  int end(bool commit) override { ++ends; commits.push_back(commit); if (transaction_status == OB_SUCCESS) active = false; return transaction_status; }
  int write(const char *statement, const int32_t, int64_t &affected) override {
    ++writes; written.emplace_back(statement);
    const int status = writes == fail_write_at ? OB_TIMEOUT : write_status;
    affected = status == OB_SUCCESS ? (affected_rows ? affected_rows(statement) : 1) : 0; return status;
  }
  int read(ReadResult &result, const char *query, const int32_t) override {
    ++reads;
    sql = query;
    queries.push_back(sql);
    if (on_read) on_read(*this);
    if (read_status != OB_SUCCESS) return read_status;
    Handler *handler = nullptr;
    return result.create_handler(handler, *this);
  }
private:
  class Result final : public sqlclient::ObMySQLResult {
  public:
    explicit Result(ExtensionVersionRows &fixture) : fixture_(fixture) {}
    int64_t get_column_count() const override {
      if (fixture_.rows.empty()) return 4;
      const auto &row = fixture_.rows.front();
      if (row.integers.empty() && row.strings.empty()) return 4;
      const int64_t integers = row.integers.empty() ? 0 : row.integers.rbegin()->first + 1;
      const int64_t strings = row.strings.empty() ? 0 : row.strings.rbegin()->first + 1;
      return integers > strings ? integers : strings;
    }
    int close() override { ++fixture_.closes; return fixture_.close_status; }
    int next() override {
      ++index_;
      if (index_ == fixture_.fail_next_at) return OB_TIMEOUT;
      return index_ < static_cast<int64_t>(fixture_.rows.size()) ? OB_SUCCESS : OB_ITER_END;
    }
    int get_int(int64_t column, int64_t &value) const override {
      if (column == fixture_.fail_field) return OB_ERR_NULL_VALUE;
      const auto &row = fixture_.rows.at(index_);
      if (!row.integers.empty() || !row.strings.empty()) {
        const auto field = row.integers.find(column);
        if (field == row.integers.end()) return OB_ERR_COLUMN_NOT_FOUND;
        value = field->second; return OB_SUCCESS;
      }
      if (column != 0 && column != 1) return OB_ERR_COLUMN_NOT_FOUND;
      value = column == 0 ? fixture_.rows.at(index_).id : fixture_.rows.at(index_).owner;
      return OB_SUCCESS;
    }
    int get_varchar(int64_t column, ObString &value) const override {
      if (column == fixture_.fail_field) return OB_ERR_NULL_VALUE;
      const auto &row = fixture_.rows.at(index_);
      if (!row.integers.empty() || !row.strings.empty()) {
        const auto field = row.strings.find(column);
        if (field == row.strings.end()) return OB_ERR_COLUMN_NOT_FOUND;
        value = ObString(field->second.size(), field->second.data()); return OB_SUCCESS;
      }
      if (column != 0 && column != 2 && column != 3) return OB_ERR_COLUMN_NOT_FOUND;
      const auto &text = column == 0 ? fixture_.rows.at(index_).dependency :
          column == 2 ? fixture_.rows.at(index_).version : fixture_.rows.at(index_).module;
      value = ObString(text.size(), text.data());
      return OB_SUCCESS;
    }
    // Unexpected getters are errors; the fixture does not invent other data.
#define UNUSED_GETTER(method, type) \
    int method(int64_t, type &) const override { return OB_ERR_UNEXPECTED; } \
    int method(const char *, type &) const override { return OB_ERR_UNEXPECTED; }
    UNUSED_GETTER(get_uint, uint64_t)
    UNUSED_GETTER(get_datetime, int64_t)
    UNUSED_GETTER(get_date, int32_t)
    UNUSED_GETTER(get_time, int64_t)
    UNUSED_GETTER(get_year, uint8_t)
    UNUSED_GETTER(get_bool, bool)
    UNUSED_GETTER(get_float, float)
    UNUSED_GETTER(get_double, double)
    UNUSED_GETTER(get_type, ObObjMeta)
#undef UNUSED_GETTER
    int get_int(const char *, int64_t &) const override { return OB_ERR_UNEXPECTED; }
    int get_varchar(const char *name, ObString &value) const override {
      return name && ((fixture_.routine_name_column && std::string(name) == "routine_name")
          || (fixture_.core_value_column && std::string(name) == "column_value"))
          ? get_varchar(int64_t{0}, value) : OB_ERR_UNEXPECTED;
    }
    int get_timestamp(int64_t, const ObTimeZoneInfo *, int64_t &) const override { return OB_ERR_UNEXPECTED; }
    int get_timestamp(const char *, const ObTimeZoneInfo *, int64_t &) const override { return OB_ERR_UNEXPECTED; }
    int get_obj(int64_t, ObObj &, const ObTimeZoneInfo *, ObIAllocator *) const override { return OB_ERR_UNEXPECTED; }
    int get_obj(const char *, ObObj &) const override { return OB_ERR_UNEXPECTED; }
    int inner_get_number(int64_t, number::ObNumber &, IAllocator &) const override { return OB_ERR_UNEXPECTED; }
    int inner_get_number(const char *, number::ObNumber &, IAllocator &) const override { return OB_ERR_UNEXPECTED; }
  private:
    ExtensionVersionRows &fixture_;
    int64_t index_ = -1;
  };
  class Handler final : public sqlclient::ObISQLResultHandler {
  public:
    explicit Handler(ExtensionVersionRows &fixture) : result_(fixture) {}
    sqlclient::ObMySQLResult *mysql_result() override { return &result_; }
  private:
    Result result_;
  };
};
} }
