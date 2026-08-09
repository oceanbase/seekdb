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

#include "ob_memstore_freezer_local_dispatch.h"
#include "data_plane/compaction/ob_i_major_freeze_coordinator.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

namespace oceanbase
{
using namespace share;
namespace storage
{

static int do_tx_data_table_freeze_(const ObMemstoreFreezeArg &arg);
static int do_major_freeze_(const ObMemstoreFreezeArg &arg);
static int do_mds_table_freeze_(const ObMemstoreFreezeArg &arg);

int dispatch_freeze(const ObMemstoreFreezeArg &arg)
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

static int do_tx_data_table_freeze_(const ObMemstoreFreezeArg &arg)
{
  int ret = OB_SUCCESS;

  LOG_INFO("start tx data table self freeze task", K(arg));

  ObTxDataFreezeGuard freeze_guard;
  ObLSService *ls_srv = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
  ObMemstoreFreezer *freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();
  ObLS *ls = nullptr;
  ObTxTableGuard tx_table_guard;

  if (OB_ISNULL(ls_srv)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[MemstoreFreezer] ls service is null", K(ret));
  } else if (OB_FAIL(freeze_guard.init(freezer))) {
  } else if (!freeze_guard.can_freeze()) {
    // skip tx data self freeze due to another freeze task is running
  } else if (OB_FAIL(ls_srv->get_ls(ls))) {
  } else if (OB_ISNULL(ls)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local log stream is unexpected nullptr", KR(ret), K(arg));
  } else if (OB_FAIL(ls->get_tx_table_guard(tx_table_guard))) {
  } else if (OB_FAIL(tx_table_guard.self_freeze_task())) {
  }

  LOG_INFO("finish tx data table self freeze task", KR(ret), K(arg));
  return ret;
}

static int do_major_freeze_(const ObMemstoreFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  SCN frozen_scn;
  data_plane::ObIMajorFreezeCoordinator *major_freeze_coordinator =
      ::oceanbase::share::server_service<::oceanbase::data_plane::ObIMajorFreezeCoordinator>();

  if (OB_ISNULL(major_freeze_coordinator)) {
    ret = OB_NOT_INIT;
    LOG_WARN("major freeze coordinator is not configured", KR(ret));
  } else if (OB_FAIL(major_freeze_coordinator->get_frozen_scn(frozen_scn))) {
  } else {
    int64_t frozen_scn_val = frozen_scn.get_val_for_tx();
    bool need_major = true;
    ObMemstoreFreezer *freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();
    ObRetryMajorInfo retry_major_info = freezer->get_retry_major_info();
    retry_major_info.frozen_scn_ = arg.try_frozen_scn_;
    if (arg.try_frozen_scn_ > 0) {
      if (arg.try_frozen_scn_ < frozen_scn_val) {
        need_major = false;
      } else {
        need_major = true;
      }
    } else if (!freezer->need_major_freeze()) {
      need_major = false;
    }
    if (!need_major) {
      retry_major_info.reset();
    } else {
      retry_major_info.frozen_scn_ = frozen_scn_val;

      if (OB_FAIL(
              major_freeze_coordinator
                  ->trigger_memstore_pressure_major_freeze())) {
      } else {
        retry_major_info.reset();
      }
    }
    freezer->set_retry_major_info(retry_major_info);
  }

  LOG_INFO("finish major freeze", KR(ret), K(frozen_scn));
  return ret;
}

static int do_mds_table_freeze_(const ObMemstoreFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("start mds table self freeze task", K(arg));

  ObLS *ls = nullptr;
  ObLSService *ls_srv = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();

  if (OB_ISNULL(ls_srv)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("[MemstoreFreezer] ls service is null", K(ret));
  } else if (OB_FAIL(ls_srv->get_ls(ls))) {
  } else if (OB_ISNULL(ls) || OB_ISNULL(ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("local log stream is unexpected nullptr", KR(ret), K(arg));
  } else if (OB_FAIL(ls->flush_mds_table(INT64_MAX))) {
  } else {
    LOG_INFO("flush local mds table successfully", K(arg));
  }

  LOG_INFO("finish mds table self freeze task", KR(ret), K(arg));
  return ret;
}

} // storage
} // oceanbase
