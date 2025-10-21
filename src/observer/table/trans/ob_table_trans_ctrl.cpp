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
#include "ob_table_trans_ctrl.h"
#include "storage/tx/ob_trans_service.h"
#include "src/share/table/ob_table_util.h"

using namespace oceanbase::observer;
using namespace oceanbase::common;
using namespace oceanbase::table;
using namespace oceanbase::share;
using namespace oceanbase::sql;
using namespace oceanbase::sql::stmt;
using namespace oceanbase::transaction;
using namespace oceanbase::rpc;



int ObTableTransCtrl::setup_tx_snapshot(ObTableTransParam &trans_param)
{
  int ret = OB_SUCCESS;
  bool strong_read = ObTableConsistencyLevel::STRONG == trans_param.consistency_level_;
  ObTransService *txs = MTL(ObTransService*);

  if (strong_read) {
    if (trans_param.ls_id_.is_valid() && !trans_param.need_global_snapshot_) {
      if (OB_FAIL(txs->get_ls_read_snapshot(*trans_param.trans_desc_,
                                            ObTxIsolationLevel::RC,
                                            trans_param.ls_id_,
                                            trans_param.timeout_ts_,
                                            trans_param.tx_snapshot_))) {
        LOG_WARN("fail to get LS read snapshot", K(ret));
      }
    } else if (OB_FAIL(txs->get_read_snapshot(*trans_param.trans_desc_,
                                              ObTxIsolationLevel::RC,
                                              trans_param.timeout_ts_,
                                              trans_param.tx_snapshot_))) {
      LOG_WARN("fail to get global read snapshot", K(ret));
    }
  } else {
    SCN weak_read_snapshot;
    if (OB_FAIL(txs->get_weak_read_snapshot_version(-1, // system variable : max read stale time for user
                                                    false,
                                                    weak_read_snapshot))) {
      LOG_WARN("fail to get weak read snapshot", K(ret));
    } else {
      trans_param.tx_snapshot_.init_weak_read(weak_read_snapshot);
    }
  }

  return ret;
}

int ObTableTransCtrl::init_read_trans(ObTableTransParam &trans_param)
{
  int ret = OB_SUCCESS;
  bool strong_read = ObTableConsistencyLevel::STRONG == trans_param.consistency_level_;
  ObTransService *txs = MTL(ObTransService*);

  if (OB_FAIL(txs->acquire_tx(trans_param.trans_desc_, OB_KV_DEFAULT_SESSION_ID))) {
    LOG_WARN("failed to acquire tx desc", K(ret));
  } else if (OB_FAIL(setup_tx_snapshot(trans_param))) {
    LOG_WARN("setup txn snapshot fail", K(ret), K(trans_param), K(strong_read));
    txs->release_tx(*trans_param.trans_desc_);
    trans_param.trans_desc_ = NULL;
  }

  return ret;
}

void ObTableTransCtrl::release_read_trans(ObTxDesc *&trans_desc)
{
  if (OB_NOT_NULL(trans_desc)) {
    ObTransService *txs = MTL(ObTransService*);
    txs->release_tx(*trans_desc);
    trans_desc = NULL;
  }
}

int ObTableTransCtrl::sync_end_trans(ObTableTransParam &trans_param)
{
  int ret = OB_SUCCESS;
  ObTransService *txs = MTL(ObTransService*);
  const int64_t stmt_timeout_ts = trans_param.timeout_ts_;

  if (trans_param.is_rollback_) {
    if (OB_FAIL(txs->rollback_tx(*trans_param.trans_desc_))) {
      LOG_WARN("fail rollback trans when session terminate", K(ret), K(trans_param));
    }
  } else {
    ACTIVE_SESSION_FLAG_SETTER_GUARD(in_committing);
    if (OB_FAIL(txs->commit_tx(*trans_param.trans_desc_,
                               stmt_timeout_ts,
                               &ObTableUtils::get_kv_normal_trace_info()))) {
      LOG_WARN("fail commit trans when session terminate", K(ret), KPC_(trans_param.trans_desc), K(stmt_timeout_ts));
    }
  }

  int tmp_ret = ret;
  if (OB_FAIL(txs->release_tx(*trans_param.trans_desc_))) {
    // overwrite ret
    LOG_ERROR("release tx failed", K(ret), K(trans_param));
  }
  if (trans_param.lock_handle_ != nullptr) {
    HTABLE_LOCK_MGR->release_handle(*trans_param.lock_handle_);
  }
  ret = tmp_ret == OB_SUCCESS ? ret : tmp_ret;
  trans_param.trans_desc_ = NULL;
  LOG_DEBUG("ObTableApiProcessorBase::sync_end_trans", K(ret), K(trans_param.is_rollback_), K(stmt_timeout_ts));

  return ret;
}

int ObTableTransCtrl::async_commit_trans(ObTableTransParam &trans_param)
{
  int ret = OB_SUCCESS;
  ObTransService *txs = MTL(ObTransService*);
  const bool is_rollback = false;
  if (NULL == trans_param.create_cb_functor_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create callback functor is null", K(ret));
  } else {
    ObTableAPITransCb *callback = trans_param.create_cb_functor_->new_callback();
    if (OB_ISNULL(callback)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new callback", K(ret));
    } else {
      if (OB_NOT_NULL(trans_param.req_)) {
        trans_param.req_->set_trace_point(rpc::ObRequest::OB_EASY_REQUEST_TABLE_API_ACOM_TRANS);
      }
      callback->set_is_need_rollback(is_rollback);
      callback->set_end_trans_type(sql::ObExclusiveEndTransCallback::END_TRANS_TYPE_IMPLICIT);
      callback->set_lock_handle(trans_param.lock_handle_);
      callback->handout();
      callback->set_tx_desc(trans_param.trans_desc_);
      const int64_t stmt_timeout_ts = trans_param.timeout_ts_;
      // clear thread local variables used to wait in queue
      request_finish_callback();
      // callback won't been called if any error occurred
      if (OB_FAIL(txs->submit_commit_tx(*trans_param.trans_desc_,
                                        stmt_timeout_ts,
                                        *callback,
                                        &ObTableUtils::get_kv_normal_trace_info()))) {
        LOG_WARN("fail end trans when session terminate", K(ret), K(trans_param));
        callback->callback(ret);
      }
      trans_param.did_async_commit_ = true;
      // ignore the return code of end_trans
      trans_param.had_do_response_ = true; // don't send response in this worker thread
      // @note after this code, the callback object can NOT be read any more!!!
      callback->destroy_cb_if_no_ref();
      trans_param.trans_desc_ = NULL;
    }
  }

  return ret;
}
