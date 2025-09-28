/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
