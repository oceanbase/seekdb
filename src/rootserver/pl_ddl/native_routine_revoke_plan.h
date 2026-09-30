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
#include "share/schema/ob_priv_type.h"
#include "share/ob_priv_common.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase {
namespace common { template<class T> class ObIArray; }
namespace share { namespace schema { class ObObjPriv; class ObRoutineInfo; class ObSchemaGetterGuard; } }
namespace rootserver {

// Host-established intrinsic grant options, independent of this object's ACL.
// The host derives them from current metadata under serialized admission. They
// are not user/role session privilege caches or an externally supplied token.
struct NativeRoutineGrantRoot {
  uint64_t principal_;
  ObPrivSet rights_;
  TO_STRING_KV(K_(principal), K_(rights));
};
struct NativeRoutineRevokeRequest {
  uint64_t grantor_, grantee_;
  ObPrivSet rights_;
  bool grant_option_only_ = false;
  TO_STRING_KV(K_(grantor), K_(grantee), K_(rights), K_(grant_option_only));
};
struct NativeRoutineRevokeDelta {
  uint64_t grantor_, grantee_;
  share::ObPackedObjPriv before_, after_;
  TO_STRING_KV(K_(grantor), K_(grantee), K_(before), K_(after));
};

// Pure dependency planner, NOT authorization or a catalog writer. The host
// must lock/read the complete exact-object ACL, validate selected grantors and
// roots against current metadata, reserve versions, and apply the whole plan
// on that SAME transaction. No SQL, version allocation or view publication here.
// Only EXECUTE/ALTER function-level rights participate; other column groups
// cannot supply function grant options and are left unchanged.
class NativeRoutineRevokePlan final
{
public:
  enum class Behavior { RESTRICT, CASCADE };
  // Ownership always supplies a root, even for an empty ACL. Explicit current
  // principal/database right+GRANT privileges are additional SeekDB roots;
  // SUPER instead acts as owner and is not a separately persisted grant root.
  // Role rights are not relabeled as a member's rights. The host must hold
  // metadata admission and the object lock;
  // this neither locks metadata nor authorizes the requested REVOKE itself.
  static int collect_roots(share::schema::ObSchemaGetterGuard &guard,
      const share::schema::ObRoutineInfo &expected,
      const common::ObIArray<share::schema::ObObjPriv> &snapshot,
      common::ObIArray<NativeRoutineGrantRoot> &output);
  // RESTRICT returns OB_OP_NOT_ALLOW if any non-direct grant must be removed.
  // Unrooted preexisting ACLs fail closed; cycles cannot justify themselves.
  // Results are owned, sorted by (grantee, grantor), and cleared on ANY failure.
  static int build(const share::schema::ObRoutineInfo &expected,
      const common::ObIArray<share::schema::ObObjPriv> &snapshot,
      const common::ObIArray<NativeRoutineGrantRoot> &roots,
      const common::ObIArray<NativeRoutineRevokeRequest> &requests,
      Behavior behavior, common::ObIArray<NativeRoutineRevokeDelta> &output);
};
} }
