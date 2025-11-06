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
#include "ob_tenant_errsim_event_mgr.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
using namespace lib;

namespace share
{

ObTenantErrsimEventMgr::ObTenantErrsimEventMgr()
    : is_inited_(false),
      lock_(),
      event_array_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator("TErrsimEvent", MTL_ID()))
{
}

ObTenantErrsimEventMgr::~ObTenantErrsimEventMgr()
{
}

int ObTenantErrsimEventMgr::mtl_init(ObTenantErrsimEventMgr *&errsim_event_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(errsim_event_mgr->init())) {
    LOG_WARN("failed to init errsim event mgr", K(ret), KP(errsim_event_mgr));
  }
  return ret;
}

void ObTenantErrsimEventMgr::destroy()
{
  event_array_.destroy();
}

int ObTenantErrsimEventMgr::init()
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("tenant errsim event mgr init twice", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObTenantErrsimEventMgr::add_tenant_event(
    const ObTenantErrsimEvent &event)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("tenant errsim event mgr do not init", K(ret));
  } else if (!event.is_valid()) {
    LOG_WARN("add tenant event get invalid argument", K(ret), K(event));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (OB_FAIL(event_array_.push_back(event))) {
      LOG_WARN("failed to add tenant event", K(ret), K(event));
    }
  }
  return ret;
}


} //share
} //oceanbase
