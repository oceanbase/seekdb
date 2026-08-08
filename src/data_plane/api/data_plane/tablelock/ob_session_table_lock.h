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

#ifndef OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_SESSION_TABLE_LOCK_H_
#define OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_SESSION_TABLE_LOCK_H_

#include <stdint.h>

#include "data_plane/tablelock/ob_table_lock_target.h"
#include "lib/string/ob_string.h"
#include "share/ob_lock_metadata_session.h"

namespace oceanbase
{
namespace common
{
class ObISQLClient;
}
namespace transaction
{
class ObTxDesc;
struct ObTxParam;
}
namespace data_plane
{

struct ObSessionLockOwner
{
  ObSessionLockOwner(uint32_t session_id = 0,
                     uint64_t session_create_ts = 0)
    : session_id_(session_id),
      session_create_ts_(session_create_ts)
  {}

  uint32_t session_id_;
  uint64_t session_create_ts_;
};

struct ObPersistedLockOwner
{
  ObPersistedLockOwner(uint8_t owner_type = 0, int64_t owner_id = 0)
    : owner_type_(owner_type), owner_id_(owner_id)
  {}

  uint8_t owner_type_;
  int64_t owner_id_;
};

enum class ObSessionLockScope : uint8_t
{
  NAMED_LOCK,
  TABLE_LOCK,
  ALL_LOCKS,
};

// High-leverage session-lock operations.  Request construction, lock-record
// persistence, and table-lock service details stay in the data plane; query
// supplies only a transaction, an opaque inner-SQL adapter, and user intent.
int acquire_named_lock(share::ObILockMetadataSession &session_io,
                       transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param,
                       const ObSessionLockOwner &owner,
                       uint64_t lock_id,
                       int64_t timeout_us);

int acquire_mysql_table_lock(share::ObILockMetadataSession &session_io,
                             transaction::ObTxDesc &tx,
                             const transaction::ObTxParam &tx_param,
                             const ObSessionLockOwner &owner,
                             const ObTableLockTarget &target,
                             int64_t timeout_us);

// release_count follows MySQL named-lock semantics: -1 means the lock does
// not exist, 0 means it exists but belongs to another owner, and a positive
// value is the number of released records.
int release_named_lock(share::ObILockMetadataSession &session_io,
                       transaction::ObTxDesc &tx,
                       const transaction::ObTxParam &tx_param,
                       const ObSessionLockOwner &owner,
                       uint64_t lock_id,
                       int64_t &release_count);

int release_session_locks(share::ObILockMetadataSession &session_io,
                          transaction::ObTxDesc &tx,
                          const transaction::ObTxParam &tx_param,
                          const ObSessionLockOwner &owner,
                          ObSessionLockScope scope,
                          int64_t &release_count);

int release_persisted_locks(share::ObILockMetadataSession &session_io,
                            transaction::ObTxDesc &tx,
                            const transaction::ObTxParam &tx_param,
                            const ObPersistedLockOwner &owner,
                            ObSessionLockScope scope,
                            int64_t &release_count);

int session_has_locks(share::ObILockMetadataSession &session_io,
                      const ObSessionLockOwner &owner,
                      bool &has_locks);

int named_lock_exists(share::ObILockMetadataSession &session_io,
                      uint64_t lock_id,
                      bool &exists);

int get_named_lock_owner_session(common::ObISQLClient &sql_client,
                                 uint64_t lock_id,
                                 uint32_t &session_id);

int session_lock_owners_equal(const ObSessionLockOwner &left,
                              const ObSessionLockOwner &right,
                              bool &equal);

int persist_session_lock_owner(const ObSessionLockOwner &owner,
                               ObPersistedLockOwner &persisted);

int get_persisted_lock_owner_session(const ObPersistedLockOwner &owner,
                                     uint32_t &session_id);

int generate_named_lock_identity(const common::ObString &lock_name,
                                 uint64_t min_lock_id,
                                 uint64_t max_lock_id,
                                 uint64_t &lock_id,
                                 uint64_t &name_hash);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_SESSION_TABLE_LOCK_H_
