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

#include <memory>
#include "share/schema/ob_priv_type.h"

namespace oceanbase {
namespace common {
class ObMySQLTransaction;
class ObString;
template<class T> class ObIArray;
}
namespace obcall { struct NativeRoutinePrivilegeTarget; }
namespace share { namespace schema {
class ObMultiVersionSchemaService;
class ObSchemaGetterGuard;
class RoutineSchemaOverlay;
class RoutinePrivilegeOverlay;
} }
namespace rootserver {
class NativeRoutineAclVersionReservation;

// Host-only, one-shot GRANT batch. The caller owns serialized user/role/database
// metadata admission, the current guard, and the already started SQL transaction.
// This does not start/end a transaction, auto-create users, publish global caches,
// or authorize unchecked actor/recipient IDs. Any error requires the
// caller to roll back SQL, even though provisional view changes are rolled back
// here. Destroy this object before closing or reusing its borrowed transaction.
class NativeRoutineGrantWriter final
{
public:
  NativeRoutineGrantWriter(share::schema::ObMultiVersionSchemaService &service,
      share::schema::ObSchemaGetterGuard &guard, common::ObMySQLTransaction &transaction)
      : service_(service), guard_(guard), transaction_(transaction) {}
  NativeRoutineGrantWriter(const NativeRoutineGrantWriter &) = delete;
  NativeRoutineGrantWriter &operator=(const NativeRoutineGrantWriter &) = delete;

  // Duplicate recipient IDs are coalesced. Distinct (grantee, grantor) groups
  // receive distinct, monotonically increasing host-reserved schema versions.
  // changed_version is zero on failure or a no-op; on success it is the highest
  // version actually written, for the caller's commit/invalidation journal.
  // Views must be a matched pair and exactly the guard's attached view; they
  // may be omitted only when the guard has no transaction-private overlay.
  // A pure NativeRoutineGrantPlan pins per-key before/after images from the
  // initial locking snapshot. The authorized writer must match those images;
  // divergence aborts the batch without publishing any private-view prefix.
  int grant(const obcall::NativeRoutinePrivilegeTarget &target,
      const common::ObIArray<uint64_t> &grantees, ObPrivSet rights,
      bool grant_option, const common::ObString *sql, int64_t &changed_version,
      std::shared_ptr<share::schema::RoutineSchemaOverlay> view = {},
      std::shared_ptr<share::schema::RoutinePrivilegeOverlay> privileges = {},
      NativeRoutineAclVersionReservation *reservation = nullptr);
private:
  share::schema::ObMultiVersionSchemaService &service_;
  share::schema::ObSchemaGetterGuard &guard_;
  common::ObMySQLTransaction &transaction_;
  bool attempted_ = false;
};
} }
