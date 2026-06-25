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

#define USING_LOG_PREFIX STORAGE
#include "storage/restore/ob_tenant_restore_info_mgr.h"
#include "share/resource_manager/ob_cgroup_ctrl.h"
#include "observer/omt/ob_multi_tenant.h"
#include "share/ob_server_struct.h"

using namespace oceanbase::share;
using namespace oceanbase::common;

namespace oceanbase
{
namespace storage
{

ObTenantRestoreInfoMgr::ObTenantRestoreInfoMgr()
  : is_inited_(false),
    refresh_info_task_(*this),
    is_refreshed_(false),
    tenant_id_(OB_INVALID_TENANT_ID),
    restore_job_id_(),
    dest_id_(0)
{
}

ObTenantRestoreInfoMgr::~ObTenantRestoreInfoMgr()
{
  destroy();
}

int ObTenantRestoreInfoMgr::mtl_init(ObTenantRestoreInfoMgr *&restore_info_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(restore_info_mgr->init(MTL_ID()))) {
    LOG_WARN("failed to init tenant restore info mgr", K(ret), K(MTL_ID()));
  } else {
    LOG_INFO("success to init ObTenantRestoreInfoMgr", K(MTL_ID()));
  }
  return ret;
}

int ObTenantRestoreInfoMgr::init(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantRestoreInfoMgr init twice", K(ret));
  }  else {
    tenant_id_ = tenant_id;
    is_inited_ = true;
  }
  return ret;
}

int ObTenantRestoreInfoMgr::start()
{
  int ret = OB_SUCCESS;
  const bool repeat = true;
  if (IS_NOT_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTenantRestoreInfoMgr is not init", K(ret));
  } else if (!is_user_tenant(tenant_id_)) {
    // do nothing
  } else if (OB_FAIL(TG_SCHEDULE(MTL(omt::ObSharedTimer*)->get_tg_id(), refresh_info_task_, REFRESH_INFO_INTERVAL, repeat))) {
    LOG_WARN("failed to schedule tenant restore info mgr", K(ret));
  }
  return ret;
}

void ObTenantRestoreInfoMgr::wait()
{
  if (OB_LIKELY(is_inited_)) {
    TG_WAIT_TASK(MTL(omt::ObSharedTimer*)->get_tg_id(), refresh_info_task_);
    LOG_INFO("wait tenant restore info refresh task", K_(tenant_id));
  }
}

void ObTenantRestoreInfoMgr::stop()
{
  if (OB_LIKELY(is_inited_)) {
    TG_CANCEL_TASK(MTL(omt::ObSharedTimer*)->get_tg_id(), refresh_info_task_);
    LOG_INFO("stop tenant restore info refresh task", K_(tenant_id));
  }
}

void ObTenantRestoreInfoMgr::destroy()
{ 
  stop();
  wait();
  LOG_INFO("tenant restore info mgr destroy", K_(tenant_id));
}

int ObTenantRestoreInfoMgr::refresh_restore_info()
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql can't null", K(ret), KP(sql_proxy));
  } else if (MTL_TENANT_ROLE_CACHE_IS_INVALID()) {
    // wait tenant role refresh
  } else {
    bool is_primary_cluster = true;
    if (OB_FAIL(ObShareUtil::is_primary_cluster(is_primary_cluster))) {
      LOG_WARN("fail to check whether is primary cluster", KR(ret), K(is_primary_cluster));
    } else if (!is_primary_cluster) {
      stop();
    } else {
      lib::ObMutexGuard guard(mutex_);
      if (is_refreshed_) {
      } else {
        set_refreshed_();
        stop();
        LOG_INFO("get refresh restore info", K_(tenant_id));
      }
    }
  }

  return ret;
}

int ObTenantRestoreInfoMgr::get_restore_dest_id(int64_t &dest_id)
{
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(mutex_);
  int64_t idx = -1;
  if (!is_refreshed_) {
    ret = OB_EAGAIN;
    LOG_WARN("restore info has not been refreshed", K(ret));
  } else {
    dest_id_ = dest_id;
    LOG_INFO("get dest id", K(dest_id));
  }
  return ret;
}

void ObTenantRestoreInfoMgr::RestoreInfoRefresher::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(mgr_.refresh_restore_info())) {
    LOG_WARN("failed to refresh restore info", K(ret));
  } else {
    LOG_INFO("refresh restore info");
  }
}

}
}
