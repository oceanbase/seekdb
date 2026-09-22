/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_ROOTSERVER_ROUTINE_CATALOG_WRITER_H_
#define SEEKDB_ROOTSERVER_ROUTINE_CATALOG_WRITER_H_
#include "rootserver/pl_ddl/ob_pl_ddl_operator.h"

namespace oceanbase { namespace rootserver {
// Host-only, one-operation database effects. Caller supplies the admitted and
// locked current schema view and an already active transaction; this is NOT an
// ACL bypass, resolver, ID authority, plugin ABI or transaction coordinator.
// Reuses normal PL schema/dependency/error/automatic-grant behavior on precisely
// the supplied transaction. No start/end/commit/rollback or schema publication.
// All reservations/results must be destroyed before closing borrowed transport.
class RoutineCatalogWriter final
{
public:
  RoutineCatalogWriter(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLProxy &proxy, share::schema::ObSchemaGetterGuard &guard,
      common::ObMySQLTransaction &transaction, bool transaction_privileges)
      : service_(service), proxy_(proxy), guard_(guard), transaction_(transaction),
        transaction_privileges_(transaction_privileges) {}
  RoutineCatalogWriter(const RoutineCatalogWriter &) = delete;
  RoutineCatalogWriter &operator=(const RoutineCatalogWriter &) = delete;
  // old==nullptr is CREATE; otherwise the ordinary replacement path (including
  // resolved MySQL ALTER attributes). Host must preserve existing dependencies
  // for attribute-only ALTER; an empty array does not mean "unchanged".
  int create(share::schema::ObRoutineInfo &routine, const share::schema::ObRoutineInfo *old,
      share::schema::ObErrorInfo &errors, common::ObIArray<share::schema::ObDependencyInfo> &dependencies,
      const common::ObString *sql, RoutineIdReservation *identity = nullptr,
      RoutineVersionReservation *version = nullptr);
  // Recompile/error-status-only branch, not MySQL attribute replacement.
  int alter(const share::schema::ObRoutineInfo &routine, share::schema::ObErrorInfo &errors,
      const common::ObString *sql);
  // Required host-owned invalidation sink. A caller transaction MUST journal
  // it for commit, never issue legacy cache-flush SQL on a second connection.
  int drop(const share::schema::ObRoutineInfo &routine, share::schema::ObErrorInfo &errors,
      const common::ObString *sql, IRoutineCacheInvalidation &invalidation,
      RoutineVersionReservation *version = nullptr);
private:
  int begin();
  share::schema::ObMultiVersionSchemaService &service_;
  common::ObMySQLProxy &proxy_;
  share::schema::ObSchemaGetterGuard &guard_;
  common::ObMySQLTransaction &transaction_;
  // true for Extension/caller writes: ACL reads come from this transaction,
  // not the final/provisional overlay. Legacy owned Root DDL keeps false.
  const bool transaction_privileges_;
  bool attempted_ = false;
};
} }
#endif
