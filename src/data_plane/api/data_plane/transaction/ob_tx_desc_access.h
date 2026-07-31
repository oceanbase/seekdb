/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_ACCESS_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_ACCESS_H_

#include "share/transaction/ob_tx_id.h"
#include "lib/net/ob_addr.h"
#include "lib/string/ob_string.h"
#include "share/scn.h"

namespace oceanbase
{
namespace transaction
{
class ObTxDesc;
class ObITxCallback;
class ObXATransID;
}
namespace data_plane
{

// Query may retain a borrowed transaction descriptor, but descriptor state and
// layout stay private to the transaction implementation.  These null-safe
// observations expose only the session decisions query needs to make.
bool tx_desc_is_explicit(const transaction::ObTxDesc *desc);
bool tx_desc_is_in_tx(const transaction::ObTxDesc *desc);
bool tx_desc_has_temporary_tables(const transaction::ObTxDesc *desc);
transaction::ObTransID tx_desc_id(const transaction::ObTxDesc *desc);
bool tx_desc_in_tx_for_free_route(transaction::ObTxDesc *desc);
bool tx_desc_is_read_only(const transaction::ObTxDesc *desc);
bool tx_desc_is_committing(transaction::ObTxDesc *desc);
bool tx_desc_in_tx_or_has_extra_state(const transaction::ObTxDesc *desc);
bool tx_desc_is_clean(const transaction::ObTxDesc *desc);
uint32_t tx_desc_session_id(const transaction::ObTxDesc *desc);
int64_t tx_desc_seq_base(const transaction::ObTxDesc *desc);
uint64_t tx_desc_operation_sequence(const transaction::ObTxDesc *desc);
int tx_desc_serialize(const transaction::ObTxDesc *desc,
                      char *buf,
                      int64_t buf_len,
                      int64_t &pos);
int64_t tx_desc_serialize_size(const transaction::ObTxDesc *desc);
share::SCN tx_desc_snapshot_version(transaction::ObTxDesc *desc);
bool tx_desc_uses_rr_or_serializable(transaction::ObTxDesc *desc);
bool tx_desc_uses_read_committed(transaction::ObTxDesc *desc);
common::ObAddr tx_desc_scheduler(const transaction::ObTxDesc *desc);
int64_t tx_desc_active_timestamp(const transaction::ObTxDesc *desc);
bool tx_desc_contains_savepoint(transaction::ObTxDesc *desc,
                                const common::ObString &savepoint);
bool tx_desc_is_ended(transaction::ObTxDesc *desc);
bool tx_desc_is_timed_out(transaction::ObTxDesc *desc);
void dump_tx_desc_trace(transaction::ObTxDesc *desc);

enum class ObTxCommitTimeoutState
{
  NONE,
  TRANSACTION,
  STATEMENT,
};

ObTxCommitTimeoutState cancel_timed_out_tx_commit(
    transaction::ObTxDesc *desc,
    transaction::ObITxCallback *&callback);

// Keeps the transaction descriptor opaque while preserving detailed logging
// for query-side structures that only retain a borrowed descriptor pointer.
class ObTxDescLogView
{
public:
  explicit ObTxDescLogView(const transaction::ObTxDesc *desc) : desc_(desc) {}
  int64_t to_string(char *buf, const int64_t buf_len) const;

private:
  const transaction::ObTxDesc *desc_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_ACCESS_H_
