/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_DROP_EXTENSION_STMT_H_
#define SEEKDB_SQL_DROP_EXTENSION_STMT_H_
#include "sql/resolver/cmd/ob_cmd_stmt.h"

namespace oceanbase { namespace sql {
class DropExtensionStmt final : public ObCMDStmt
{
public:
  DropExtensionStmt() : ObCMDStmt(stmt::T_DROP_EXTENSION) {}
  bool cause_implicit_commit() const override { return false; }
  const common::ObString &name() const { return name_; }
  const common::ObString &database_name() const { return database_name_; }
  uint64_t database_id() const { return database_id_; }
  bool cascade() const { return cascade_; }
  void set_name(const common::ObString &value) { name_ = value; }
  void set_database(const common::ObString &name, uint64_t id) { database_name_ = name; database_id_ = id; }
  void set_cascade(bool value) { cascade_ = value; }
  TO_STRING_KV(K_(stmt_type), K_(name), K_(database_id), K_(cascade));
private:
  common::ObString name_;
  common::ObString database_name_;
  uint64_t database_id_ = common::OB_INVALID_ID;
  bool cascade_ = false;
  DISALLOW_COPY_AND_ASSIGN(DropExtensionStmt);
};
} }
#endif
