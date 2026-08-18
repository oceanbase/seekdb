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

#include "ob_ts_mgr.h"
#include "ob_timestamp_access.h"
#include "share/config/ob_server_config.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace transaction
{

int ObTsMgr::get_gts(SCN &scn)
{
  int ret = OB_SUCCESS;
  int64_t gts = 0;
  if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::transaction::ObTimestampAccess>()->get_number(gts))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "get local timestamp failed", KR(ret));
    }
  } else if (OB_FAIL(scn.convert_for_gts(gts))) {
  }
  return ret;
}

int ObTsMgr::get_gts(const MonotonicTs stc,
                     SCN &scn,
                     MonotonicTs &receive_gts_ts)
{
  int ret = get_gts(scn);
  if (OB_SUCC(ret)) {
    receive_gts_ts = MonotonicTs::current_time();
  }
  return ret;
}

int ObTsMgr::get_gts_sync(const int64_t timeout_us, SCN &scn)
{
  int ret = OB_EAGAIN;
  const int64_t RETRY_INTERVAL_US = 10 * 1000;
  const int64_t expire_ts = ObClockGenerator::getClock() + timeout_us;
  while (OB_EAGAIN == ret) {
    if (ObClockGenerator::getClock() >= expire_ts) {
      ret = OB_TIMEOUT;
    } else if (OB_EAGAIN == (ret = get_gts(scn))) {
      ob_usleep(RETRY_INTERVAL_US);
    }
  }
  return ret;
}

int ObTsMgr::wait_gts_elapse(const SCN &scn)
{
  int ret = OB_EAGAIN;
  const int64_t expire_ts = ObClockGenerator::getClock() + GCONF.rpc_timeout;
  while (OB_EAGAIN == ret) {
    SCN current_scn;
    if (ObClockGenerator::getClock() >= expire_ts) {
      ret = OB_TIMEOUT;
    } else if (OB_FAIL(get_gts(current_scn))) {
      if (OB_EAGAIN == ret) {
        ob_usleep(500);
      }
    } else if (current_scn < scn) {
      ret = OB_EAGAIN;
      ob_usleep(500);
    }
  }
  return ret;
}

ObTsMgr &ObTsMgr::get_instance()
{
  static ObTsMgr instance;
  return instance;
}

} // namespace transaction
} // namespace oceanbase
