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

#ifndef OCEANBASE_ROOTSERVER_OB_I_DEBUG_SYNC_LOCAL_RUNTIME_H_
#define OCEANBASE_ROOTSERVER_OB_I_DEBUG_SYNC_LOCAL_RUNTIME_H_

namespace oceanbase
{
namespace obcall
{
struct ObDebugSyncActionArg;
}
namespace rootserver
{

// The server-local receiver used by Rootserver's distributed debug-sync
// adapter. Observer supplies the production implementation.
class ObIDebugSyncLocalRuntime
{
public:
  virtual ~ObIDebugSyncLocalRuntime() = default;

  virtual int set_ds_action(
      const obcall::ObDebugSyncActionArg &arg) = 0;
};

} // namespace rootserver
} // namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_I_DEBUG_SYNC_LOCAL_RUNTIME_H_
