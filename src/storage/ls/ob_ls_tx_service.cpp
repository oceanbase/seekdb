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

#define USING_LOG_PREFIX TRANS

#include "ob_ls_tx_service.h"
#include "storage/tx/ob_ts_mgr.h"
#include "storage/throttle/ob_throttle_unit.h"
#include "storage/throttle/ob_throttle_unit.h"
#include "storage/tx/ob_trans_service.h"
#include "storage/tx/ob_tx_replay_executor.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_memstore_freezer.h"

namespace oceanbase
{
using namespace share;
using namespace transaction;
using namespace transaction::tablelock;
using namespace palf;

namespace storage
{
using namespace checkpoint;

int ObLSTxService::init(ObLSTxCtxMgr *mgr,
                        ObTransService *trans_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr) || OB_ISNULL(trans_service)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(mgr), KP(trans_service));
  } else {
    mgr_ = mgr;
    trans_service_ = trans_service;
  }
  return ret;
}

int ObLSTxService::create_tx_ctx(ObTxCreateArg arg,
                                 bool &existed,
                                 ObTxCtx *&ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ret = mgr_->create_tx_ctx(arg, existed, ctx);
  }
  return ret;
}

int ObLSTxService::get_tx_ctx(const transaction::ObTransID &tx_id,
                              const bool for_replay,
                              ObTxCtx *&ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ret = mgr_->get_tx_ctx(tx_id, for_replay, ctx);
  }
  return ret;
}

int ObLSTxService::get_tx_ctx_with_timeout(const transaction::ObTransID &tx_id,
                                           const bool for_replay,
                                           transaction::ObTxCtx *&tx_ctx,
                                           const int64_t lock_timeout) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ret = mgr_->get_tx_ctx_with_timeout(tx_id, for_replay, tx_ctx, lock_timeout);
  }

  return ret;
}

int ObLSTxService::get_tx_start_session_id(const transaction::ObTransID &tx_id, uint32_t &session_id) const
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ObTxCtx *ctx;
    if (OB_FAIL(mgr_->get_tx_ctx_directly_from_hash_map(tx_id, ctx))) {
      if (OB_TRANS_CTX_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        TRANS_LOG(INFO, "ctx not existed", K(tx_id));
      } else {
        TRANS_LOG(WARN, "get ctx failed", K(ret), K(tx_id));
      }
    } else if (OB_ISNULL(ctx)) {
      ret = OB_BAD_NULL_ERROR;
      TRANS_LOG(WARN, "get ctx is null", K(ret), K(tx_id));
    } else {
      session_id = ctx->get_session_id();
      if (OB_TMP_FAIL(mgr_->revert_tx_ctx(ctx))) {
        TRANS_LOG(ERROR, "fail to revert tx", K(ret), K(tmp_ret), K(tx_id), KPC(ctx));
      }
    }
  }
  return ret;
}

int ObLSTxService::revert_tx_ctx(ObTransCtx *ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ret = mgr_->revert_tx_ctx(ctx);
  }
  return ret;
}

int ObLSTxService::get_read_store_ctx(const ObTxReadSnapshot &snapshot,
                                      const bool read_latest,
                                      const int64_t lock_timeout,
                                      ObStoreCtx &store_ctx,
                                      ObTxDesc *tx_desc) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(trans_service_) || OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret), KP(trans_service_), KP(mgr_));
  } else if (OB_FAIL(mgr_->start_readonly_request())) {
    TRANS_LOG(WARN, "start readonly request failed", K(ret));
  } else {
    store_ctx.is_read_store_ctx_ = true;
    ret = trans_service_->get_read_store_ctx(snapshot, read_latest, lock_timeout, store_ctx, tx_desc);
    if (OB_FAIL(ret)) {
      mgr_->end_readonly_request();
    } else {
      READ_CHECKER_RECORD(store_ctx);
    }
  }
  return ret;
}

int ObLSTxService::get_read_store_ctx(const SCN &snapshot,
                                      const int64_t lock_timeout,
                                      ObStoreCtx &store_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(trans_service_) || OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret), KP(trans_service_), KP(mgr_));
  } else if (OB_FAIL(mgr_->start_readonly_request())) {
    TRANS_LOG(WARN, "start readonly request failed", K(ret));
  } else {
    store_ctx.is_read_store_ctx_ = true;

    ret = trans_service_->get_read_store_ctx(snapshot, lock_timeout, store_ctx);
    if (OB_FAIL(ret)) {
      mgr_->end_readonly_request();
    } else {
      READ_CHECKER_RECORD(store_ctx);
    }
  }
  return ret;
}

int ObLSTxService::get_write_store_ctx(ObTxDesc &tx,
                                       const ObTxReadSnapshot &snapshot,
                                       const concurrent_control::ObWriteFlag write_flag,
                                       storage::ObStoreCtx &store_ctx,
                                       const ObTxSEQ &spec_seq_no) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(trans_service_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    int64_t abs_expire_ts = ObClockGenerator::getClock() + tx.get_timeout_us();
    if (abs_expire_ts < 0) {
      abs_expire_ts = ObClockGenerator::getClock() + share::ObThrottleUnit<ObTxDataAllocator>::DEFAULT_MAX_THROTTLE_TIME;
    }

    ObTxDataThrottleGuard tx_data_throttle_guard(false /* for_replay */, abs_expire_ts);
    ret = trans_service_->get_write_store_ctx(tx, snapshot, write_flag, store_ctx, spec_seq_no, false);
  }
  return ret;
}

int ObLSTxService::revert_store_ctx(storage::ObStoreCtx &store_ctx) const
{
  int ret = OB_SUCCESS;

  if (store_ctx.is_read_store_ctx()) {
    // do not overrite ret
    int tmp_ret = OB_SUCCESS;
    if (OB_ISNULL(mgr_)) {
      tmp_ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(ERROR, "mgr is null", K(tmp_ret), KP(this));
    } else {
      READ_CHECKER_RELEASE(store_ctx);
      (void)mgr_->end_readonly_request();
    }
  }

  if (OB_ISNULL(trans_service_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ret = trans_service_->revert_store_ctx(store_ctx);
  }
  return ret;
}

int ObLSTxService::check_tx_status(SCN &min_start_scn,
                                   transaction::MinStartScnStatus &status)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(trans_service_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", K(ret));
  } else {
    ret = mgr_->check_tx_status(min_start_scn, status);
  }
  return ret;
}

int ObLSTxService::check_all_tx_clean_up() const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (mgr_->get_active_tx_count() > 0) {
    // there is some tx not finished, retry.
    ret = OB_EAGAIN;
  } else {
    TRANS_LOG(INFO, "wait_all_tx_cleaned_up cleaned up success");
  }
  return ret;
}

ERRSIM_POINT_DEF(EN_GC_CHECK_RD_TX);
int ObLSTxService::check_all_readonly_tx_clean_up() const
{
  int ret = OB_SUCCESS;
  int64_t active_readonly_request_count = 0;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if ((active_readonly_request_count = mgr_->get_total_active_readonly_request_count()) > 0) {
    if (REACH_TIME_INTERVAL(5000000)) {
      TRANS_LOG(INFO, "readonly requests are active", K(active_readonly_request_count));
      READ_CHECKER_PRINT();
    }
    ret = OB_EAGAIN;
  } else {
    TRANS_LOG(INFO, "wait_all_readonly_tx_cleaned_up cleaned up success");
  }

#ifdef ERRSIM
    if (OB_SUCC(ret)) {
      ret = EN_GC_CHECK_RD_TX ? : OB_SUCCESS;
      if (OB_FAIL(ret)) {
        TRANS_LOG(INFO, "fake EN_GC_CHECK_RD_TX", K(ret));
      }
    }
#endif
  return ret;
}

int ObLSTxService::block_tx()
{
  int ret = OB_SUCCESS;
  bool unused_is_all_tx_clean_up = false;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->block_tx(unused_is_all_tx_clean_up))) {
    TRANS_LOG(WARN, "block rw tx failed");
  } else {
    TRANS_LOG(INFO, "block rw tx success");
  }
  return ret;
}

int ObLSTxService::block_all()
{
  int ret = OB_SUCCESS;
  bool unused_is_all_tx_clean_up = false;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->block_all(unused_is_all_tx_clean_up))) {
    TRANS_LOG(WARN, "block all failed");
  } else {
    TRANS_LOG(INFO, "block all success");
  }
  return ret;
}

int ObLSTxService::kill_all_tx(const bool graceful)
{
  int ret = OB_SUCCESS;
  bool unused_is_all_tx_clean_up = false;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->kill_all_tx(graceful, unused_is_all_tx_clean_up))) {
    TRANS_LOG(WARN, "kill_all_tx failed");
  } else {
    TRANS_LOG(INFO, "kill_all_tx success");
  }
  return ret;
}

int ObLSTxService::check_modify_schema_elapsed(const ObTabletID &tablet_id,
                                               const int64_t schema_version,
                                               ObTransID &block_tx_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid()) ||
             OB_UNLIKELY(schema_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(tablet_id), K(schema_version));
  } else if (OB_FAIL(mgr_->check_modify_schema_elapsed(tablet_id,
                                                       schema_version,
                                                       block_tx_id))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "check modify schema elapsed failed", K(ret),
                K(tablet_id), K(schema_version));
    }
  } else {
    // do nothing
  }
  return ret;
}

int ObLSTxService::check_modify_time_elapsed(const ObTabletID &tablet_id,
                                             const int64_t timestamp,
                                             ObTransID &block_tx_id)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_UNLIKELY(!tablet_id.is_valid()) ||
             OB_UNLIKELY(timestamp < 0)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(tablet_id), K(timestamp));
  } else if (OB_FAIL(mgr_->check_modify_time_elapsed(tablet_id,
                                                     timestamp,
                                                     block_tx_id))) {
    if (OB_EAGAIN != ret) {
      TRANS_LOG(WARN, "check modify time elapsed failed", K(ret),
                K(tablet_id), K(timestamp));
    }
  } else {
    // do nothing
  }
  return ret;
}

int ObLSTxService::iterate_tx_obj_lock_op(ObLockOpIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->iterate_tx_obj_lock_op(iter))) {
    TRANS_LOG(WARN, "get tx obj lock op iter failed", K(ret));
  } else if (OB_FAIL(iter.set_ready())) {
    TRANS_LOG(WARN, "iter set ready failed", K(ret));
  } else {
    TRANS_LOG(INFO, "iter set ready success", K(ret));
  }
  return ret;
}

int ObLSTxService::iterate_tx_ctx(ObLSTxCtxIterator &iter) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(iter.set_ready(mgr_))) {
    TRANS_LOG(WARN, "get tx obj lock op iter failed", K(ret));
  } else {
    TRANS_LOG(INFO, "iter set ready success", K(ret));
  }
  return ret;
}

int ObLSTxService::replay(const void *buffer,
                          const int64_t nbytes,
                          const palf::LSN &lsn,
                          const SCN &scn)
{
  int ret = OB_SUCCESS;
  logservice::ObLogBaseHeader base_header;
  int64_t tmp_pos = 0;
  const char *log_buf = static_cast<const char *>(buffer);
  if (OB_ISNULL(parent_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid arguments", KP(parent_));
  } else if (OB_FAIL(base_header.deserialize(log_buf, nbytes, tmp_pos))) {
    LOG_WARN("log base header deserialize error", K(ret));
  } else if (OB_FAIL(ObTxReplayExecutor::execute(parent_, this, log_buf, nbytes,
                                                 tmp_pos, lsn, scn,
                                                 base_header))) {
    LOG_WARN("replay tx log error", K(ret), K(lsn), K(scn));
  }
  return ret;
}

int ObLSTxService::traverse_trans_to_submit_redo_log(ObTransID &fail_tx_id, const uint32_t freeze_clock)
{
  return mgr_->traverse_tx_to_submit_redo_log(fail_tx_id, freeze_clock);
}
int ObLSTxService::traverse_trans_to_submit_next_log() { return mgr_->traverse_tx_to_submit_next_log(); }


ObITxLogAdapter *ObLSTxService::get_tx_ls_log_adapter() { return mgr_->get_ls_log_adapter(); }

void ObLSTxService::deactivate()
{
}

int ObLSTxService::activate()
{
  return OB_SUCCESS;
}

inline
void get_min_rec_scn_common_checkpoint_type_by_index_(int index,
                                                      char *common_checkpoint_type)
{
  int ret = OB_SUCCESS;
  if (index == 0) {
    strncpy(common_checkpoint_type, "ALL_EMPTY", common::MAX_CHECKPOINT_TYPE_BUF_LENGTH);
  } else if (OB_FAIL(common_checkpoint_type_to_string(ObCommonCheckpointType(index),
                                              common_checkpoint_type,
                                              common::MAX_CHECKPOINT_TYPE_BUF_LENGTH))) {
    TRANS_LOG(WARN, "common_checkpoint_type_to_string failed", K(index), K(ret));
    strncpy(common_checkpoint_type,
            "UNKNOWN_COMMON_CHECKPOINT_TYPE",
            common::MAX_CHECKPOINT_TYPE_BUF_LENGTH);
  }
}

SCN ObLSTxService::get_rec_scn()
{
  SCN min_rec_scn = SCN::max_scn();
  int min_rec_scn_common_checkpoint_type_index = 0;
  char common_checkpoint_type[common::MAX_CHECKPOINT_TYPE_BUF_LENGTH];
  RLockGuard guard(rwlock_);
  for (int i = 1; i < ObCommonCheckpointType::MAX_BASE_TYPE; i++) {
    if (OB_NOT_NULL(common_checkpoints_[i])) {
      SCN rec_scn = common_checkpoints_[i]->get_rec_scn();
      if (rec_scn.is_valid() && rec_scn < min_rec_scn) {
        min_rec_scn = rec_scn;
        min_rec_scn_common_checkpoint_type_index = i;
      }
    }
  }
  get_min_rec_scn_common_checkpoint_type_by_index_(min_rec_scn_common_checkpoint_type_index,
                                                   common_checkpoint_type);

  TRANS_LOG(INFO, "[CHECKPOINT] ObLSTxService::get_rec_scn",
            K(common_checkpoint_type),
            KPC(common_checkpoints_[min_rec_scn_common_checkpoint_type_index]),
            K(min_rec_scn));

  return min_rec_scn;
}

int ObLSTxService::flush(SCN &recycle_scn)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);
  for (int i = 1; i < ObCommonCheckpointType::MAX_BASE_TYPE; i++) {
    // only flush the common_checkpoint that whose clog need recycle
    if (OB_NOT_NULL(common_checkpoints_[i])
        && !common_checkpoints_[i]->is_flushing()
        && recycle_scn >= common_checkpoints_[i]->get_rec_scn()) {
      TRANS_LOG(INFO,
                "common_checkpoints flush",
                K(i),
                K(common_checkpoints_[i]));
      if (OB_SUCCESS != (tmp_ret = common_checkpoints_[i]->flush(recycle_scn))) {
        TRANS_LOG(WARN, "obCommonCheckpoint flush failed", K(tmp_ret), K(common_checkpoints_[i]));
      }
    }
  }
  return ret;
}

int ObLSTxService::flush_ls_inner_tablet(const ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  if (!tablet_id.is_ls_inner_tablet()) {
    TRANS_LOG(INFO, "not a ls inner tablet", KR(ret), K(tablet_id));
  } else {
    for (int i = 1; i < ObCommonCheckpointType::MAX_BASE_TYPE; i++) {
      if (OB_NOT_NULL(common_checkpoints_[i]) && common_checkpoints_[i]->get_tablet_id() == tablet_id &&
          OB_FAIL(common_checkpoints_[i]->flush(SCN::max_scn(), true))) {
        TRANS_LOG(WARN, "obCommonCheckpoint flush failed", KR(ret), KP(common_checkpoints_[i]));
        break;
      }
    }
  }
  return ret;
}

int ObLSTxService::get_common_checkpoint_info(
    ObIArray<ObCommonCheckpointVTInfo> &common_checkpoint_array)
{
  int ret = OB_SUCCESS;
  common_checkpoint_array.reset();
  RLockGuard guard(rwlock_);
  for (int i = 1; i < ObCommonCheckpointType::MAX_BASE_TYPE; i++) {
    ObCommonCheckpoint *common_checkpoint = common_checkpoints_[i];
    if (OB_ISNULL(common_checkpoint)) {
      // ignore ret
      TRANS_LOG(WARN, "the common_checkpoint should not be null", K(i));
    } else {
      ObCommonCheckpointVTInfo info;
      info.rec_scn = common_checkpoint->get_rec_scn(info.tablet_id);
      info.checkpoint_type = i;
      info.is_flushing = common_checkpoint->is_flushing();
      common_checkpoint_array.push_back(info);
    }
  }

  return ret;
}

int ObLSTxService::register_common_checkpoint(const ObCommonCheckpointType &type,
                                              ObCommonCheckpoint* common_checkpoint)
{
  int ret = OB_SUCCESS;

  if (!is_valid_log_base_type(type) || NULL == common_checkpoint) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid arguments", K(ret), K(type), K(common_checkpoint));
  } else {
    WLockGuard guard(rwlock_);
    if (OB_NOT_NULL(common_checkpoints_[type])) {
      STORAGE_LOG(WARN, "repeat register common_checkpoint", K(ret), K(type), K(common_checkpoint));
    } else {
      common_checkpoints_[type] = common_checkpoint;
    }
  }

  return ret;
}

int ObLSTxService::unregister_common_checkpoint(const ObCommonCheckpointType &type,
                                                const ObCommonCheckpoint* common_checkpoint)
{
  int ret = OB_SUCCESS;

  if (!is_valid_log_base_type(type) || OB_ISNULL(common_checkpoint)) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid arguments", K(ret), K(type), K(common_checkpoint));
  } else {
    WLockGuard guard(rwlock_);
    if (OB_ISNULL(common_checkpoints_[type])) {
      // ignore ret
      STORAGE_LOG(WARN, "common_checkpoint is null, no need unregister", K(type),
                  K(common_checkpoint));
    } else if (common_checkpoints_[type] != common_checkpoint) {
      ret = OB_ERR_UNEXPECTED;
      STORAGE_LOG(WARN, "common checkpoint not equal, not unregister", K(type),
                  K(common_checkpoints_[type]), K(common_checkpoint));
    } else {
      common_checkpoints_[type] = nullptr;
    }
  }

  return ret;
}

int ObLSTxService::traversal_flush()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  RLockGuard guard(rwlock_);
  for (int i = 1; i < ObCommonCheckpointType::MAX_BASE_TYPE; i++) {
    if (OB_NOT_NULL(common_checkpoints_[i]) &&
        OB_SUCCESS != (tmp_ret = common_checkpoints_[i]->flush(SCN::max_scn(), false))) {
      TRANS_LOG(WARN, "obCommonCheckpoint flush failed", K(tmp_ret), KP(common_checkpoints_[i]));
    }
  }
  return ret;
}


void ObLSTxService::reset_() {
  WLockGuard guard(rwlock_);
  for (int i = 0; i < ObCommonCheckpointType::MAX_BASE_TYPE; i++) {
    common_checkpoints_[i] = NULL;
  }
}

SCN ObLSTxService::get_ls_weak_read_ts() {
  return parent_->get_ls_wrs_handler()->get_ls_weak_read_ts();
}

ObTxLogCbPoolMgr *ObLSTxService::get_log_cb_pool_mgr()
{
  ObTxLogCbPoolMgr *log_cb_pool_mgr_ptr = nullptr;

  if (OB_ISNULL(mgr_)) {
    log_cb_pool_mgr_ptr = nullptr;
  } else {
    log_cb_pool_mgr_ptr = &mgr_->get_log_cb_pool_mgr();
  }

  return log_cb_pool_mgr_ptr;
}


int ObLSTxService::prepare_offline(const int64_t start_ts)
{
  int ret = OB_SUCCESS;
  const int64_t PRINT_LOG_INTERVAL = 1000 * 1000; // 1s
  const int64_t WAIT_READONLY_REQUEST_US = 60 * 1000 * 1000;
  bool unused_is_all_tx_clean_up = false;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->block_all(unused_is_all_tx_clean_up))) {
    TRANS_LOG(WARN, "block all failed");
  } else if (ObTimeUtility::current_time() > start_ts + WAIT_READONLY_REQUEST_US) {
    // dont care readonly request
  } else {
    const int64_t readonly_request_cnt = mgr_->get_total_active_readonly_request_count();
    if (readonly_request_cnt > 0) {
      ret = OB_EAGAIN;
      if (REACH_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
        TRANS_LOG(WARN, "readonly requests are active", K(ret), KP(mgr_), K(readonly_request_cnt));
      }
    }
  }
  TRANS_LOG(INFO, "prepare offline ls", K(ret), K(start_ts), KP(mgr_));
  return ret;
}

int ObLSTxService::offline()
{
  int ret = OB_SUCCESS;
  const int64_t PRINT_LOG_INTERVAL = 1000 * 1000; // 1s
  const bool graceful = false;
  bool unused_is_all_tx_clean_up = false;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->block_all(unused_is_all_tx_clean_up))) {
    TRANS_LOG(WARN, "block all failed");
  } else if (OB_FAIL(mgr_->kill_all_tx(graceful, unused_is_all_tx_clean_up))) {
    TRANS_LOG(WARN, "kill_all_tx failed");
  } else if (mgr_->get_tx_ctx_count() > 0) {
    ret = OB_EAGAIN;
    if (REACH_TIME_INTERVAL(PRINT_LOG_INTERVAL)) {
      TRANS_LOG(WARN, "transaction not empty, try again", K(ret), KP(mgr_), K(mgr_->get_tx_ctx_count()));
    }
  }
  return ret;
}

int ObLSTxService::online()
{
  int ret = OB_SUCCESS;
  // need reset block.
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else if (OB_FAIL(mgr_->online())) {
    TRANS_LOG(WARN, "ls tx service online failed", K(ret));
  } else {
    // do nothing
  }
  return ret;
}



int ObLSTxService::get_tx_ctx_count(int64_t &tx_ctx_count)
{
  int ret = OB_SUCCESS;
  tx_ctx_count = -1;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else {
    tx_ctx_count = mgr_->get_tx_ctx_count();
  }
  return ret;
}

int ObLSTxService::get_active_tx_count(int64_t &active_tx_count)
{
  int ret = OB_SUCCESS;
  active_tx_count = -1;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else {
    active_tx_count = mgr_->get_active_tx_count();
  }
  return ret;
}

int ObLSTxService::print_all_tx_ctx(const int64_t print_num)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else {
    const bool verbose = true;
    mgr_->print_all_tx_ctx(print_num, verbose);
  }
  return ret;
}

int ObLSTxService::check_tx_blocked(bool &tx_blocked) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    TRANS_LOG(WARN, "not init", KR(ret));
  } else {
    tx_blocked = mgr_->is_tx_blocked();
  }
  return ret;
}
} // transaction
} // oceanbase
