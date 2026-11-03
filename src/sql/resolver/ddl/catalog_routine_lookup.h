/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_CATALOG_ROUTINE_LOOKUP_H_
#define SEEKDB_SQL_CATALOG_ROUTINE_LOOKUP_H_

#include "lib/charset/ob_charset.h"
#include "share/plugin/catalog_builder.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase { namespace sql {
// Use the caller's view and ordinary SHOW visibility in both installation and
// query callbacks. ID=0 is absence, not permission denial. No new snapshot,
// transaction, membership, execution privilege or dependency is created here.
inline int lookup_catalog_routine(ObSQLSessionInfo &session, share::schema::ObSchemaGetterGuard &view,
    uint64_t database_id, share::plugin::CatalogRoutineKind kind, const std::string &name, uint64_t &object_id)
{
  using namespace common;
  using namespace share::schema;
  using share::plugin::CatalogRoutineKind;
  object_id = 0;
  if (database_id == 0 || database_id == OB_INVALID_ID || name.empty() ||
      name.size() > OB_MAX_ROUTINE_NAME_BINARY_LENGTH || name.find('\0') != std::string::npos ||
      (kind != CatalogRoutineKind::FUNCTION && kind != CatalogRoutineKind::PROCEDURE)) return OB_INVALID_ARGUMENT;
  int64_t valid_bytes = 0;
  int ret = ObCharset::well_formed_len(CS_TYPE_UTF8MB4_BIN, name.data(), name.size(), valid_bytes);
  if (ret != OB_SUCCESS) return ret;
  if (valid_bytes != static_cast<int64_t>(name.size())) return OB_INVALID_ARGUMENT;
  ObSessionPrivInfo privileges;
  const ObDatabaseSchema *database = nullptr;
  if (OB_FAIL(session.get_session_priv_info(privileges))) return ret;
  if (OB_FAIL(view.get_database_schema(database_id, database))) return ret;
  if (!database) return OB_ERR_BAD_DATABASE;
  const ObString routine_name(name.size(), name.data());
  const auto type = kind == CatalogRoutineKind::FUNCTION ? ROUTINE_FUNCTION_TYPE : ROUTINE_PROCEDURE_TYPE;
  bool visible = false;
  if (OB_FAIL(view.check_routine_show(privileges, session.get_enable_role_array(),
      database->get_database_name_str(), routine_name, visible, type))) return ret;
  if (!visible) return OB_ERR_NO_PRIVILEGE;
  const ObRoutineInfo *routine = nullptr;
  ret = kind == CatalogRoutineKind::FUNCTION ? view.get_standalone_function_info(database_id, routine_name, routine) :
      view.get_standalone_procedure_info(database_id, routine_name, routine);
  if (ret == OB_SUCCESS && routine) {
    const auto id = routine->get_routine_id();
    if (id == 0 || id > INT64_MAX || routine->get_database_id() != database_id ||
        routine->get_routine_type() != type) return OB_ERR_UNEXPECTED;
    object_id = id;
  }
  return ret;
}
} }
#endif
