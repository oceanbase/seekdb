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

#include "standby/ob_standby_bootstrap_service.h"
#include "lib/ob_errno.h"
#include "lib/oblog/ob_log.h"
#include "logservice/ob_log_service.h"
#include "standby/ob_standby_source_util.h"
#include "share/config/ob_server_config.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "standby/restore/ob_standby_sstable_copier.h"
#include "standby/standby_host.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
namespace standby
{

ObStandbyBootstrapParam::ObStandbyBootstrapParam()
    : is_standby_cluster_(false),
      source_(),
      bandwidth_throttle_(nullptr),
      restore_config_(nullptr)
{
}

bool ObStandbyBootstrapParam::is_valid() const
{
  return is_standby_cluster_ && !source_.empty()
      && nullptr != bandwidth_throttle_ && nullptr != restore_config_
      && restore_config_->is_valid();
}

int ObStandbyBootstrapService::bootstrap(
    const ObStandbyBootstrapParam &param,
    share::SCN &source_end_scn)
{
  int ret = OB_SUCCESS;
  common::ObAddr primary_addr;
  share::SCN restore_checkpoint_scn;
  palf::PalfBaseInfo palf_base_info;
  storage::ObStandbySSTableCopier copier;
  source_end_scn.reset();

  if (OB_FAIL(check_bootstrap_source(param, primary_addr))) {
    LOG_ERROR("invalid standby bootstrap source", KR(ret), K(primary_addr));
  }

  if (OB_FAIL(ret)) {
  } else {
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(copier.init(
          primary_addr, param.bandwidth_throttle_, *param.restore_config_))) {
        LOG_WARN("failed to init standby sstable copier", KR(ret), K(primary_addr));
      } else if (OB_FAIL(copier.prepare_replay_base(
                     restore_checkpoint_scn, palf_base_info, source_end_scn))) {
        LOG_WARN("failed to prepare standby replay base", KR(ret), K(primary_addr));
      } else if (OB_FAIL(create_sys_ls_(param, palf_base_info, restore_checkpoint_scn))) {
        LOG_WARN("failed to create standby restore sys ls", KR(ret));
      } else if (OB_FAIL(copier.copy(restore_checkpoint_scn))) {
        LOG_WARN("failed to copy standby sstable baseline", KR(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("standby restore sys ls initialized and sstable baseline copied",
        K(primary_addr), K(restore_checkpoint_scn));
  } else {
    LOG_WARN("standby restore sys ls bootstrap did not finish",
        KR(ret), K(primary_addr), K(restore_checkpoint_scn));
  }
  return ret;
}

int ObStandbyBootstrapService::check_bootstrap_source(
    const ObStandbyBootstrapParam &param,
    common::ObAddr &primary_addr)
{
  int ret = OB_SUCCESS;
  primary_addr.reset();

  if (!param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid standby bootstrap parameter", KR(ret),
        K(param.is_standby_cluster_), KP(param.bandwidth_throttle_));
  } else if (OB_FAIL(StandbySourceParser::get_first_service_addr(
                 param.source_, primary_addr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("standby log source is not configured for bootstrap", KR(ret));
    } else {
      LOG_WARN("failed to get standby bootstrap source", KR(ret));
    }
  } else {
    LOG_INFO("got standby bootstrap source", K(primary_addr),
        "self", param.restore_config_->self_addr_);
  }
  return ret;
}

int ObStandbyBootstrapService::create_sys_ls_(
    const ObStandbyBootstrapParam &param,
    const palf::PalfBaseInfo &palf_base_info,
    const share::SCN &restore_checkpoint_scn)
{
  int ret = OB_SUCCESS;
  storage::ObLSService *ls_service = nullptr;
  logservice::ObLogService *log_service = nullptr;

  LOG_INFO("create empty LS for standby");
  if (!param.is_standby_cluster_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create_sys_tenant_ls_if_not_exists called but not standby cluster", KR(ret));
  } else if (OB_ISNULL(ls_service = share::server_service<storage::ObLSService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls_service is null");
  } else if (OB_ISNULL(log_service = share::server_service<logservice::ObLogService>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log_service is null");
  } else if (!palf_base_info.is_valid() || !restore_checkpoint_scn.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid replay base for standby bootstrap",
        KR(ret), K(palf_base_info), K(restore_checkpoint_scn));
  } else if (OB_FAIL(ls_service->create_ls_for_restore(
                 palf_base_info, restore_checkpoint_scn))) {
    LOG_WARN("create ls failed", KR(ret));
  }
  return ret;
}

} // namespace standby
} // namespace oceanbase
