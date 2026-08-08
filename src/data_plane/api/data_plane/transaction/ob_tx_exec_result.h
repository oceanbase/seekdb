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

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_EXEC_RESULT_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_EXEC_RESULT_H_

#include "share/transaction/ob_tx_id.h"
#include "lib/container/ob_iarray.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_unify_serialize.h"

namespace oceanbase
{
namespace transaction
{

struct ObTxWriteState;
class ObTxDesc;
class ObTxExecResultAccess;

// Wire-compatible result of executing a statement in the data plane.  Query
// can merge and forward the result, while participant/conflict bookkeeping
// remains private to the transaction implementation.
class ObTxExecResult
{
  OB_UNIS_VERSION(1);
public:
  ObTxExecResult();
  ~ObTxExecResult();
  void reset();
  void set_incomplete();
  bool is_incomplete() const;
  bool touches_storage() const;
  void mark_touched_storage();
  int set_write_state(const ObTxWriteState &write_state);
  int merge_write_state(const ObTxWriteState &write_state, bool has_write_state);
  bool has_write_state() const;
  int merge_result(const ObTxExecResult &other);
  int assign(const ObTxExecResult &other);
  const common::ObIArray<ObTransID> &get_conflict_txs() const;

  int merge_cflict_txs(const common::ObIArray<ObTransID> &txs);

  DECLARE_TO_STRING;

private:
  friend class ObTxDesc;
  friend class ObTxExecResultAccess;
  void *impl_;
  DISABLE_COPY_ASSIGN(ObTxExecResult);
};

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_EXEC_RESULT_H_
