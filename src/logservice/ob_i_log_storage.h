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

#ifndef OCEANBASE_LOGSERVICE_OB_I_LOG_STORAGE_H_
#define OCEANBASE_LOGSERVICE_OB_I_LOG_STORAGE_H_

#include <cstdint>

namespace oceanbase
{
namespace logservice
{
class ObLogHandler;
class ObLogReplayTask;

// Storage operations demanded by the log runtime.  The Logservice module owns
// this port; Storage implements it and Observer binds the two lifetimes.
class ObILogStorage
{
public:
  virtual ~ObILogStorage() = default;

  virtual int replay(ObLogReplayTask *replay_task) = 0;
  virtual int wait_append_sync() = 0;
  virtual bool is_replay_pending_log_too_large(int64_t pending_size) = 0;
  virtual int get_log_handler(ObLogHandler *&log_handler) = 0;
  virtual int get_unrecyclable_log_disk_size(int64_t &unrecyclable_log_disk_size) = 0;
};

} // namespace logservice
} // namespace oceanbase

#endif // OCEANBASE_LOGSERVICE_OB_I_LOG_STORAGE_H_
