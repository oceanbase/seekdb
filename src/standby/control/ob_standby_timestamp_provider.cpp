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

#define USING_LOG_PREFIX SERVER

#include "standby/control/ob_standby_timestamp_provider.h"
#include "lib/oblog/ob_log.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/ls/ob_ls.h"
#include "storage/tx/ob_timestamp_access.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{

int ObStandbyTimestampProvider::enable()
{
  int ret = OB_SUCCESS;
  transaction::ObTimestampAccess *timestamp_access =
      share::server_service<transaction::ObTimestampAccess>();
  if (OB_ISNULL(timestamp_access)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("timestamp access is null while enabling standby timestamp provider", KR(ret));
  } else {
    timestamp_access->set_provider(get_timestamp_);
  }
  return ret;
}

int ObStandbyTimestampProvider::disable()
{
  int ret = OB_SUCCESS;
  transaction::ObTimestampAccess *timestamp_access =
      share::server_service<transaction::ObTimestampAccess>();
  if (OB_ISNULL(timestamp_access)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("timestamp access is null while disabling standby timestamp provider", KR(ret));
  } else {
    timestamp_access->set_provider(nullptr);
  }
  return ret;
}

int ObStandbyTimestampProvider::get_timestamp_(int64_t &timestamp)
{
  int ret = OB_SUCCESS;
  storage::ObLS *ls = nullptr;
  storage::ObLSService *ls_service = share::server_service<storage::ObLSService>();
  if (OB_ISNULL(ls_service)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ls_service->get_ls(ls))) {
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const share::SCN replay_scn = ls->get_ls_wrs_handler()->get_ls_weak_read_ts();
    if (!replay_scn.is_valid_and_not_min()) {
      ret = OB_EAGAIN;
    } else {
      timestamp = static_cast<int64_t>(replay_scn.get_val_for_gts());
    }
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
