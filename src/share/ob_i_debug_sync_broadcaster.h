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

#ifndef OCEANBASE_COMMON_OB_I_DEBUG_SYNC_BROADCASTER_H_
#define OCEANBASE_COMMON_OB_I_DEBUG_SYNC_BROADCASTER_H_

namespace oceanbase
{
namespace common
{

struct ObDebugSyncAction;

// A process-level debug-sync request has already been parsed and validated
// when it crosses this seam. The adapter owns distribution to every server.
class ObIDebugSyncBroadcaster
{
public:
  virtual ~ObIDebugSyncBroadcaster() = default;

  virtual int broadcast_debug_sync_action(
      bool reset,
      bool clear,
      const ObDebugSyncAction &action) = 0;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_COMMON_OB_I_DEBUG_SYNC_BROADCASTER_H_
