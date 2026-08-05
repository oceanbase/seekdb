/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_DEADLOCK_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_DEADLOCK_H_

#include <stdint.h>
#include "share/transaction/ob_tx_id.h"

namespace oceanbase
{
namespace transaction
{
class ObTxDesc;
}
namespace query
{
class ObIDeadlockSessionService;
}
namespace data_plane
{

// Query-owned statement facts needed by the transaction deadlock policy.
// Conflict lists, detector nodes, and unregister paths remain data-plane
// implementation details.
struct ObStatementDeadlockContext
{
  ObStatementDeadlockContext(bool is_inner_session,
                             bool is_rollback,
                             int64_t query_timeout_ts,
                             uint32_t session_id,
                             int exec_error,
                             int64_t retry_count)
    : is_inner_session_(is_inner_session),
      is_rollback_(is_rollback),
      query_timeout_ts_(query_timeout_ts),
      session_id_(session_id),
      exec_error_(exec_error),
      retry_count_(retry_count)
  {}

  bool is_inner_session_;
  bool is_rollback_;
  int64_t query_timeout_ts_;
  uint32_t session_id_;
  int exec_error_;
  int64_t retry_count_;
};

int maintain_deadlock_after_statement(
    transaction::ObTxDesc &tx,
    query::ObIDeadlockSessionService &session_service,
    const ObStatementDeadlockContext &context);
int register_autonomous_transaction_dependency(
    const transaction::ObTransID &suspended_tx_id,
    const transaction::ObTransID &autonomous_tx_id,
    int64_t timeout_us);
void finish_transaction_deadlock(const transaction::ObTransID &tx_id);
void rollback_statement_deadlock(const transaction::ObTransID &tx_id);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_DEADLOCK_H_
