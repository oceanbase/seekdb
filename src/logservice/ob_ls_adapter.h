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

#ifndef OCEANBASE_LOGSERVICE_OB_LS_ADAPTER_H_
#define OCEANBASE_LOGSERVICE_OB_LS_ADAPTER_H_

#include <stdint.h>
#include "share/ob_ls_id.h"

namespace oceanbase
{
namespace storage
{
class ObLSService;
}
namespace logservice
{
class ObLogReplayTask;
class ObLSAdapter
{
public:
  ObLSAdapter();
  ~ObLSAdapter();
  int init(storage::ObLSService *ls_service_);
  void destroy();
public:
  virtual int replay(ObLogReplayTask *replay_task);
  virtual int wait_append_sync(const share::ObLSID &ls_id);
private:
const int64_t MAX_SINGLE_REPLAY_WARNING_TIME_THRESOLD = 100 * 1000; //100ms
  const int64_t MAX_SINGLE_REPLAY_ERROR_TIME_THRESOLD = 2 * 1000 * 1000; // 2s Single log replay execution time exceeding this value reports error
  const int64_t MAX_SINGLE_RETRY_WARNING_TIME_THRESOLD = 5 * 1000 * 1000; // 5s Single log replay retry exceeds this value reports error
  bool is_inited_;
  storage::ObLSService *ls_service_;
};

} // logservice
} // oceanbase

#endif
