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

#ifndef OCEANBASE_TRANSACTION_OB_TX_STANDBY_CLEANUP_
#define OCEANBASE_TRANSACTION_OB_TX_STANDBY_CLEANUP_

#include "ob_trans_define.h"

namespace oceanbase
{

namespace transaction
{
class ObTxStandbyCleanupTask : public ObTransTask
{
public:
  ObTxStandbyCleanupTask() : ObTransTask(ObTransRetryTaskType::STANDBY_CLEANUP_TASK)
  {}
  ~ObTxStandbyCleanupTask() { destroy(); }
};

} // transaction
} // oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TX_STANDBY_CLEANUP_
