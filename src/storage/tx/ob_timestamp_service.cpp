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
#include "lib/time/ob_time_utility.h"

namespace oceanbase
{

using namespace oceanbase::share;
namespace transaction
{

int ObTimestampService::init()
{
  service_type_ = ServiceType::TimestampService;
  ATOMIC_STORE(&last_id_, ObClockGenerator::getClock() * 1000);
  ATOMIC_STORE(&last_gts_, 0);
  ATOMIC_STORE(&last_request_ts_, 0);
  ATOMIC_STORE(&check_gts_speed_lock_, 0);
  ATOMIC_STORE(&durable_timestamp_, 0);
  ATOMIC_STORE(&is_ready_, false);
  return OB_SUCCESS;
}

int ObTimestampService::server_module_init(ObTimestampService *&timestamp_service)
{
  int ret = OB_SUCCESS;
  ret = timestamp_service->init();
  return ret;
}

int ObTimestampService::allocate_timestamp_(const int64_t range,
                                            const int64_t base_id,
                                            int64_t &gts)
{
  int ret = OB_SUCCESS;
  bool allocated = false;
  if (range <= 0 || base_id < 0) {
    ret = OB_INVALID_ARGUMENT;
  }
  while (OB_SUCC(ret) && !allocated) {
    const int64_t last_id = ATOMIC_LOAD(&last_id_);
    const int64_t candidate = max(last_id, base_id);
    if (candidate > INT64_MAX - range) {
      ret = OB_SIZE_OVERFLOW;
    } else if (ATOMIC_BCAS(&last_id_, last_id, candidate + range)) {
      gts = candidate;
      allocated = true;
    }
  }
  return ret;
}

int ObTimestampService::recover(const SCN &max_ls_scn)
{
  int ret = OB_SUCCESS;
  if (!max_ls_scn.is_valid() || max_ls_scn.is_max()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const uint64_t durable_gts = max_ls_scn.get_val_for_gts();
    if (durable_gts >= static_cast<uint64_t>(INT64_MAX)) {
      ret = OB_SIZE_OVERFLOW;
      TRANS_LOG(ERROR, "durable timestamp is too large to recover", K(ret), K(max_ls_scn),
          K(durable_gts));
    } else {
      const int64_t current_time = ObClockGenerator::getClock() * 1000;
      const int64_t log_floor = static_cast<int64_t>(durable_gts) + TIMESTAMP_RECOVERY_SAFETY_RANGE;
      (void)inc_update(&last_id_, max(current_time, log_floor));
      (void)inc_update(&durable_timestamp_, static_cast<int64_t>(durable_gts));
      ATOMIC_STORE(&is_ready_, true);
      TRANS_LOG(INFO, "timestamp service recovered from durable log frontier",
          K(max_ls_scn), K(log_floor), K_(last_id));
    }
  }
  return ret;
}

// In-memory monotonic allocation. Recovery obtains its floor from the durable
// LS log frontier, so no dedicated timestamp log is submitted here.
int ObTimestampService::get_timestamp(int64_t &gts)
{
  int ret = OB_SUCCESS;
  // 100ms
  const int64_t CHECK_INTERVAL = 100000000;
  const int64_t current_time = ObClockGenerator::getClock() * 1000;
  int64_t last_request_ts = ATOMIC_LOAD(&last_request_ts_);
  int64_t time_delta = current_time - last_request_ts;

  if (!ATOMIC_LOAD(&is_ready_)) {
    ret = OB_EAGAIN;
  } else {
    ret = allocate_timestamp_(1, current_time, gts);
  }

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
          ret = allocate_timestamp_(compensation_value, current_time, gts);
          TRANS_LOG(WARN, "the gts service advanced too slowly", K(ret), K(current_time),
              K(last_request_ts), K(time_delta), K(last_gts), K(gts), K(gts_delta),
              K(compensation_value));
        }
        if (OB_SUCC(ret)) {
          ATOMIC_STORE(&last_request_ts_, current_time);
          ATOMIC_STORE(&last_gts_, gts);
        }
      }
      ATOMIC_STORE(&check_gts_speed_lock_, 0);
    }
  }

  return ret;
}

int ObTimestampService::persist_timestamp_(const int64_t timestamp)
{
  int ret = OB_SUCCESS;
  const int64_t expire_ts = ObTimeUtility::current_time() + 10 * 1000 * 1000;
  while (OB_SUCC(ret) && ATOMIC_LOAD(&durable_timestamp_) < timestamp) {
    const int submit_ret = submit_timestamp_fence_(timestamp);
    if (OB_SUCCESS != submit_ret && OB_EAGAIN != submit_ret) {
      ret = submit_ret;
    } else if (ObTimeUtility::current_time() >= expire_ts) {
      ret = OB_TIMEOUT;
    } else {
      ob_usleep(100);
    }
  }
  return ret;
}

int ObTimestampService::submit_timestamp_fence_(const int64_t timestamp)
{
  int ret = OB_SUCCESS;
  bool locked = false;
  if (timestamp < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(!(locked = rwlock_.try_wrlock()))) {
    ret = OB_EAGAIN;
  } else if (is_logging_) {
    ret = OB_EAGAIN;
  } else if (OB_FAIL(append_id_log_(timestamp, timestamp))) {
    if (OB_EAGAIN != ret && REACH_TIME_INTERVAL(100 * 1000)) {
      TRANS_LOG(WARN, "failed to submit timestamp persistence fence", KR(ret), K(timestamp));
    }
  } else {
    TRANS_LOG(INFO, "submitted timestamp persistence fence", K(timestamp));
  }
  if (locked) {
    rwlock_.unlock();
  }
  return ret;
}

int ObTimestampService::get_virtual_info(int64_t &ts_value)
{
  int ret = OB_SUCCESS;
  if (!ATOMIC_LOAD(&is_ready_)) {
    ret = OB_EAGAIN;
  } else {
    ts_value = ATOMIC_LOAD(&last_id_);
    if (OB_FAIL(persist_timestamp_(ts_value))) {
      TRANS_LOG(WARN, "failed to persist timestamp for virtual table", KR(ret), K(ts_value));
    } else {
      TRANS_LOG(INFO, "persisted gts for virtual table", K(ts_value), K_(durable_timestamp));
    }
  }
  return ret;
}

int ObTimestampService::handle_persist_callback(const bool success,
                                                 const int64_t persisted_timestamp,
                                                 const SCN log_scn)
{
  int ret = OB_SUCCESS;
  WLockGuard guard(rwlock_);
  if (success) {
    (void)inc_update(&durable_timestamp_, persisted_timestamp);
    latest_log_ts_.atomic_set(log_scn);
  }
  is_logging_ = false;
  submit_log_ts_ = OB_INVALID_TIMESTAMP;
  cb_.reset();
  TRANS_LOG(INFO, "timestamp persistence callback", K(success), K(persisted_timestamp),
      K(log_scn), K_(durable_timestamp));
  return ret;
}

int ObTimestampService::replay(const void *buffer,
                               const int64_t nbytes,
                               const palf::LSN &lsn,
                               const SCN &scn)
{
  UNUSEDx(buffer, nbytes, lsn);
  // Recovery uses the durable LS frontier as its timestamp floor. Timestamp
  // log contents are deliberately not restored into the old allocation model.
  return scn.is_valid() ? OB_SUCCESS : OB_INVALID_ARGUMENT;
}

}
}
