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

void ObTxCtx::reset_log_cbs_()
{
  ObSpinLockGuard guard(log_cb_lock_);
  busy_cbs_.clear();
  allocated_log_cb_count_ = 0;
}

int ObTxCtx::prepare_log_cb_(ObTxLogCb *&log_cb)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_log_cb_(log_cb)) && REACH_TIME_INTERVAL(100 * 1000)) {
    TRANS_LOG(WARN, "failed to get log_cb", KR(ret), K(*this));
  }
  return ret;
}

int ObTxCtx::get_log_cb_(ObTxLogCb *&log_cb)
{
  int ret = OB_SUCCESS;
  ObTxLogCb *new_log_cb = nullptr;
  int64_t allocated_log_cb_count = 0;
  const int64_t trx_max_log_cb_limit = GCONF._trx_max_log_cb_limit;

  if (OB_NOT_NULL(log_cb)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid log cb", K(ret), KP(log_cb), K(trans_id_));
  } else {
    bool can_alloc = false;
    {
      ObSpinLockGuard guard(log_cb_lock_);
      allocated_log_cb_count = allocated_log_cb_count_;
      can_alloc = trx_max_log_cb_limit <= 0
                  || allocated_log_cb_count_ < trx_max_log_cb_limit;
    }

    if (!can_alloc) {
      ret = OB_TX_NOLOGCB;
    } else if (OB_FAIL(alloc_log_cb(this, new_log_cb))) {
    } else {
      {
        ObSpinLockGuard guard(log_cb_lock_);
        allocated_log_cb_count = allocated_log_cb_count_;
        if (trx_max_log_cb_limit > 0
            && allocated_log_cb_count_ >= trx_max_log_cb_limit) {
          ret = OB_TX_NOLOGCB;
        } else {
          ++allocated_log_cb_count_;
          allocated_log_cb_count = allocated_log_cb_count_;
          log_cb = new_log_cb;
          new_log_cb = nullptr;
        }
      }
    }

    // Allocator tail latency must not extend the log callback spin-lock hold time.
    free_log_cb(new_log_cb);

    if (OB_TX_NOLOGCB == ret && EXECUTE_COUNT_PER_SEC(10)) {
      TRANS_LOG(INFO, "the configured limit of log callbacks has been reached", K(ret),
                K(trans_id_), K(allocated_log_cb_count), K(trx_max_log_cb_limit));
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

int ObTxCtx::return_log_cb_(ObTxLogCb *log_cb)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(log_cb)) {
  } else if (log_cb->get_tx_ctx() != this) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "log callback does not belong to this transaction", K(ret), KPC(log_cb),
              KPC(this));
  } else {
    {
      ObSpinLockGuard guard(log_cb_lock_);
      if (allocated_log_cb_count_ <= 0) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        --allocated_log_cb_count_;
      }
    }

    if (OB_FAIL(ret)) {
      TRANS_LOG(ERROR, "invalid allocated log callback count", K(ret), KPC(log_cb), KPC(this));
    } else {
      free_log_cb(log_cb);
    }
  }

  return ret;
}

} // namespace transaction
} // namespace oceanbase
