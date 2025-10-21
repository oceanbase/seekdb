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
#include "storage/tx_storage/ob_ls_safe_destroy_task.h"
#include "storage/tx_storage/ob_ls_service.h"

namespace oceanbase
{
using namespace share;
namespace storage
{

ObLSSafeDestroyTask::ObLSSafeDestroyTask()
  : is_inited_(false),
    tenant_id_(OB_INVALID_ID),
    ls_handle_(),
    ls_service_(nullptr)
{
  type_ = ObSafeDestroyTask::ObSafeDestroyTaskType::LS;
}

ObLSSafeDestroyTask::~ObLSSafeDestroyTask()
{
  destroy();
}


bool ObLSSafeDestroyTask::safe_to_destroy()
{
  int ret = OB_SUCCESS;
  bool is_safe = false;
  ObLS *ls = nullptr;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  // there is no ls protected. safe to destroy
  if (IS_NOT_INIT) {
    is_safe = true;
  } else if (OB_FAIL(guard.switch_to(tenant_id_, false))) {
    LOG_WARN("switch tenant failed", K(ret), K(tenant_id_));
  } else if (OB_UNLIKELY(!ls_handle_.is_valid())) {
    is_safe = true;
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret));
    // the ls is not safe to destroy.
  } else if (OB_LIKELY(!ls->safe_to_destroy())) {
    // do nothing
  } else {
    is_safe = true;
  }
  return is_safe;
}

void ObLSSafeDestroyTask::destroy()
{
  int ret = OB_SUCCESS;
  // 1. reset ls_handle.
  // 2. tell the ls service, a ls is released.
  if (is_inited_) {
    // the ls tablet reset may use some MTL component we must switch to this tenant.
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    if (OB_FAIL(guard.switch_to(tenant_id_, false))) {
      LOG_WARN("switch to tenant failed, we cannot release the ls, because it is may core",
               K(ret), KPC(this));
    } else {
      ls_handle_.reset();
      ls_service_->dec_ls_safe_destroy_task_cnt();
      tenant_id_ = OB_INVALID_ID;
      ls_service_ = nullptr;
      is_inited_ = false;
    }
  }
}

int ObLSSafeDestroyTask::get_ls_id(ObLSID &ls_id) const
{
  int ret = OB_SUCCESS;
  ObLS *ls = nullptr;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  // there is no ls protected. safe to destroy
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("task is not inited", K(ret));
  } else if (OB_FAIL(guard.switch_to(tenant_id_, false))) {
    LOG_WARN("switch tenant failed", K(ret), K(tenant_id_));
  } else if (OB_UNLIKELY(!ls_handle_.is_valid())) {
    LOG_WARN("ls handle invalid", K(ret), K(tenant_id_), K_(ls_handle));
  } else if (OB_ISNULL(ls = ls_handle_.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret));
    // the ls is not safe to destroy.
  } else {
    ls_id = ls->get_ls_id();
  }
  return ret;
}


} // storage
} // oceanbase
