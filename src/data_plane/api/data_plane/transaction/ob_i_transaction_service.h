/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_I_TRANSACTION_SERVICE_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_I_TRANSACTION_SERVICE_H_

#include <cstdint>
#include "data_plane/transaction/ob_tx_options.h"
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace transaction
{
class ObITxCallback;
class ObTxDesc;
class ObTxExecResult;
class ObTxReadSnapshot;
class ObTxSEQ;
class ObXATransID;
}
namespace data_plane
{

// Transitional query-facing transaction boundary. It is intentionally broad:
// first make every query/transaction interaction explicit here, then reduce
// the surface without allowing implementation headers to leak back into SQL.
class ObITransactionService
{
public:
  virtual ~ObITransactionService() {}

  virtual int acquire_tx(transaction::ObTxDesc *&tx,
                         uint32_t session_id = 0) = 0;
  virtual int acquire_tx(const char *buf,
                         int64_t len,
                         int64_t &pos,
                         transaction::ObTxDesc *&tx) = 0;
  virtual int start_tx(transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param) = 0;
  virtual int abort_tx(transaction::ObTxDesc &tx, int cause) = 0;
  virtual int rollback_tx(transaction::ObTxDesc &tx) = 0;
  virtual int commit_tx(transaction::ObTxDesc &tx,
                        int64_t expire_ts) = 0;
  virtual int submit_commit_tx(transaction::ObTxDesc &tx,
                               int64_t expire_ts,
                               transaction::ObITxCallback &callback) = 0;
  virtual int release_tx(transaction::ObTxDesc &tx) = 0;
  virtual int reuse_tx(transaction::ObTxDesc &tx) = 0;
  virtual int interrupt(transaction::ObTxDesc &tx, int cause) = 0;

  virtual int get_read_snapshot(transaction::ObTxDesc &tx,
                                transaction::ObTxIsolationLevel isolation_level,
                                int64_t expire_ts,
                                transaction::ObTxReadSnapshot &snapshot) = 0;
  virtual int get_read_snapshot_version(int64_t expire_ts,
                                        share::SCN &snapshot_version) = 0;
  virtual int get_weak_read_snapshot_version(int64_t max_read_stale_time,
                                             share::SCN &snapshot_version) = 0;
  virtual int register_tx_snapshot_verify(
      transaction::ObTxReadSnapshot &snapshot) = 0;

  virtual int create_implicit_savepoint(transaction::ObTxDesc &tx,
                                        const transaction::ObTxParam &tx_param,
                                        transaction::ObTxSEQ &savepoint,
                                        bool release = false) = 0;
  virtual int create_branch_savepoint(transaction::ObTxDesc &tx,
                                      int16_t branch,
                                      transaction::ObTxSEQ &savepoint) = 0;
  virtual int create_in_txn_implicit_savepoint(transaction::ObTxDesc &tx,
                                               transaction::ObTxSEQ &savepoint) = 0;
  virtual int create_explicit_savepoint(transaction::ObTxDesc &tx,
                                        const common::ObString &savepoint) = 0;
  virtual int rollback_to_implicit_savepoint(
      transaction::ObTxDesc &tx,
      transaction::ObTxSEQ savepoint,
      int64_t expire_ts,
      bool touched_storage,
      transaction::ObTxCleanPolicy clean_policy = transaction::ObTxCleanPolicy::FAST_ROLLBACK) = 0;
  virtual int rollback_to_explicit_savepoint(transaction::ObTxDesc &tx,
                                             const common::ObString &savepoint,
                                             int64_t expire_ts) = 0;
  virtual int release_explicit_savepoint(transaction::ObTxDesc &tx,
                                         const common::ObString &savepoint) = 0;
  virtual int create_stash_savepoint(transaction::ObTxDesc &tx,
                                     const common::ObString &name) = 0;

  virtual int merge_tx_state(transaction::ObTxDesc &to,
                             const transaction::ObTxDesc &from) = 0;
  virtual int get_tx_exec_result(transaction::ObTxDesc &tx,
                                 transaction::ObTxExecResult &exec_info) = 0;
  virtual int add_tx_exec_result(transaction::ObTxDesc &tx,
                                 const transaction::ObTxExecResult &exec_info) = 0;
  virtual int collect_tx_exec_result(transaction::ObTxDesc &tx,
                                     transaction::ObTxExecResult &result) = 0;
  virtual bool can_elr() const = 0;
  TO_STRING_EMPTY();
};

ObITransactionService *query_transaction_service();
void force_release_tx_when_tenant_gone(transaction::ObTxDesc &tx);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_I_TRANSACTION_SERVICE_H_
