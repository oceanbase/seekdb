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

#ifndef OCEANBASE_QUERY_OB_I_ACTIVE_SNAPSHOT_SERVICE_H_
#define OCEANBASE_QUERY_OB_I_ACTIVE_SNAPSHOT_SERVICE_H_

#include "share/scn.h"

namespace oceanbase
{
namespace query
{

// Query-owned view of active sessions needed by data-plane retention logic.
class ObIActiveSnapshotService
{
public:
  virtual ~ObIActiveSnapshotService() = default;
  virtual int get_min_active_snapshot_version(share::SCN &snapshot_version) = 0;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_OB_I_ACTIVE_SNAPSHOT_SERVICE_H_
