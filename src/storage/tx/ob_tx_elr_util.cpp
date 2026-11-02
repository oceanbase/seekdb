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

#include "ob_tx_elr_util.h"
#include "share/config/ob_tenant_config_mgr.h"
#include "ob_trans_event.h"

namespace oceanbase
{
namespace transaction
{

int ObTxELRUtil::check_and_update_tx_elr_info()
{
  int ret = OB_SUCCESS;
  // lite: sys-only -> ELR tenant-config refresh not applicable
  return ret;
}

void ObTxELRUtil::refresh_elr_tenant_config_()
{
  bool need_refresh = ObClockGenerator::getClock() - last_refresh_ts_ > REFRESH_INTERVAL;

  if (OB_UNLIKELY(need_refresh)) {
    if (OB_LIKELY(true)) {
      can_tenant_elr_ = GCONF.enable_early_lock_release;
      last_refresh_ts_ = ObClockGenerator::getClock();
    }
    if (REACH_TIME_INTERVAL(10000000 /* 10s */)) {
      TRANS_LOG(INFO, "refresh tenant config success",  K(*this));
    }
  }
}

} //transaction

} //oceanbase
