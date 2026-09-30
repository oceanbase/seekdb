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
#include "share/ob_priv_common.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase {
namespace common { class ObMySQLTransaction; template<class T> class ObIArray; }
namespace obcall { struct NativeRoutinePrivilegeTarget; }
namespace share { namespace schema { class ObMultiVersionSchemaService; class ObRoutineInfo; } }
namespace rootserver {
struct NativeRoutineAclChange {
  uint64_t grantor_, grantee_;
  share::ObPackedObjPriv before_, after_;
  TO_STRING_KV(K_(grantor), K_(grantee), K_(before), K_(after));
};

// Host-only one-shot reservation for an admitted ACL plan. Not an authority
// token, wire field or durable transaction ID. The owner must destroy it at
// transaction end and never restart/reuse the transaction object while held.
// Real versions are allocated during planning, never invented at execution.
class NativeRoutineAclVersionReservation final {
public:
  enum class Kind { GRANT, REVOKE };
  NativeRoutineAclVersionReservation();
  ~NativeRoutineAclVersionReservation();
  NativeRoutineAclVersionReservation(NativeRoutineAclVersionReservation &&) noexcept;
  NativeRoutineAclVersionReservation &operator=(NativeRoutineAclVersionReservation &&) noexcept;
  NativeRoutineAclVersionReservation(const NativeRoutineAclVersionReservation &) = delete;
  NativeRoutineAclVersionReservation &operator=(const NativeRoutineAclVersionReservation &) = delete;
  // Changes must be unique and sorted by (grantee, grantor), including no-ops.
  // Pins service, SQL service, transaction, full target/actor/roles and exact
  // before/after images. Allocation failure leaves no partially usable token.
  static int reserve(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const obcall::NativeRoutinePrivilegeTarget &target,
      Kind kind, const common::ObIArray<NativeRoutineAclChange> &changes,
      NativeRoutineAclVersionReservation &output);
  int64_t count() const;
  int64_t version_at(int64_t index) const;
  // Implicit native CREATE owner grant: empty ACL -> EXECUTE/ALTER, without
  // grant options. Call only after CREATE admission with automatic grants on.
  // Uses the same one-shot binding, not authority to create/grant the object.
  static int reserve_create_owner(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      NativeRoutineAclVersionReservation &output);
  int take_create_owner(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const share::schema::ObRoutineInfo &routine,
      int64_t &version);
  // Every take attempt consumes the token. Returned versions match the exact
  // revalidated plan or output is empty; no new versions are allocated here.
  int take(share::schema::ObMultiVersionSchemaService &service,
      common::ObMySQLTransaction &transaction, const obcall::NativeRoutinePrivilegeTarget &target,
      Kind kind, const common::ObIArray<NativeRoutineAclChange> &changes,
      common::ObIArray<int64_t> &versions);
private:
  struct Identity;
  std::unique_ptr<Identity> identity_;
};
} }
