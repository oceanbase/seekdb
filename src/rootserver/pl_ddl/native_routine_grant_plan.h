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
namespace share { namespace schema { class ObRoutineInfo; class ObObjPriv; } }
namespace rootserver {
struct NativeRoutineGrantRequest {
  uint64_t grantor_, grantee_;
  ObPrivSet rights_;
  bool grant_option_ = false;
  TO_STRING_KV(K_(grantor), K_(grantee), K_(rights), K_(grant_option));
};
struct NativeRoutineGrantDelta {
  uint64_t grantor_, grantee_;
  share::ObPackedObjPriv before_, after_;
  TO_STRING_KV(K_(grantor), K_(grantee), K_(before), K_(after));
};

// Pure exact-object ACL transformation, not authority or a write receipt.
// The host must select/authorize grantors, validate recipients, pin the object
// and lock its full ACL. UPDATE may instead supply its admitted private ACL.
// Before execution the host must revalidate the plan on the same transaction;
// a plan cannot start/end SQL, allocate versions or publish a private/global view.
class NativeRoutineGrantPlan final {
public:
  // Duplicate requests coalesce; grant options remain per privilege. Output is
  // owned and ordered by (grantee, grantor), including no-op requested groups.
  // Unrequested object/column groups remain unchanged and are not in output.
  // Malformed/duplicate snapshot keys fail closed. Failure clears all output.
  static int build(const share::schema::ObRoutineInfo &expected,
      const common::ObIArray<share::schema::ObObjPriv> &snapshot,
      const common::ObIArray<NativeRoutineGrantRequest> &requests,
      common::ObIArray<NativeRoutineGrantDelta> &output);
};
} }
