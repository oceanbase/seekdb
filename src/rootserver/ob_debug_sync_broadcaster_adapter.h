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

#ifndef OCEANBASE_ROOTSERVER_OB_DEBUG_SYNC_BROADCASTER_ADAPTER_H_
#define OCEANBASE_ROOTSERVER_OB_DEBUG_SYNC_BROADCASTER_ADAPTER_H_

#include "share/ob_i_debug_sync_broadcaster.h"

namespace oceanbase
{
namespace rootserver
{

class ObIDebugSyncLocalRuntime;

class ObDebugSyncBroadcasterAdapter final
    : public common::ObIDebugSyncBroadcaster
{
public:
  explicit ObDebugSyncBroadcasterAdapter(
      ObIDebugSyncLocalRuntime &local_runtime)
      : local_runtime_(local_runtime)
  {}

  int broadcast_debug_sync_action(
      bool reset,
      bool clear,
      const common::ObDebugSyncAction &action) override;

private:
  ObIDebugSyncLocalRuntime &local_runtime_;
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_DEBUG_SYNC_BROADCASTER_ADAPTER_H_
