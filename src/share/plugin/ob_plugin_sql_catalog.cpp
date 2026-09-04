/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#define USING_LOG_PREFIX SHARE

#include "share/plugin/ob_plugin_sql_catalog.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace oceanbase
{
namespace share
{

int ObPluginSqlBinder::append_text(const char *value, int value_len)
{
  if (nullptr == value && value_len > 0) {
    return common::OB_INVALID_ARGUMENT;
  }
  std::string escaped("'");
  for (int i = 0; i < value_len; ++i) {
    const char c = value[i];
    if ('\\' == c || '\'' == c) {
      escaped.push_back('\\');
    }
    escaped.push_back(c);
  }
  escaped.push_back('\'');
  values_.push_back(std::move(escaped));
  return common::OB_SUCCESS;
}

int ObPluginSqlBinder::bind_int(int32_t value)
{
  return bind_int64(value);
}

int ObPluginSqlBinder::bind_int64(int64_t value)
{
  values_.push_back(std::to_string(value));
  return common::OB_SUCCESS;
}

int ObPluginSqlBinder::bind_text(const char *value)
{
  return append_text(value, nullptr == value ? 0 : static_cast<int>(strlen(value)));
}

int ObPluginSqlBinder::bind_text(const char *value, int value_len)
{
  return append_text(value, value_len);
}

int ObPluginSqlBinder::bind_blob(const void *value)
{
  return bind_blob(value, nullptr == value ? 0 : static_cast<int>(strlen(static_cast<const char *>(value))));
}

int ObPluginSqlBinder::bind_blob(const void *value, int value_len)
{
  if (nullptr == value && value_len > 0) {
    return common::OB_INVALID_ARGUMENT;
  }
  static const char hex[] = "0123456789ABCDEF";
  std::string encoded("X'");
  const unsigned char *bytes = static_cast<const unsigned char *>(value);
  for (int i = 0; i < value_len; ++i) {
    encoded.push_back(hex[(bytes[i] >> 4) & 0xF]);
    encoded.push_back(hex[bytes[i] & 0xF]);
  }
  encoded.push_back('\'');
  values_.push_back(std::move(encoded));
  return common::OB_SUCCESS;
}

int64_t ObPluginSqlRowReader::get_int64(int column) const
{
  int64_t value = 0;
  if (nullptr != result_) {
    (void)result_->get_int(column, value);
  }
  return value;
}

int32_t ObPluginSqlRowReader::get_int(int column) const
{
  return static_cast<int32_t>(get_int64(column));
}

const char *ObPluginSqlRowReader::get_text(int column, int *len) const
{
  common::ObString value;
  if (nullptr == result_ || common::OB_SUCCESS != result_->get_varchar(column, value)) {
    if (nullptr != len) *len = 0;
    return nullptr;
  }
  if (nullptr != len) *len = value.length();
  return value.ptr();
}

common::ObString ObPluginSqlRowReader::get_string(int column) const
{
  common::ObString value;
  if (nullptr != result_) {
    (void)result_->get_varchar(column, value);
  }
  return value;
}

const void *ObPluginSqlRowReader::get_blob(int column, int *len) const
{
  return get_text(column, len);
}

ObPluginSqlConnection::ObPluginSqlConnection(common::ObISQLClient *client)
    : client_(client), transaction_() {}

ObPluginSqlConnection::~ObPluginSqlConnection()
{
  if (transaction_.is_started()) {
    (void)transaction_.end(false);
  }
}

bool ObPluginSqlConnection::is_in_transaction() const
{
  bool active = transaction_.is_started();
  if (!active && nullptr != client_) {
    const common::ObMySQLTransaction *external_transaction =
        dynamic_cast<const common::ObMySQLTransaction *>(client_);
    active = nullptr != external_transaction &&
             external_transaction->is_started();
  }
  return active;
}

common::ObISQLClient *ObPluginSqlConnection::executor() const
{
  return transaction_.is_started() ?
      static_cast<common::ObISQLClient *>(const_cast<common::ObMySQLTransaction *>(&transaction_)) : client_;
}

int ObPluginSqlConnection::render_sql(
    const char *sql,
    const std::function<int(ObPluginSqlBinder &)> &binder,
    std::string &rendered) const
{
  if (nullptr == sql || nullptr == client_) return common::OB_INVALID_ARGUMENT;
  ObPluginSqlBinder values;
  int ret = nullptr == binder ? common::OB_SUCCESS : binder(values);
  if (common::OB_SUCCESS != ret) return ret;
  rendered.reserve(strlen(sql) + values.values().size() * 8);
  size_t value_index = 0;
  for (const char *p = sql; *p != '\0'; ++p) {
    if ('?' == *p && value_index < values.values().size()) {
      rendered.append(values.values()[value_index++]);
    } else {
      rendered.push_back(*p);
    }
  }
  return value_index == values.values().size() ? common::OB_SUCCESS : common::OB_INVALID_ARGUMENT;
}

int ObPluginSqlConnection::query(
    const char *sql,
    const std::function<int(ObPluginSqlBinder &)> &binder,
    const std::function<int(ObPluginSqlRowReader &)> &row_processor)
{
  std::string rendered;
  int ret = render_sql(sql, binder, rendered);
  if (common::OB_SUCCESS == ret) {
    common::ObISQLClient::ReadResult result;
    common::ObISQLClient *exec = executor();
    if (nullptr == exec || common::OB_SUCCESS != (ret = exec->read(result, rendered.c_str()))) {
      return ret;
    }
    common::sqlclient::ObMySQLResult *mysql_result = result.get_result();
    if (nullptr == mysql_result) {
      ret = common::OB_ERR_UNEXPECTED;
    } else {
      while (common::OB_SUCCESS == (ret = mysql_result->next())) {
        ObPluginSqlRowReader reader(mysql_result);
        int row_ret = nullptr == row_processor ? common::OB_SUCCESS : row_processor(reader);
        if (common::OB_ITER_END == row_ret) {
          ret = common::OB_SUCCESS;
          break;
        } else if (common::OB_SUCCESS != row_ret) {
          ret = row_ret;
          break;
        }
      }
      if (common::OB_ITER_END == ret) ret = common::OB_SUCCESS;
    }
    (void)result.close();
  }
  return ret;
}

int ObPluginSqlConnection::execute(
    const char *sql,
    const std::function<int(ObPluginSqlBinder &)> &binder,
    int64_t *affected_rows)
{
  std::string rendered;
  int ret = render_sql(sql, binder, rendered);
  if (common::OB_SUCCESS == ret) {
    common::ObISQLClient *exec = executor();
    if (nullptr == exec) {
      ret = common::OB_NOT_INIT;
    } else {
      int64_t rows = 0;
      ret = exec->write(rendered.c_str(), rows);
      if (nullptr != affected_rows) *affected_rows = rows;
    }
  }
  return ret;
}

int ObPluginSqlConnection::begin_transaction()
{
  return transaction_.start(client_);
}

int ObPluginSqlConnection::commit()
{
  return transaction_.is_started() ? transaction_.end(true) : common::OB_SUCCESS;
}

int ObPluginSqlConnection::rollback()
{
  return transaction_.is_started() ? transaction_.end(false) : common::OB_SUCCESS;
}

} // namespace share
} // namespace oceanbase
