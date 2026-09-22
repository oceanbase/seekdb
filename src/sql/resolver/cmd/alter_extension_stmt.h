/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_ALTER_EXTENSION_STMT_H_
#define SEEKDB_SQL_ALTER_EXTENSION_STMT_H_
#include "sql/resolver/cmd/ob_cmd_stmt.h"
namespace oceanbase { namespace sql {
class AlterExtensionStmt final : public ObCMDStmt
{
public:
  AlterExtensionStmt() : ObCMDStmt(stmt::T_ALTER_EXTENSION) {}
  bool cause_implicit_commit() const override { return false; }
  const common::ObString &name() const { return name_; }
  const common::ObString &version() const { return version_; }
  const common::ObString &database_name() const { return database_name_; }
  uint64_t database_id() const { return database_id_; }
  void set_name(const common::ObString &value) { name_ = value; }
  void set_version(const common::ObString &value) { version_ = value; }
  void set_database(const common::ObString &name, uint64_t id) { database_name_ = name; database_id_ = id; }
  TO_STRING_KV(K_(stmt_type), K_(name), K_(version), K_(database_id));
private:
  common::ObString name_, version_, database_name_;
  uint64_t database_id_ = common::OB_INVALID_ID;
  DISALLOW_COPY_AND_ASSIGN(AlterExtensionStmt);
};
} }
#endif
