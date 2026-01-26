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

#include <limits>
#include "lib/lock/ob_thread_cond.h"
#include "lib/stat/ob_diagnose_info.h"

namespace oceanbase
{
namespace common
{
int ObThreadCond::init(const int32_t event_no)
{
  int ret = OB_SUCCESS;
  int tmp_ret = 0;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    COMMON_LOG(WARN, "The thread cond has been inited, ", K(this), KCSTRING(common::lbt()), K(ret));
  } else if (!mutex_inited_ &&
      OB_UNLIKELY(0 != (tmp_ret = pthread_mutex_init(&mutex_, NULL)))) {
    ret = OB_ERR_SYS;
    COMMON_LOG(WARN, "Fail to init pthread mutex, ", K(tmp_ret), K(ret));
  } else {
    mutex_inited_ = true;
    if (!cond_inited_ &&
        OB_UNLIKELY(0 != (tmp_ret = pthread_cond_init(&cond_, NULL)))) {
      ret = OB_ERR_SYS;
      COMMON_LOG(WARN, "Fail to init pthread cond, ", K(tmp_ret), K(ret));
    } else {
      event_no_ = event_no;
      cond_inited_ = true;
      is_inited_ = true;
    }
  }

  if (!is_inited_) {
    destroy();
  }
  return ret;
}

void ObThreadCond::destroy()
{
  int ret = 0;
  if (cond_inited_) {
    if (OB_UNLIKELY(0 != (ret = pthread_cond_destroy(&cond_)))) {
      COMMON_LOG(WARN, "Fail to destroy pthread cond, ", K(ret));
    } else {
      cond_inited_ = false;
    }
  }

  if (mutex_inited_) {
    if (OB_UNLIKELY(0 != (ret = pthread_mutex_destroy(&mutex_)))) {
      COMMON_LOG(WARN, "Fail to destroy pthread mutex, ", K(ret));
    } else {
      mutex_inited_ = false;
    }
  }

  is_inited_ = false;
}

int ObThreadCond::wait_us(const uint64_t time_us)
{
  int ret = OB_SUCCESS;
  int tmp_ret = 0;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    COMMON_LOG(WARN, "The thread cond has not been inited, ", K(ret), KCSTRING(lbt()));
  } else {
    ObWaitEventGuard guard(event_no_, time_us / 1000, reinterpret_cast<int64_t>(this), 0, 0, true);
    if (0 == time_us) {
      if (OB_UNLIKELY(0 != (tmp_ret = ob_pthread_cond_wait(&cond_, &mutex_)))) {
        ret = OB_ERR_SYS;
        COMMON_LOG(WARN, "Fail to cond wait, ", K(tmp_ret), K(ret));
      }
    } else {
#ifdef __APPLE__
      // On macOS, pthread_cond_timedwait uses gettimeofday internally and can have
      // high overhead. Use pthread_cond_timedwait_relative_np instead, which:
      // 1. Takes a relative timeout (no need for gettimeofday to compute absolute time)
      // 2. Uses efficient internal implementation without polling
      // 3. Provides immediate signal response (no polling latency)
      struct timespec reltime;
      reltime.tv_sec = static_cast<time_t>(time_us / 1000000);
      reltime.tv_nsec = static_cast<long>((time_us % 1000000) * 1000);

      tmp_ret = pthread_cond_timedwait_relative_np(&cond_, &mutex_, &reltime);
      if (tmp_ret != 0) {
        if (ETIMEDOUT != tmp_ret) {
          ret = OB_ERR_SYS;
          COMMON_LOG(WARN, "Fail to timed cond wait, ", K(time_us), K(tmp_ret), K(ret));
        } else {
          ret = OB_TIMEOUT;
        }
      }
#else
      struct timeval curtime;
      struct timespec abstime;
      uint64_t us = 0;
      if (OB_UNLIKELY(0 != (tmp_ret = gettimeofday(&curtime, NULL)))) {
        ret = OB_ERR_SYS;
        COMMON_LOG(WARN, "Fail to get time, ", K(tmp_ret), K(ret));
      } else {
        uint64_t cur_time = static_cast<uint64_t>(curtime.tv_sec) *
                            static_cast<uint64_t>(1000000) +
                            static_cast<uint64_t>(curtime.tv_usec);
        us = cur_time + time_us;
        if (us < cur_time || us < time_us) {
          us = UINT64_MAX;
        }
      }

      if (OB_SUCC(ret)) {
        abstime.tv_sec = static_cast<decltype(abstime.tv_sec)>(std::min(static_cast<uint64_t>(std::numeric_limits<decltype(abstime.tv_sec)>::max()),
                                                                        static_cast<uint64_t>(us / 1000000)));
        abstime.tv_nsec = static_cast<decltype(abstime.tv_nsec)>(us % static_cast<uint64_t>(1000000)) * 1000;
        if (OB_UNLIKELY(0 != (tmp_ret = ob_pthread_cond_timedwait(&cond_, &mutex_, &abstime)))) {
          if (ETIMEDOUT != tmp_ret) {
            ret = OB_ERR_SYS;
            COMMON_LOG(WARN, "Fail to timed cond wait, ", K(time_us), K(tmp_ret), K(ret));
          } else {
            ret = OB_TIMEOUT;
          }
        }
      }
#endif
    }
  }

  return ret;
}

} /* namespace common */
} /* namespace oceanbase */
