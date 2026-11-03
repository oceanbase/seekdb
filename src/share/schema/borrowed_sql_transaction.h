/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SCHEMA_BORROWED_SQL_TRANSACTION_H_
#define SEEKDB_SCHEMA_BORROWED_SQL_TRANSACTION_H_
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "share/schema/catalog_operation_recorder.h"
#include <new>

namespace oceanbase { namespace share { namespace schema {
// Host-only validation of a borrowed caller transaction. Must verify its real
// identity, execution context and thread, not just whether a connection exists.
class ICallerTransactionGuard
{
public:
  virtual ~ICallerTransactionGuard() = default;
  virtual int check() const = 0;
};

// Compatibility adapter for existing catalog writers taking ObMySQLTransaction.
// It NEVER sets the base's in_trans_: base destruction therefore cannot commit
// or roll back the caller. Client/guard/results are scoped by the host; all read
// results must close before the client. Not an SDK capability or DDL authority.
class BorrowedSQLTransaction final : public common::ObMySQLTransaction, public ICatalogOperationRecorder
{
public:
  BorrowedSQLTransaction(common::ObISQLClient &client, const ICallerTransactionGuard &guard,
      ICatalogOperationRecorder *recorder = nullptr)
      : ObMySQLTransaction(false), client_(client), guard_(guard), recorder_(recorder) {}
  int check_schema_operation() const override {
    const int ret = status();
    return ret != common::OB_SUCCESS ? ret : recorder_ ? recorder_->check_schema_operation() : common::OB_NOT_SUPPORTED;
  }
  int finish_schema_operation(int64_t version, int sql_result) override {
    if (sql_result != common::OB_SUCCESS) return remember(sql_result);
    return invoke(0, [&] { return recorder_ ? recorder_->finish_schema_operation(version, sql_result) : common::OB_NOT_SUPPORTED; });
  }
  bool is_started() const override { return status() == common::OB_SUCCESS; }
  int status() const { return error_ == common::OB_SUCCESS ? guard_.check() : error_; }
  int start(common::ObISQLClient *, bool = false, int32_t = 0) override
  { return remember(common::OB_NOT_SUPPORTED); }
  int start(common::ObISQLClient *, const int64_t &, bool = false) override
  { return remember(common::OB_NOT_SUPPORTED); }
  int end(bool) override { return remember(common::OB_NOT_SUPPORTED); }
  // Prevent helpers from opening a nested owned transaction on this connection.
  int acquire_connection(common::sqlclient::ObISQLConnectionGuard &connection, int32_t) override
  { connection.reset(); return remember(common::OB_NOT_SUPPORTED); }
  common::sqlclient::ObISQLConnection *get_connection() override
  { return status() == common::OB_SUCCESS ? client_.get_connection() : nullptr; }
  using ObMySQLTransaction::read;
  using ObMySQLTransaction::write;
  int read(ReadResult &result, const char *sql, int32_t group) override
  {
    result.reset();
    const int ret = invoke(group, [&] { return client_.read(result, sql, group); });
    if (ret != common::OB_SUCCESS) result.reset();
    return ret;
  }
  int write(const char *sql, int32_t group, int64_t &affected) override
  {
    affected = 0;
    int64_t rows = 0;
    const int ret = invoke(group, [&] { return client_.write(sql, group, rows); });
    if (ret == common::OB_SUCCESS) affected = rows;
    return ret;
  }
  int escape(const char *from, int64_t length, char *to, int64_t capacity, int64_t &size) override
  {
    size = 0;
    int64_t written = 0;
    const int ret = invoke(0, [&] { return client_.escape(from, length, to, capacity, written); });
    if (ret == common::OB_SUCCESS) size = written;
    return ret;
  }
private:
  int remember(int error) { if (error_ == common::OB_SUCCESS) error_ = error; return error_; }
  template<class F> int invoke(int32_t group, F operation)
  {
    int ret = status();
    if (ret == common::OB_SUCCESS && group != 0) ret = common::OB_NOT_SUPPORTED;
    if (ret == common::OB_SUCCESS) {
      try { ret = operation(); }
      catch (const std::bad_alloc &) { ret = common::OB_ALLOCATE_MEMORY_FAILED; }
      catch (...) { ret = common::OB_ERR_UNEXPECTED; }
    }
    if (ret == common::OB_SUCCESS) ret = guard_.check();
    return remember(ret);
  }
  common::ObISQLClient &client_;
  const ICallerTransactionGuard &guard_;
  ICatalogOperationRecorder *recorder_;
  int error_ = common::OB_SUCCESS;
};
} } }
#endif
