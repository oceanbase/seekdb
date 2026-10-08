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
#include "rootserver/pl_ddl/native_routine_revoke_plan.h"

namespace oceanbase {
namespace common { class ObMySQLTransaction; class ObString; }
namespace obcall { struct NativeRoutinePrivilegeTarget; }
namespace share { namespace schema {
class ObMultiVersionSchemaService;
class RoutineSchemaOverlay;
class RoutinePrivilegeOverlay;
} }
namespace rootserver {
class NativeRoutineAclVersionReservation;

// One-shot host REVOKE batch. The host supplies serialized user/role/database
// admission, a current guard and an active transaction. This object builds and
// validates its own dependency plan; it never accepts a caller-supplied plan or
// authority roots. It owns neither SQL transaction completion nor global cache
// publication. On ANY error the caller MUST roll back SQL, even if view cleanup
// has succeeded. Destroy before closing/reusing the borrowed transaction.
class NativeRoutineRevokeWriter final
{
public:
  NativeRoutineRevokeWriter(share::schema::ObMultiVersionSchemaService &service,
      share::schema::ObSchemaGetterGuard &guard, common::ObMySQLTransaction &transaction)
      : service_(service), guard_(guard), transaction_(transaction) {}
  NativeRoutineRevokeWriter(const NativeRoutineRevokeWriter &) = delete;
  NativeRoutineRevokeWriter &operator=(const NativeRoutineRevokeWriter &) = delete;
  // The optional view pair is required iff the guard has that exact attached
  // overlay. Unchanged direct keys also refresh the private view; such no-ops
  // reserve versions but write no operation log. changed_version reports only
  // the maximum actually changed SQL version, and is zero on failure/no-op.
  int revoke(const obcall::NativeRoutinePrivilegeTarget &target,
      const common::ObIArray<uint64_t> &grantees, ObPrivSet rights, bool grant_option_only,
      NativeRoutineRevokePlan::Behavior behavior, const common::ObString *sql,
      int64_t &changed_version, std::shared_ptr<share::schema::RoutineSchemaOverlay> view = {},
      std::shared_ptr<share::schema::RoutinePrivilegeOverlay> privileges = {},
      NativeRoutineAclVersionReservation *reservation = nullptr);
private:
  share::schema::ObMultiVersionSchemaService &service_;
  share::schema::ObSchemaGetterGuard &guard_;
  common::ObMySQLTransaction &transaction_;
  bool attempted_ = false;
};
} }
