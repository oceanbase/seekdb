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

#ifndef OCEANBASE_TRANSACTION_OB_TIMESTAMP_SERVICE_
#define OCEANBASE_TRANSACTION_OB_TIMESTAMP_SERVICE_

#include "ob_id_service.h"

namespace oceanbase
{

namespace transaction
{
class ObTimestampService : public ObIDService
{
public:
  ObTimestampService() {}
  ~ObTimestampService() {}
  int init();
  static int server_module_init(ObTimestampService *&timestamp_service);
  int start() { return common::OB_SUCCESS; }
  void stop() {}
  void wait() {}
  void destroy()
  {
    ATOMIC_STORE(&is_ready_, false);
    reset();
  }
  // nano second
  static const int64_t TIMESTAMP_RECOVERY_SAFETY_RANGE = 20L * 1000L * 1000L * 1000L;
  int get_timestamp(int64_t &gts);
  int recover(const share::SCN &max_ls_scn);
  int get_virtual_info(int64_t &ts_value);
  int handle_persist_callback(const bool success,
                              const int64_t persisted_timestamp,
                              const share::SCN log_scn);
  int replay(const void *buffer,
             const int64_t nbytes,
             const palf::LSN &lsn,
             const share::SCN &scn) override;
private:
  int allocate_timestamp_(const int64_t range, const int64_t base_id, int64_t &gts);
  int persist_timestamp_(const int64_t timestamp);
  int submit_timestamp_fence_(const int64_t timestamp);

  // last timestamp retrieved from the local timestamp service, in nanoseconds
  int64_t last_gts_;
  // the time of last request, updated periodically, nanosecond 
  int64_t last_request_ts_;
  // the lock of checking the gts service's advancing speed, used in get_timestamp to avoid 
  // concurrent threads all pushing the gts ahead
  int64_t check_gts_speed_lock_;
  int64_t durable_timestamp_;
  bool is_ready_;
};

}
}
#endif
