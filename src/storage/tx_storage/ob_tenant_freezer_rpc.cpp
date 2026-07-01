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


#include "ob_tenant_freezer_rpc.h"
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server.h"
#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_tenant_freezer.h"

namespace oceanbase
{
using namespace storage;
using namespace share;
using namespace storage::mds;
using namespace rootserver;
namespace obcall
{

// File-local freeze handlers (formerly ObTenantFreezerP members). Run in the target
// tenant's MTL context (the async_call caller sets it up via MTL_SWITCH).
static int do_tx_data_table_freeze_(const ObTenantFreezeArg &arg);
static int do_major_freeze_(const ObTenantFreezeArg &arg);
static int do_mds_table_freeze_(const ObTenantFreezeArg &arg);

int tenant_freeze_dispatch(const ObTenantFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  if (storage::MINOR_FREEZE == arg.freeze_type_) {
    LOG_ERROR("should not be here");
  } else if (storage::TX_DATA_TABLE_FREEZE == arg.freeze_type_) {
    if (OB_FAIL(do_tx_data_table_freeze_(arg))) {
    }
  } else if (storage::MAJOR_FREEZE == arg.freeze_type_) {
    if (OB_FAIL(do_major_freeze_(arg))) {
    }
  } else if (storage::MDS_TABLE_FREEZE == arg.freeze_type_) {
    if (OB_FAIL(do_mds_table_freeze_(arg))) {
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("unknown freeze type", K(arg), K(ret));
  }
  return ret;
}

static int do_tx_data_table_freeze_(const ObTenantFreezeArg &arg)
{
  int ret = OB_SUCCESS;

  LOG_INFO("start tx data table self freeze task in rpc handle thread", K(arg));

  common::ObSharedGuard<ObLSIterator> iter_guard;
  ObTenantTxDataFreezeGuard tenant_freeze_guard;
  ObLSService *ls_srv = share::g_mp->ls_service();
  ObTenantFreezer *freezer = share::g_mp->tenant_freezer();

  if (OB_FAIL(tenant_freeze_guard.init(freezer))) {
  } else if (!tenant_freeze_guard.can_freeze()) {
    // skip tx data self freeze due to another freeze task is running
  } else if (OB_FAIL(ls_srv->get_ls_iter(iter_guard, ObLSGetMod::TXSTORAGE_MOD))) {
  } else {
    int ls_cnt = 0;
    while (OB_SUCC(ret))
    {
      ObTxTableGuard tx_table_guard;
      ObLS *ls = nullptr;
      if (OB_FAIL(iter_guard->get_next(ls))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("get next ls failed.", KR(ret), K(arg));
        }
      } else if (OB_ISNULL(ls)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls is unexpected nullptr", KR(ret), K(arg));
      } else if (OB_FAIL(ls->get_tx_table_guard(tx_table_guard))) {
      } else if (OB_FAIL(tx_table_guard.self_freeze_task())) {
      }
      ++ls_cnt;
    }

    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
      if (0 == ls_cnt) {
        LOG_WARN("[TenantFreezer] no logstream", K(ret), K(ls_cnt));
      }
    }
  }

  LOG_INFO("finish self freeze task in rpc handle thread", KR(ret), K(arg));
  return ret;
}

static int do_major_freeze_(const ObTenantFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  
  SCN frozen_scn;

  if (OB_FAIL(ObMajorFreezeHelper::get_frozen_scn(frozen_scn))) {
  } else {
    int64_t frozen_scn_val = frozen_scn.get_val_for_tx();
    bool need_major = true;
    ObTenantFreezer *freezer = share::g_mp->tenant_freezer();
    ObRetryMajorInfo retry_major_info = freezer->get_retry_major_info();
    retry_major_info.frozen_scn_ = arg.try_frozen_scn_;
    if (arg.try_frozen_scn_ > 0) {
      if (arg.try_frozen_scn_ < frozen_scn_val) {
        need_major = false;
      } else {
        need_major = true;
      }
    } else if (!freezer->tenant_need_major_freeze()) {
      need_major = false;
    }
    if (!need_major) {
      retry_major_info.reset();
    } else {
      retry_major_info.frozen_scn_ = frozen_scn_val;

      ObMajorFreezeParam param;
      param.freeze_reason_ = rootserver::MF_MAJOR_COMPACT_TRIGGER;
      if (OB_FAIL(param.add_freeze_info())) {
      } else {
        LOG_INFO("do major freeze", K(param));
        if (OB_FAIL(ObMajorFreezeHelper::major_freeze(param))) {
        } else {
          retry_major_info.reset();
        }
      }
    }
    freezer->set_retry_major_info(retry_major_info);
  }

  LOG_INFO("finish tenant major freeze", KR(ret), K(frozen_scn));
  return ret;
}

static int do_mds_table_freeze_(const ObTenantFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("start mds table self freeze task in rpc handle thread", K(arg));

  common::ObSharedGuard<ObLSIterator> iter_guard;
  ObLSService *ls_srv = share::g_mp->ls_service();

  if (OB_FAIL(ls_srv->get_ls_iter(iter_guard, ObLSGetMod::TXSTORAGE_MOD))) {
  } else {
    int ls_cnt = 0;
    while (OB_SUCC(ret)) {
      ObLS *ls = nullptr;
      MdsTableMgrHandle mgr_handle;
      ObMdsTableMgr *mds_table_mgr = nullptr;

      if (OB_FAIL(iter_guard->get_next(ls))) {
        if (OB_ITER_END != ret) {
          LOG_WARN("get next ls failed.", KR(ret), K(arg));
        }
      } else if (OB_ISNULL(ls) || OB_ISNULL(ls->get_tablet_svr())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ls is unexpected nullptr", KR(ret), K(arg));
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(ls->flush_mds_table(INT64_MAX))) {
        }
      }
      ++ls_cnt;
    }

    if (ret == OB_ITER_END) {
      ret = OB_SUCCESS;
      if (0 == ls_cnt) {
        LOG_WARN("[TenantFreezer] no logstream", K(ret), K(ls_cnt));
      }
    }
  }

  LOG_INFO("finish mds table self freeze task in rpc handle thread", KR(ret), K(arg));
  return ret;
}

} // obcall
} // oceanbase
