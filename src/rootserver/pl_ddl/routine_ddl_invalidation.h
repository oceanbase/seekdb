/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once
#include "share/schema/routine_catalog_transaction.h"
#include "data_plane/transaction/ob_tx_seq.h"
#include "lib/ob_errno.h"

namespace oceanbase { namespace rootserver {
// One Root-owned DDL transaction, without SQL savepoints. Scope/barrier 1 are
// private journal labels, NOT physical transaction IDs or fabricated SQL
// versions. The host supplies the real captured epoch, operation and end-sign
// versions. This owns no SQL connection and confers no DROP/commit authority.
class RoutineDdlInvalidation final {
public:
  RoutineDdlInvalidation() : journal_(1) {}
  int record(uint64_t database, uint64_t routine, int64_t version, int64_t epoch) {
    using namespace common;
    if (!journal_.valid()) return OB_ALLOCATE_MEMORY_FAILED;
    if (epoch <= 0 || version <= 0) return OB_INVALID_ARGUMENT;
    int ret = OB_SUCCESS;
    if (epoch_ == 0) {
      ret = journal_.admit_ddl(1, transaction::ObTxSEQ(1, 0), epoch);
      if (ret == OB_SUCCESS) epoch_ = epoch;
    } else if (epoch_ != epoch) return OB_STATE_NOT_MATCH;
    if (ret == OB_SUCCESS) ret = journal_.record_schema_version(1, transaction::ObTxSEQ(1, 0), version);
    if (ret == OB_SUCCESS) ret = journal_.record_invalidation(1, transaction::ObTxSEQ(1, 0), database, routine);
    return ret;
  }
  // After successful normal Root commit preparation, but BEFORE data COMMIT.
  // Queue capacity is reserved while rollback is still possible. No callback
  // may execute SQL or publish schema. A failed preparation requires abort.
  template<class Reserve>
  int prepare(int64_t last_operation, int64_t end_sign, Reserve &&reserve) {
    using namespace common;
    int ret = journal_.record_schema_version(1, transaction::ObTxSEQ(1, 0), last_operation);
    uint64_t version = 0, operations = 0;
    if (ret == OB_SUCCESS) ret = journal_.begin_prepare(1, version, operations);
    if (ret == OB_SUCCESS) ret = reserve(journal_, uint64_t{1});
    if (ret == OB_SUCCESS) ret = journal_.record_end_sign(1, end_sign);
    if (ret == OB_SUCCESS) ret = journal_.complete_prepare(1, end_sign, OB_SUCCESS);
    return ret;
  }
  // Only a verified outcome. Unknown transport outcome: destroy this owner;
  // the existing queue conservatively evicts after its schema-version fence,
  // without claiming a commit or publishing schema. Known abort cancels it.
  int finish(bool committed) { return journal_.finish(1, committed); }
private:
  share::schema::RoutineCatalogTransaction journal_;
  int64_t epoch_ = 0;
};
} }
