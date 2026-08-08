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

#ifndef OCEANBASE_DATA_PLANE_OB_LOG_SERVICE_HANDLER_H_
#define OCEANBASE_DATA_PLANE_OB_LOG_SERVICE_HANDLER_H_

namespace oceanbase
{
namespace logservice
{
class ObICheckpointSubHandler;
class ObILocalLogHandler;
class ObIReplaySubHandler;
}
namespace data_plane
{

// Type-erased view of a composition-owned module's three log-service roles.
// Storage owns registration but does not need the concrete Observer/Rootserver
// service type behind these handlers.
struct ObLogServiceHandler
{
  ObLogServiceHandler()
      : replay_(nullptr), local_(nullptr), checkpoint_(nullptr)
  {}

  void set(logservice::ObIReplaySubHandler *replay,
           logservice::ObILocalLogHandler *local,
           logservice::ObICheckpointSubHandler *checkpoint)
  {
    replay_ = replay;
    local_ = local;
    checkpoint_ = checkpoint;
  }

  bool is_valid() const
  {
    return nullptr != replay_ && nullptr != local_ && nullptr != checkpoint_;
  }

  logservice::ObIReplaySubHandler *replay_;
  logservice::ObILocalLogHandler *local_;
  logservice::ObICheckpointSubHandler *checkpoint_;
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_OB_LOG_SERVICE_HANDLER_H_
