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

#define USING_LOG_PREFIX RS_COMPACTION

#include "rootserver/freeze/ob_major_freeze_service.h"

namespace oceanbase
{
namespace rootserver
{
using namespace share;

int ObMajorFreezeService::init()
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

ObMajorFreezeService::~ObMajorFreezeService()
{
  SpinWLockGuard w_guard(rw_lock_);
  ob_delete(tenant_major_freeze_);
}

int ObMajorFreezeService::switch_to_leader()
{
  ObRecursiveMutexGuard switch_guard(switch_lock_);
  int64_t start_time_us = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check_inner_stat", KR(ret));
  } else {
    if (OB_ISNULL(tenant_major_freeze_)) {
      SpinWLockGuard w_guard(rw_lock_);
      if (OB_FAIL(alloc_tenant_major_freeze())) {
        LOG_WARN("fail to alloc tenant_major_freeze", KR(ret));
      }
    } else {
      SpinRLockGuard r_guard(rw_lock_);
      tenant_major_freeze_->resume();
    }
  }
  const int64_t cost_us = ObTimeUtility::current_time() - start_time_us;
  FLOG_INFO("major_freeze: switch_to_leader", KR(ret), K(cost_us), KP_(tenant_major_freeze));

  return ret;
}

int ObMajorFreezeService::switch_to_follower_gracefully()
{
  int ret = OB_SUCCESS;
  LOG_INFO("switch_to_follower_gracefully");
  if (OB_FAIL(inner_switch_to_follower())) {
    LOG_WARN("fail to switch to follower", KR(ret));
  }
  return ret;
}

void ObMajorFreezeService::switch_to_follower_forcedly()
{
  int ret = OB_SUCCESS;
  LOG_INFO("switch_to_follower_forcedly");
  if (OB_FAIL(inner_switch_to_follower())) {
    LOG_WARN("fail to switch to follower", KR(ret));
  }
}

int ObMajorFreezeService::inner_switch_to_follower()
{
  ObRecursiveMutexGuard switch_guard(switch_lock_);
  SpinRLockGuard r_guard(rw_lock_);
  const int64_t start_time_us = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(tenant_major_freeze_)) {
    tenant_major_freeze_->pause();
  }
  const int64_t cost_us = ObTimeUtility::current_time() - start_time_us;
  FLOG_INFO("major_freeze: switch_to_follower", KR(ret), K(cost_us), KP_(tenant_major_freeze));
  return ret;
}

int ObMajorFreezeService::alloc_tenant_major_freeze()
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  int64_t len = sizeof(ObTenantMajorFreeze);
  bool is_primary_service = true;
  ObMajorFreezeServiceType service_type = get_service_type();
  if ((service_type <= ObMajorFreezeServiceType::SERVICE_TYPE_INVALID)
      || (service_type >= ObMajorFreezeServiceType::SERVICE_TYPE_MAX)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected major freeze service type", KR(ret), K(service_type));
  } else {
    is_primary_service = (ObMajorFreezeServiceType::SERVICE_TYPE_PRIMARY == service_type) ? true : false;
  }

  if (FAILEDx(check_inner_stat())) {
    LOG_WARN("fail to check_inner_stat", KR(ret));
  } else if (OB_NOT_NULL(tenant_major_freeze_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant_major_freeze is not null", KR(ret), KP_(tenant_major_freeze));
  } else if (nullptr == (buf = common::ob_malloc(len, ObMemAttr("tenant_mf_mgr")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", KR(ret), K(len));
  } else if (FALSE_IT(tenant_major_freeze_ = new(buf) ObTenantMajorFreeze{})) {
    // impossible
  } else if (OB_FAIL(tenant_major_freeze_->init(is_primary_service, *GCTX.sql_proxy_,
             *GCTX.config_, *GCTX.schema_service_))) {
    LOG_WARN("fail to init tenant_major_freeze", KR(ret), K(is_primary_service));
  } else if (OB_FAIL(tenant_major_freeze_->start())) {
    LOG_WARN("fail to start tenant_major_freeze", KR(ret), K(is_primary_service));
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("succ to alloc tenant_major_freeze", KP_(tenant_major_freeze),
             K(is_primary_service));
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(delete_tenant_major_freeze())) {
      LOG_WARN("fail to delete tenant major freeze", KR(tmp_ret), K(is_primary_service));
    }
    buf = nullptr;
  }
  return ret;
}

int ObMajorFreezeService::delete_tenant_major_freeze()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(check_inner_stat())) {
    LOG_WARN("fail to check_inner_stat", KR(ret));
  } else if (OB_ISNULL(tenant_major_freeze_)) {
    // no need to delete
  } else if (FALSE_IT(tenant_major_freeze_->stop())) {
  } else if (OB_FAIL(tenant_major_freeze_->wait())) {
    LOG_WARN("fail to wait", KR(ret));
  } else if (OB_FAIL(tenant_major_freeze_->destroy())) {
    LOG_WARN("fail to destroy", KR(ret));
  } else {
    LOG_INFO("succ to delete tenant_major_freeze");
  }

  // ignore ret
  if (OB_NOT_NULL(tenant_major_freeze_)) {
    ob_delete(tenant_major_freeze_);
    tenant_major_freeze_ = nullptr;
  }

  LOG_INFO("finish to delete tenant_major_freeze", KR(ret));

  return ret;
}

int ObMajorFreezeService::launch_major_freeze(const ObMajorFreezeReason freeze_reason)
{
  int ret = OB_SUCCESS;
  bool can_launch = ATOMIC_BCAS(&is_launched_, false, true);

  if (!can_launch) {
    // 'sync operation' of launch_major_freeze not finish
    ret = OB_MAJOR_FREEZE_NOT_FINISHED;
    LOG_WARN("previous major freeze not finish, please wait", KR(ret), K_(is_launched));
  } else {
    ObRecursiveMutexGuard guard(lock_);
    SpinRLockGuard r_guard(rw_lock_);
    if (OB_ISNULL(tenant_major_freeze_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tenant_major_freeze is null", KR(ret), KP_(tenant_major_freeze));
    } else if (OB_FAIL(tenant_major_freeze_->launch_major_freeze(freeze_reason))) {
      // 'async operation' of launch_major_freeze not finish
      if ((OB_MAJOR_FREEZE_NOT_FINISHED != ret) && (OB_FROZEN_INFO_ALREADY_EXIST != ret)) {
        LOG_WARN("fail to launch_major_freeze", KR(ret));
      }
    }
    ATOMIC_STORE(&is_launched_, false); // set is as false no matter its previous value.
  }

  return ret;
}

int ObMajorFreezeService::suspend_merge()
{
  ObRecursiveMutexGuard guard(lock_);
  SpinRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_major_freeze_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant_major_freeze is null", KR(ret), KP_(tenant_major_freeze));
  } else if (OB_FAIL(tenant_major_freeze_->suspend_merge())) {
    LOG_WARN("fail to suspend_merge", KR(ret));
  }
  return ret;
}

int ObMajorFreezeService::resume_merge()
{
  ObRecursiveMutexGuard guard(lock_);
  SpinRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_major_freeze_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant_major_freeze is null", KR(ret), KP_(tenant_major_freeze));
  } else if (OB_FAIL(tenant_major_freeze_->resume_merge())) {
    LOG_WARN("fail to resume_merge", KR(ret));
  }
  return ret;
}

int ObMajorFreezeService::clear_merge_error()
{
  ObRecursiveMutexGuard guard(lock_);
  SpinRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(tenant_major_freeze_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant_major_freeze is null", KR(ret));
  } else if (OB_FAIL(tenant_major_freeze_->clear_merge_error())) {
    LOG_WARN("fail to clear_merge_error", KR(ret));
  }
  return ret;
}

int ObMajorFreezeService::check_inner_stat()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  }
  return ret;
}

void ObMajorFreezeService::stop()
{
  LOG_INFO("major_freeze_service start to stop");
  ObRecursiveMutexGuard guard(lock_);
  SpinRLockGuard r_guard(rw_lock_);
  if (OB_NOT_NULL(tenant_major_freeze_)) {
    LOG_INFO("tenant_major_freeze_ start to stop");
    tenant_major_freeze_->stop();
  }
  LOG_INFO("major_freeze_service finish to stop");
}

void ObMajorFreezeService::wait()
{
  LOG_INFO("major_freeze_service start to wait");
  ObRecursiveMutexGuard guard(lock_);
  SpinRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(tenant_major_freeze_)) {
    LOG_INFO("tenant_major_freeze_ start to wait");
    if (OB_FAIL(tenant_major_freeze_->wait())) {
      LOG_WARN("fail to wait", KR(ret));
    }
  }
  LOG_INFO("major_freeze_service finish to wait");
}

void ObMajorFreezeService::destroy()
{
  LOG_INFO("major_freeze_service start to destroy");
  ObRecursiveMutexGuard guard(lock_);
  SpinRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(tenant_major_freeze_)) {
    LOG_INFO("tenant_major_freeze_ start to destroy");
    if (OB_FAIL(tenant_major_freeze_->destroy())) {
      LOG_WARN("fail to destroy", KR(ret));
    }
  }
  LOG_INFO("major_freeze_service finish to destroy");
}

bool ObMajorFreezeService::is_paused() const
{
  bool is_paused = true;
  if (OB_NOT_NULL(tenant_major_freeze_)) {
    is_paused = tenant_major_freeze_->is_paused();
  }
  // if tenant_major_freeze_ is null, treat it as paused
  return is_paused;
}

int ObMajorFreezeService::get_uncompacted_tablets(
    ObArray<ObTabletReplica> &uncompacted_tablets,
    ObArray<uint64_t> &uncompacted_table_ids) const
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    if (OB_ISNULL(tenant_major_freeze_)) {
      ret = OB_LEADER_NOT_EXIST;
      LOG_WARN("tenant_major_freeze is null", KR(ret));
    } else if (OB_FAIL(tenant_major_freeze_->get_uncompacted_tablets(uncompacted_tablets, uncompacted_table_ids))) {
      LOG_WARN("fail to get uncompacted tablets", KR(ret));
    }
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////
ObPrimaryMajorFreezeService::ObPrimaryMajorFreezeService() : ObMajorFreezeService()
{}

ObPrimaryMajorFreezeService::~ObPrimaryMajorFreezeService()
{}

int ObPrimaryMajorFreezeService::mtl_init(ObPrimaryMajorFreezeService *&service)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(service->init())) {
    LOG_WARN("fail to init primary major freeze service", KR(ret));
  }
  return ret;
}

ObMajorFreezeServiceType ObPrimaryMajorFreezeService::get_service_type() const
{
  return ObMajorFreezeServiceType::SERVICE_TYPE_PRIMARY;
}

///////////////////////////////////////////////////////////////////////////////
ObRestoreMajorFreezeService::ObRestoreMajorFreezeService() : ObMajorFreezeService()
{}

ObRestoreMajorFreezeService::~ObRestoreMajorFreezeService()
{}

int ObRestoreMajorFreezeService::mtl_init(ObRestoreMajorFreezeService *&service)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(service->init())) {
    LOG_WARN("fail to init restore major freeze service", KR(ret));
  }
  return ret;
}

ObMajorFreezeServiceType ObRestoreMajorFreezeService::get_service_type() const
{
  return ObMajorFreezeServiceType::SERVICE_TYPE_RESTORE;
}

} // end namespace rootserver
} // end namespace oceanbase
