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

#include "observer/table_load/resource/ob_table_load_resource_service.h"
#include "share/rc/ob_module_provider.h"
#include "observer/omt/ob_tenant.h"
#include "observer/table_load/ob_table_load_table_ctx.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace lib;
using namespace share::schema;
using namespace table;
using namespace omt;
using namespace common::hash;

/**
 * ObTableLoadResourceService
 */

ObTableLoadResourceService::~ObTableLoadResourceService()
{
  obsys::ObWLockGuard w_guard(rw_lock_);
  ob_delete(resource_manager_);
}

int ObTableLoadResourceService::init()
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

int ObTableLoadResourceService::mtl_init(ObTableLoadResourceService *&service)
{
  int ret = OB_SUCCESS;
  
  if (OB_ISNULL(service)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(service));
  } else if (OB_FAIL(service->init())) {
  }

  return ret;
}

void ObTableLoadResourceService::stop()
{
  obsys::ObRLockGuard r_guard(rw_lock_);
  if (OB_NOT_NULL(resource_manager_)) {
    LOG_INFO("resource_manager_ start to stop");
    resource_manager_->stop();
  }
  LOG_INFO("resource_service finish to stop");
}

void ObTableLoadResourceService::wait()
{
  obsys::ObRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(resource_manager_)) {
    LOG_INFO("resource_manager_ start to wait");
    if (OB_FAIL(resource_manager_->wait())) {
    }
  }
  LOG_INFO("resource_service finish to wait");
}

void ObTableLoadResourceService::destroy()
{
  obsys::ObRLockGuard r_guard(rw_lock_);
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(resource_manager_)) {
    LOG_INFO("resource_manager_ start to destroy");
    resource_manager_->destroy();
  }
  LOG_INFO("resource_service finish to destroy");
}

int ObTableLoadResourceService::switch_to_leader()
{
  int ret = OB_SUCCESS;
  ObMutexGuard switch_guard(switch_lock_);
  int64_t start_time_us = ObTimeUtility::current_time();
  if (OB_FAIL(check_inner_stat())) {
  } else {
    if (OB_ISNULL(resource_manager_)) {
      obsys::ObWLockGuard w_guard(rw_lock_);
      if (OB_FAIL(alloc_resource_manager())) {
      }
    } else {
      obsys::ObRLockGuard r_guard(rw_lock_);
      ret = resource_manager_->resume();
      LOG_INFO("resource_service finish to resume",KR(ret));
    }
  }
  const int64_t cost_us = ObTimeUtility::current_time() - start_time_us;
  FLOG_INFO("resource_manager: switch_to_leader", KR(ret), K(cost_us), KP_(resource_manager));

  return ret;
}

int ObTableLoadResourceService::switch_to_follower_gracefully() {
  int ret = OB_SUCCESS;
  LOG_INFO("switch_to_follower_gracefully");
  if (OB_FAIL(inner_switch_to_follower())) {
  }
  
  return ret;
}

void ObTableLoadResourceService::switch_to_follower_forcedly() {
  int ret = OB_SUCCESS;
  LOG_INFO("switch_to_follower_forcedly");
  if (OB_FAIL(inner_switch_to_follower())) {
  }
}

int ObTableLoadResourceService::alloc_resource_manager()
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  int64_t len = sizeof(ObTableLoadResourceManager);
  if (FAILEDx(check_inner_stat())) {
  } else if (OB_NOT_NULL(resource_manager_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("resource_manager_ is not null", KR(ret), KP_(resource_manager));
  } else if (nullptr == (buf = common::ob_malloc(len, ObMemAttr("tenant_rm_mgr")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", KR(ret), K(len));
  } else if (FALSE_IT(resource_manager_ = new(buf) ObTableLoadResourceManager())) {
    // impossible
  } else if (OB_FAIL(resource_manager_->init())) {
  } else if (OB_FAIL(resource_manager_->start())) {
  }

  if (OB_SUCC(ret)) {
    LOG_INFO("succ to alloc resource_manager", KP_(resource_manager));
  } else {
    int tmp_ret = OB_SUCCESS;
    if (OB_TMP_FAIL(delete_resource_manager())) {
    }
    buf = nullptr;
  }

  return ret;
}

int ObTableLoadResourceService::delete_resource_manager() 
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(check_inner_stat())) {
  } else if (OB_ISNULL(resource_manager_)) {
    // no need to delete
  } else {
    resource_manager_->stop();
    if (OB_FAIL(resource_manager_->wait())) {
    } else {
      resource_manager_->destroy();
      LOG_INFO("succ to delete resource_manager");
    }
  }

  // ignore ret
  if (OB_NOT_NULL(resource_manager_)) {
    ob_delete(resource_manager_);
    resource_manager_ = nullptr;
  }
  LOG_INFO("finish to delete resource_manager", KR(ret));

  return ret;
}

int ObTableLoadResourceService::inner_switch_to_follower()
{
  int ret = OB_SUCCESS;
  ObMutexGuard switch_guard(switch_lock_);
  obsys::ObRLockGuard r_guard(rw_lock_);
  const int64_t start_time_us = ObTimeUtility::current_time();
  if (OB_NOT_NULL(resource_manager_)) {
    resource_manager_->pause();
  }
  const int64_t cost_us = ObTimeUtility::current_time() - start_time_us;
  FLOG_INFO("resource_manager: switch_to_follower", KR(ret), K(cost_us), KP_(resource_manager));
  
  return ret;
}

int ObTableLoadResourceService::check_inner_stat()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  }

  return ret;
}

int ObTableLoadResourceService::get_leader_addr(
    const share::ObLSID &ls_id,
    ObAddr &leader)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(GCTX.location_service_->get_leader_with_retry_until_timeout(
      GCONF.cluster_id, ls_id, leader, GET_LEADER_RETRY_TIMEOUT))) {
    LOG_WARN("fail to get ls location leader", KR(ret), K(ls_id));    
    if (is_location_service_renew_error(ret)) {
      ret = OB_EAGAIN;
    }
  }

  return ret;
}

int ObTableLoadResourceService::local_apply_resource(ObDirectLoadResourceApplyArg &arg, ObDirectLoadResourceOpRes &res)
{
  int ret = OB_SUCCESS;
  ObTableLoadResourceService *service = nullptr;
  if (OB_ISNULL(service = share::g_mp->table_load_resource_service())) {
    ret = OB_ERR_SYS;
    LOG_WARN("null table load resource service", KR(ret));
  } else if(OB_ISNULL(service->resource_manager_)) {
    ret = OB_EAGAIN;
    LOG_WARN("resource_manager_ is null", KR(ret));
  } else {
    ret = service->resource_manager_->apply_resource(arg, res);
  }
  
  return ret;
}

int ObTableLoadResourceService::local_release_resource(ObDirectLoadResourceReleaseArg &arg)
{
  int ret = OB_SUCCESS;
  ObTableLoadResourceService *service = nullptr;
  if (OB_ISNULL(service = share::g_mp->table_load_resource_service())) {
    ret = OB_ERR_SYS;
    LOG_WARN("null table load resource service", KR(ret));
  } else if(OB_ISNULL(service->resource_manager_)) {
    ret = OB_EAGAIN;
    LOG_WARN("resource_manager_ is null", KR(ret));
  } else {
    ret = service->resource_manager_->release_resource(arg);
  }
  
  return ret;
}

int ObTableLoadResourceService::local_update_resource(ObDirectLoadResourceUpdateArg &arg)
{
  int ret = OB_SUCCESS;
  ObTableLoadResourceService *service = nullptr;
  if (OB_ISNULL(service = share::g_mp->table_load_resource_service())) {
    ret = OB_ERR_SYS;
    LOG_WARN("null table load resource service", KR(ret));
  } else if(OB_ISNULL(service->resource_manager_)) {
    ret = OB_EAGAIN;
    LOG_WARN("resource_manager_ is null", KR(ret));
  } else {
    ret = service->resource_manager_->update_resource(arg);
  }
  
  return ret;
}

int ObTableLoadResourceService::apply_resource(ObDirectLoadResourceApplyArg &arg, ObDirectLoadResourceOpRes &res)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), KR(ret));
  } else {
    ObAddr leader;
    if (OB_FAIL(get_leader_addr(share::SYS_LS, leader))) {
    } else if (ObTableLoadUtils::is_local_addr(leader)) {
      ret = local_apply_resource(arg, res);
    } else {
      TABLE_LOAD_RESOURCE_RPC_CALL(apply_resource, leader, arg, res);
    }
  }
  
  return ret;
}

int ObTableLoadResourceService::release_resource(ObDirectLoadResourceReleaseArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), KR(ret));
  } else {
    ObAddr leader;
    if (OB_FAIL(get_leader_addr(share::SYS_LS, leader))) {
    } else if (ObTableLoadUtils::is_local_addr(leader)) {
      ret = local_release_resource(arg);
    } else {
      TABLE_LOAD_RESOURCE_RPC_CALL(release_resource, leader, arg);
    }
  }
  
  return ret;
}


} // namespace observer
} // namespace oceanbase
