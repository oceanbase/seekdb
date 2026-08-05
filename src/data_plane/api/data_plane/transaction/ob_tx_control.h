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

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_CONTROL_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_CONTROL_H_

#include <stdint.h>
#include "data_plane/transaction/ob_tx_options.h"
#include "data_plane/transaction/ob_tx_seq.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace transaction
{
class ObTxDesc;
class ObTxReadSnapshot;
}
namespace data_plane
{

class ObITransactionService;

// Transaction-owned decision for a requested weak read. Query applies the
// decision without inspecting descriptor participants or isolation state.
enum class ObTxWeakReadPolicy
{
  ALLOW,
  FORCE_STRONG,
  REJECT_ISOLATION,
};

// Semantic statement operations. Query owns the statement choreography; the
// descriptor fields and logical-clock representation remain data-plane details.
ObTxWeakReadPolicy evaluate_tx_weak_read_policy(
    const transaction::ObTxDesc &tx);
void prepare_tx_for_statement(transaction::ObTxDesc &tx);
void initialize_plain_insert_snapshot(
    const transaction::ObTxDesc &tx,
    transaction::ObTxReadSnapshot &snapshot);
// Session lifecycle decisions backed by private descriptor state.
bool tx_owns_local_temporary_tables(
    const transaction::ObTxDesc *tx,
    const common::ObAddr &local_addr);
int allocate_tx_branches(transaction::ObTxDesc &tx,
                         int64_t count,
                         int16_t &first_branch_id);
int prepare_tx_for_autocommit_retry(transaction::ObTxDesc &tx);

enum class ObTxAbortReason
{
  INCOMPLETE_RESULT,
  SESSION_DISCONNECT,
};

// Translate stable query intent at the data-plane boundary. The numeric abort
// cause and its diagnostic name are transaction implementation details.
const char *describe_transaction_abort_error(int error_code);
const char *describe_transaction_abort_reason(ObTxAbortReason reason);
int abort_transaction_for_error(transaction::ObTxDesc &tx, int error_code);
int abort_transaction(transaction::ObTxDesc &tx, ObTxAbortReason reason);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_CONTROL_H_
