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

#include "storage/tx/ob_tx_ctx.h"

#include "lib/allocator/ob_malloc.h"

namespace oceanbase
{
namespace transaction
{

namespace
{

int alloc_log_cb(ObTxCtx *tx_ctx, ObTxLogCb *&log_cb)
{
  int ret = OB_SUCCESS;
  ObTxLogCb *new_log_cb = nullptr;

  if (OB_ISNULL(tx_ctx) || OB_NOT_NULL(log_cb)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument for allocating log callback", K(ret), KPC(tx_ctx),
              KPC(log_cb));
  } else if (OB_ISNULL(new_log_cb = OB_NEW(ObTxLogCb, "TxLogCb"))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    TRANS_LOG(WARN, "allocate log callback failed", K(ret), KPC(tx_ctx));
  } else if (OB_FAIL(new_log_cb->init(tx_ctx))) {
    TRANS_LOG(WARN, "initialize log callback failed", K(ret), KPC(new_log_cb), KPC(tx_ctx));
  } else {
    log_cb = new_log_cb;
    new_log_cb = nullptr;
  }

  if (OB_NOT_NULL(new_log_cb)) {
    OB_DELETE(ObTxLogCb, "TxLogCb", new_log_cb);
  }

  return ret;
}

void free_log_cb(ObTxLogCb *&log_cb)
{
  if (OB_NOT_NULL(log_cb)) {
    OB_DELETE(ObTxLogCb, "TxLogCb", log_cb);
    log_cb = nullptr;
  }
}

} // namespace

int ObTxCtx::init_log_cbs_(const ObTransID &tx_id)
{
  int ret = OB_SUCCESS;

  if (final_log_cb_.is_busy() || !busy_cbs_.is_empty()) {
    ret = OB_NEED_WAIT;
    TRANS_LOG(WARN, "log callback is still busy", K(ret), K(tx_id), K(final_log_cb_),
              K(busy_cbs_.get_size()));
  } else {
    reset_log_cbs_();
    if (OB_FAIL(final_log_cb_.init(this))) {
      TRANS_LOG(WARN, "initialize final log callback failed", K(ret), K(tx_id), KPC(this));
    }
  }

  return ret;
}

void ObTxCtx::reset_log_cbs_()
{
  ObTxLogCb *allocated_log_cbs = nullptr;

  {
    ObSpinLockGuard guard(log_cb_lock_);
    free_cbs_.clear();
    busy_cbs_.clear();
    allocated_log_cbs = allocated_log_cb_head_;
    allocated_log_cb_head_ = nullptr;
  }

  while (OB_NOT_NULL(allocated_log_cbs)) {
    ObTxLogCb *next_log_cb = allocated_log_cbs->get_next_allocated_cb();
    free_log_cb(allocated_log_cbs);
    allocated_log_cbs = next_log_cb;
  }
  final_log_cb_.reset();
}

int ObTxCtx::prepare_log_cb_(const bool need_final_cb, ObTxLogCb *&log_cb)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_log_cb_(need_final_cb, log_cb)) && REACH_TIME_INTERVAL(100 * 1000)) {
    TRANS_LOG(WARN, "failed to get log_cb", KR(ret), K(*this));
  }
  return ret;
}

int ObTxCtx::get_log_cb_(const bool need_final_cb, ObTxLogCb *&log_cb)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  if (OB_NOT_NULL(log_cb)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid log cb", K(ret), K(need_final_cb), KP(log_cb), K(trans_id_));
  } else {
    if (need_final_cb && !final_log_cb_.is_busy()) {
      log_cb = &final_log_cb_;
    }

    if (OB_ISNULL(log_cb)) {
      bool need_alloc = false;
      int64_t busy_cbs_cnt = 0;
      int64_t free_cbs_cnt = 0;
      const int64_t trx_max_log_cb_limit = GCONF._trx_max_log_cb_limit;
      {
        ObSpinLockGuard guard(log_cb_lock_);
        log_cb = free_cbs_.remove_first();
        free_cbs_cnt = free_cbs_.get_size();
        if (OB_ISNULL(log_cb)) {
          busy_cbs_cnt = busy_cbs_.get_size();
          need_alloc = busy_cbs_cnt < trx_max_log_cb_limit || trx_max_log_cb_limit <= 0;
          if (!need_alloc && EXECUTE_COUNT_PER_SEC(10)) {
            TRANS_LOG(INFO, "the configured limit of log callbacks has been reached", K(ret),
                      K(trans_id_), K(busy_cbs_cnt), K(trx_max_log_cb_limit),
                      K(free_cbs_cnt));
          }
        }
      }

      ObTxLogCb *new_log_cb = nullptr;
      if (need_alloc) {
        // Allocator tail latency must not extend the log callback spin-lock hold time.
        if (OB_TMP_FAIL(alloc_log_cb(this, new_log_cb))) {
        }

        {
          ObSpinLockGuard guard(log_cb_lock_);
          if (OB_ISNULL(log_cb)) {
            log_cb = free_cbs_.remove_first();
          }
          busy_cbs_cnt = busy_cbs_.get_size();
          if (OB_ISNULL(log_cb)
              && OB_NOT_NULL(new_log_cb)
              && (busy_cbs_cnt < trx_max_log_cb_limit || trx_max_log_cb_limit <= 0)) {
            new_log_cb->set_next_allocated_cb(allocated_log_cb_head_);
            allocated_log_cb_head_ = new_log_cb;
            log_cb = new_log_cb;
            new_log_cb = nullptr;
          }
          free_cbs_cnt = free_cbs_.get_size();
        }

        // A concurrent return may make the speculative allocation unnecessary.
        // Release it after leaving log_cb_lock_.
        free_log_cb(new_log_cb);
      }

      if (OB_ISNULL(log_cb)) {
        ret = OB_TX_NOLOGCB;
        TRANS_LOG(WARN, "no free callback in transaction", KR(ret), K(tmp_ret),
                  K(free_cbs_cnt), K(busy_cbs_cnt), K(*this));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_ISNULL(log_cb)) {
        ret = OB_ERR_UNEXPECTED;
        TRANS_LOG(WARN, "unexpected log callback", K(ret), K(trans_id_), KPC(log_cb));
      } else {
        log_cb->reuse();
        log_cb->set_busy();
      }
    }
  }

  return ret;
}

int ObTxCtx::return_redo_log_cb(ObTxLogCb *log_cb)
{
  return return_log_cb_(log_cb);
}

int ObTxCtx::return_log_cb_(ObTxLogCb *log_cb, bool release_final_cb)
{
  int ret = OB_SUCCESS;

  UNUSED(release_final_cb);

  if (OB_NOT_NULL(log_cb)) {
    const bool is_final_log_cb = (&final_log_cb_ == log_cb);
    log_cb->reuse();
    if (!is_final_log_cb) {
      ObSpinLockGuard guard(log_cb_lock_);
      free_cbs_.add_first(log_cb);
    }
  }

  return ret;
}

} // namespace transaction
} // namespace oceanbase
