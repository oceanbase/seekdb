/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_ROOTSERVER_CATALOG_COMMIT_PREPARATION_H_
#define SEEKDB_ROOTSERVER_CATALOG_COMMIT_PREPARATION_H_
#include <cstdint>
namespace oceanbase {
namespace common { class ObMySQLTransaction; class ObISQLClient; }
namespace share { namespace schema { class ObMultiVersionSchemaService; } }
namespace rootserver {
// Pre-write admission for caller-owned catalog DDL. No transaction ownership,
// object ACL, epoch promotion, watermark lock or schema publication. Caller
// prepares a root data barrier BEFORE acquiring this transaction-owned lock.
class CatalogDDLAdmission
{
public:
  CatalogDDLAdmission(share::schema::ObMultiVersionSchemaService &schema_service,
      common::ObMySQLTransaction &transaction, common::ObISQLClient &fresh_reader);
  virtual ~CatalogDDLAdmission() = default;
  CatalogDDLAdmission(const CatalogDDLAdmission &) = delete;
  CatalogDDLAdmission &operator=(const CatalogDDLAdmission &) = delete;
  // One attempt; epoch stays zero on failure. Fresh reader must be a host-owned
  // current-read client, NOT the user's potentially old RR transaction snapshot.
  int acquire(int64_t refreshed_schema_version, int64_t absolute_deadline, int64_t &epoch);
  // Shared with legacy Root DDL; identical lock identity and lifetime.
  static int lock_transaction(common::ObMySQLTransaction &transaction, bool parallel, int64_t timeout_us);
protected:
  virtual int capture_epoch(int64_t &epoch);
  virtual int lock(int64_t timeout_us);
  virtual int read_version(int64_t &version);
private:
  share::schema::ObMultiVersionSchemaService &schema_service_;
  common::ObMySQLTransaction &transaction_;
  common::ObISQLClient &fresh_reader_;
  bool attempted_ = false;
};
// Database-specific commit preparation, not a transaction owner. Caller must
// already hold its DDL locks, validate the captured DDL epoch and complete any
// parallel-DDL ordering barrier. On failure it MUST abort, never commit/retry.
// Accepts transaction-owned version state; never reads TSILastOper itself.
// No plugin callback, schema publication, commit or rollback is performed here.
class CatalogCommitPreparation
{
public:
  CatalogCommitPreparation(share::schema::ObMultiVersionSchemaService &schema_service,
      common::ObMySQLTransaction &transaction);
  virtual ~CatalogCommitPreparation() = default;
  CatalogCommitPreparation(const CatalogCommitPreparation &) = delete;
  CatalogCommitPreparation &operator=(const CatalogCommitPreparation &) = delete;
  // Exactly one attempt. Output is zero on failure/no schema changes. When
  // needed, the generated end-sign version must exceed all surviving writes.
  int prepare(int64_t last_schema_version, bool schema_changed, bool need_end_signal,
      int64_t &prepared_schema_version);
  static int register_transaction_signal(common::ObMySQLTransaction &transaction);
protected:
  // Host effect boundaries, not public plugin extension points. Production
  // implementations operate on the SAME transaction passed to the constructor.
  virtual int register_signal();
  virtual int write_watermark(int64_t version);
private:
  share::schema::ObMultiVersionSchemaService &schema_service_;
  common::ObMySQLTransaction &transaction_;
  bool attempted_ = false;
};
} }
#endif
