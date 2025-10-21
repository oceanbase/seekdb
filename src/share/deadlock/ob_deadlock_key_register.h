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

#ifdef NEED_DEFINE
#include "share/deadlock/test/test_key.h"// module unittest
#include "storage/tx/ob_trans_define.h"
#include "storage/tablelock/ob_table_lock_deadlock.h"
#endif

// user's key forward declaration
// for template specialization
#ifdef NEED_DECLARATION
namespace oceanbase
{
namespace unittest
{
  class ObDeadLockTestIntKey;
  class ObDeadLockTestDoubleKey;
}// namespace unittest
namespace transaction
{
  class ObTransID;
namespace tablelock
{
  class ObTransLockPartID;
  class ObTransLockPartBlockCallBack;
} // namespace tablelock

}// namespace transaction
}
#endif

// user choose a unique ID for his key
// for runtime reflection
#ifdef NEED_REGISTER
#define REGISTER(T, ID) USER_REGISTER(T, ID)
REGISTER(oceanbase::transaction::ObTransID, 1)
REGISTER(oceanbase::unittest::ObDeadLockTestIntKey, 65535)
REGISTER(oceanbase::unittest::ObDeadLockTestDoubleKey, 65536)
REGISTER(oceanbase::transaction::tablelock::ObTransLockPartID, 65537)
#undef REGISTER
#endif
