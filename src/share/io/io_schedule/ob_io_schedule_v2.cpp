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

#define USING_LOG_PREFIX COMMON
#include "ob_io_schedule_v2.h"
#include "share/io/ob_io_manager.h"
#include "share/io/ob_io_struct.h"
namespace oceanbase
{
namespace common
{

int64_t ObIOScheduler::get_qindex(ObIORequest& req)
{
  int ret = OB_SUCCESS;
  int64_t index = -1;
  const ObIOGroupKey grp_key = req.get_group_key();
  if (OB_FAIL(req.io_service_->get_group_index(grp_key, (uint64_t&)index))) {
    if (ret == OB_HASH_NOT_EXIST) {
      ret = OB_SUCCESS;
      if (REACH_TIME_INTERVAL(1 * 1000L * 1000L)) {
        LOG_INFO("get group index failed, but maybe it is ok", K(ret), K(grp_key), K(index));
      }
    } else {
      LOG_WARN("get group index failed", K(ret), K(grp_key), K(index));
    }
    index = -1;
  } else if (INT64_MAX == index) {
    index = -1;
  }
  return index;
}

int ObIOScheduler::schedule_request(ObIORequest &req)
{
  int ret = OB_SUCCESS;
  ObDeviceChannel *device_channel = nullptr;
  ObTimeGuard time_guard("submit_req", 100000); //100ms
  ObIOResult* result = req.io_result_;
  if (OB_ISNULL(result)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("io result is null", K(ret), K(req));
  } else if (OB_UNLIKELY(req.is_canceled())) {
    ret = OB_CANCELED;
  } else if (OB_FAIL(req.prepare())) {
    LOG_WARN("prepare io request failed", K(ret), K(req));
  } else if (FALSE_IT(time_guard.click("prepare_req"))) {
  } else if (OB_FAIL(OB_IO_MANAGER.get_device_channel(req, device_channel))) {
    LOG_WARN("get device channel failed", K(ret), K(req));
  } else if (FALSE_IT(result->time_log_.dequeue_ts_ = ObTimeUtility::fast_current_time())) {
  } else {
    // Submit with bounded in-place retry on OB_EAGAIN (device queue / io depth full).
    // Do NOT retry via req.retry_io()/schedule_request -- that recurses on the same
    // stack and overflows it under sustained backpressure (see TC-removal regression).
    // Loop here instead, backing off outside the cond guard, until the request times
    // out or is canceled.
    do {
      {
        ObThreadCondGuard guard(result->get_cond());
        if (OB_FAIL(guard.get_ret())) {
          LOG_ERROR("fail to guard master condition", K(ret));
        } else if (req.is_canceled()) {
          ret = OB_CANCELED;
        } else if (OB_FAIL(device_channel->submit(req))) {
          if (OB_EAGAIN != ret) {
            LOG_WARN("submit io to device failed", K(ret));
          }
        } else {
          time_guard.click("device_submit");
        }
      }
      if (OB_EAGAIN == ret) {
        if (REACH_TIME_INTERVAL(1 * 1000L * 1000L)) {
          LOG_INFO("device channel eagain, will retry", K(ret));
        }
        ob_usleep(100); // 100us backoff, let get_events reap completions and free io depth
      }
    } while (OB_EAGAIN == ret
             && ObTimeUtility::current_time() < req.timeout_ts()
             && !req.is_canceled());
  }

  if (time_guard.get_diff() > 100000) {// 100ms
    LOG_INFO("submit_request cost too much time", K(ret), K(time_guard), K(req));
  }
  if (OB_FAIL(ret)) {
    // EAGAIN here means the bounded retry loop above exhausted the io timeout; finish
    // the request with the error rather than recursing (the removed retry_io path).
    req.io_result_->finish(ObIORetCode(ret), &req);
  }
  return ret;
}

}; // end namespace common
}; // end namespace oceanbase
