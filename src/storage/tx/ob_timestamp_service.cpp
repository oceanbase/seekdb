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

#include "ob_timestamp_service.h"

namespace oceanbase
{

using namespace oceanbase::share;
namespace transaction
{

int ObTimestampService::init()
{
  const ObAddr &self = GCTX.self_addr();
  self_ = self;
  service_type_ = ServiceType::TimestampService;
  pre_allocated_range_ = TIMESTAMP_PREALLOCATED_RANGE;
  ATOMIC_STORE(&last_gts_, 0);
  ATOMIC_STORE(&last_request_ts_, 0);
  ATOMIC_STORE(&check_gts_speed_lock_, 0);
  return OB_SUCCESS;
}

int ObTimestampService::mtl_init(ObTimestampService *&timestamp_service)
{
  int ret = OB_SUCCESS;
  ret = timestamp_service->init();
  return ret;
}

// The interface for getting gts timestamp, actually a wrapper of ObIDService::get_number.
//
// The timestamp service uses the machine clock as its base. Persisted preallocation can make the
// allocated range temporarily larger than the machine clock, so the service may need to slow down
// and wait for the machine clock. But we don't want the service to advance too slowly (when request
// rate is low), since the observer may wait too long before the gts timestamp crosses log SCN. 
// So we periodically check the gts service's advancing speed, and if it's far slower than the 
// machine clock, we manually push the gts ahead. 
int ObTimestampService::get_timestamp(int64_t &gts)
{
  int ret = OB_SUCCESS;
  int64_t unused_id;
  // 100ms
  const int64_t CHECK_INTERVAL = 100000000;
  const int64_t current_time = ObClockGenerator::getClock() * 1000;
  int64_t last_request_ts = ATOMIC_LOAD(&last_request_ts_);
  int64_t time_delta = current_time - last_request_ts;

  ret = get_number(1, current_time, gts, unused_id);

  if (OB_SUCC(ret)) {
    if ((last_request_ts == 0 || time_delta < 0) && ATOMIC_BCAS(&check_gts_speed_lock_, 0, 1)) {
      last_request_ts = ATOMIC_LOAD(&last_request_ts_);
      time_delta = current_time - last_request_ts;
      // before, we only do a fast check, and we should check again after we get the lock
      if (last_request_ts == 0 || time_delta < 0) {
        ATOMIC_STORE(&last_request_ts_, current_time);
        ATOMIC_STORE(&last_gts_, gts);
      }
      ATOMIC_STORE(&check_gts_speed_lock_, 0);
    } else if (time_delta > CHECK_INTERVAL && ATOMIC_BCAS(&check_gts_speed_lock_, 0, 1)) {
      last_request_ts = ATOMIC_LOAD(&last_request_ts_);
      time_delta = current_time - last_request_ts;
      // before, we only do a fast check, and we should check again after we get the lock
      if (time_delta > CHECK_INTERVAL) {
        const int64_t last_gts = ATOMIC_LOAD(&last_gts_);
        const int64_t gts_delta = gts - last_gts;
        const int64_t compensation_threshold = time_delta / 2;
        const int64_t compensation_value = time_delta / 10;
        // if the gts service advanced too slowly, then we add it up with `compensation_value`
        if (time_delta - gts_delta > compensation_threshold) {
          ret = get_number(compensation_value, current_time, gts, unused_id);
          TRANS_LOG(WARN, "the gts service advanced too slowly", K(ret), K(current_time),
              K(last_request_ts), K(time_delta), K(last_gts), K(gts), K(gts_delta),
              K(compensation_value));
        }
        if (OB_SUCC(ret)) {
          ATOMIC_STORE(&last_request_ts_, current_time);
          ATOMIC_STORE(&last_gts_, gts);
        }
        TRANS_LOG(DEBUG, "check the gts service advancing speed", K(ret), K(current_time),
            K(last_request_ts), K(time_delta), K(last_gts), K(gts), K(gts_delta),
            K(compensation_value));
      }
      ATOMIC_STORE(&check_gts_speed_lock_, 0);
    }
  }
  
  return ret;
}

void ObTimestampService::get_virtual_info(int64_t &ts_value)
{
  ts_value = last_id_;
  TRANS_LOG(INFO, "gts get virtual info", K_(last_id), K(ts_value));
}

}
}
