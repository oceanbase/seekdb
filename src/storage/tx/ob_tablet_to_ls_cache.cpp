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

#include "storage/tx/ob_tablet_to_ls_cache.h"
#include "storage/tx/ob_trans_ctx_mgr.h"

namespace oceanbase
{
namespace transaction
{
int ObTabletToLSCache::init(ObTxCtxMgr *tx_ctx_mgr)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "ObTabletToLSCache init twice", KR(ret), K(tx_ctx_mgr));
  } else if (OB_ISNULL(tx_ctx_mgr)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tx_ctx_mgr));
  } else {
    tx_ctx_mgr_ = tx_ctx_mgr;
    is_inited_ = true;
    TRANS_LOG(INFO, "ObTabletToLSCache init success", KR(ret), KPC(this));
  }
  return ret;
}

void ObTabletToLSCache::destroy()
{
  if (is_inited_) {
    tx_ctx_mgr_ = NULL;
    is_inited_ = false;
    TRANS_LOG(INFO, "ObTabletToLSCache destroy");
  }
}


int ObTabletToLSCache::create_tablet(const common::ObTabletID &tablet_id, const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;
  ObTimeGuard tg("ObTabletToLSCache::create_tablet", 5 * 1000); // 5ms

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTabletToLSCache has not inited", KR(ret), K(tablet_id), K(ls_id), KPC(this), K(lbt()));
  } else if (!tablet_id.is_valid() || !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tablet_id), K(ls_id));
  } else if (OB_FAIL(tx_ctx_mgr_->get_ls_tx_ctx_mgr(ls_id, ls_tx_ctx_mgr))) {
    TRANS_LOG(WARN, "get ls tx ctx mgr fail", KR(ret), K_(tx_ctx_mgr), K(tablet_id), K(ls_id));
  } else if (OB_ISNULL(ls_tx_ctx_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected ls_tx_ctx_mgr is null ", KR(ret), K(tablet_id), K(ls_id));
  } else if (OB_FAIL(tx_ctx_mgr_->revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr))) {
    TRANS_LOG(WARN, "revert ls tx ctx mgr fail", KR(ret), K(tablet_id), K(ls_id), KP(ls_tx_ctx_mgr));
  }
  TRANS_LOG(INFO, "create tablet cache", KR(ret), K(tablet_id), K(ls_id));

  return ret;
}

int ObTabletToLSCache::remove_tablet(const common::ObTabletID &tablet_id, const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("ObTabletToLSCache::remove_tablet", 5 * 1000); // 5ms

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTabletToLSCache has not inited", KR(ret), K(tablet_id), K(ls_id), KPC(this), K(lbt()));
  } else if (!tablet_id.is_valid() || !ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tablet_id), K(ls_id));
  }
  TRANS_LOG(INFO, "remove tablet cache", KR(ret), K(tablet_id), K(ls_id));

  return ret;
}

int ObTabletToLSCache::remove_ls_tablets(const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObTimeGuard tg("ObTabletToLSCache::remove_ls_tablets", 1000 * 1000); // 1s

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTabletToLSCache has not inited", KR(ret), K(ls_id), KPC(this), K(lbt()));
  } else if (!ls_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(ls_id));
  }
  TRANS_LOG(INFO, "remove ls tablets cache", KR(ret), K(ls_id));

  return ret;
}

int ObTabletToLSCache::check_and_get_ls_info(const common::ObTabletID &tablet_id,
                          share::ObLSID &ls_id,
                          bool &is_local_leader)
{
  int ret = OB_SUCCESS;
  ObLSTxCtxMgr *ls_tx_ctx_mgr = NULL;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "ObTabletToLSCache has not inited", KR(ret), K(tablet_id), KPC(this), K(lbt()));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), K(tablet_id));
  } else if (OB_FAIL(tx_ctx_mgr_->get_ls_tx_ctx_mgr(share::SYS_LS, ls_tx_ctx_mgr))) {
    TRANS_LOG(WARN, "get ls tx ctx mgr fail", KR(ret), K(tablet_id), K(share::SYS_LS));
  } else {
    if (OB_ISNULL(ls_tx_ctx_mgr)) {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "unexpected ls_tx_ctx_mgr is null", KR(ret), K(tablet_id));
    } else {
      ls_id = share::SYS_LS;
      is_local_leader = ls_tx_ctx_mgr->is_master();
    }
    int tmp_ret = tx_ctx_mgr_->revert_ls_tx_ctx_mgr(ls_tx_ctx_mgr);
    if (OB_SUCCESS != tmp_ret) {
      TRANS_LOG(WARN, "revert ls tx ctx mgr fail", KR(tmp_ret), K(tablet_id), K(share::SYS_LS));
      if (OB_SUCC(ret)) {
        ret = tmp_ret;
      }
    }
  }
  TRANS_LOG(DEBUG, "check and get ls info", K(tablet_id), K(ls_id), K(is_local_leader), K(ret));
  return ret;
}

int64_t ObTabletToLSCache::size()
{
  return 0;
}

} // transaction
} // oceanbase
