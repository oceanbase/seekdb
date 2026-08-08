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

#ifndef OCEANBASE_DATA_PLANE_MEMTABLE_OB_LOCK_WAIT_SERVICE_H_
#define OCEANBASE_DATA_PLANE_MEMTABLE_OB_LOCK_WAIT_SERVICE_H_

namespace oceanbase
{
namespace data_plane
{

class ObILockWaitService
{
public:
  virtual ~ObILockWaitService() = default;
  virtual void reset_current_wait() = 0;
  virtual int repost_lock_wait_request(void *request) = 0;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_MEMTABLE_OB_LOCK_WAIT_SERVICE_H_
