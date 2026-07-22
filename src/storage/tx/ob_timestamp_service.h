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
    reset();
  }
  // nano second
  static const int64_t TIMESTAMP_PREALLOCATED_RANGE = 10L * 1000L * 1000L * 1000L;
  static const int64_t TIMESTAMP_RECOVERY_SAFETY_RANGE = 2 * TIMESTAMP_PREALLOCATED_RANGE;
  int get_timestamp(int64_t &gts);
  int64_t get_limited_id() const { return limited_id_; }
  static SCN get_sts_start_scn(const SCN &max_ls_scn)
  { return SCN::plus(max_ls_scn, TIMESTAMP_RECOVERY_SAFETY_RANGE); };
  void get_virtual_info(int64_t &ts_value);
private:
  // last timestamp retrieved from the local timestamp service, in nanoseconds
  int64_t last_gts_;
  // the time of last request, updated periodically, nanosecond 
  int64_t last_request_ts_;
  // the lock of checking the gts service's advancing speed, used in get_timestamp to avoid 
  // concurrent threads all pushing the gts ahead
  int64_t check_gts_speed_lock_;
};

}
}
#endif
