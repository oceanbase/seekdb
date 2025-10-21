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

#include "ob_tx_log_adapter.h"
#include "src/storage/ls/ob_ls.h"

namespace oceanbase
{
using namespace share;
namespace transaction
{

void ObLSTxLogAdapter::reset()
{
  log_handler_ = nullptr;
  tx_table_ = nullptr;
}

int ObLSTxLogAdapter::init(ObITxLogParam *param, ObTxTable *tx_table)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(param) || OB_NOT_NULL(log_handler_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid arguments", KR(ret), KP(param), KP(log_handler_));
  } else {
    ObTxPalfParam *palf_param = static_cast<ObTxPalfParam *>(param);
    log_handler_ = palf_param->get_log_handler();
    tx_table_ = tx_table;
  }
  return ret;
}

int ObLSTxLogAdapter::submit_log(const char *buf,
                                 const int64_t size,
                                 const SCN &base_scn,
                                 ObTxBaseLogCb *cb,
                                 const bool need_nonblock,
                                 const int64_t retry_timeout_us)
{
  int ret = OB_SUCCESS;
  palf::LSN lsn;
  SCN scn;
  int64_t cur_ts = ObTimeUtility::current_time();
  int64_t retry_cnt = 0;
  const bool is_big_log = (size > palf::MAX_NORMAL_LOG_BODY_SIZE);
  const bool allow_compression = true;

  if (base_scn.convert_to_ts() > cur_ts + 86400000000L) {
    // only print error log
    if (REACH_TIME_INTERVAL(1000000)) {
      TRANS_LOG(ERROR, "base scn is too large", K(base_scn));
    }
  }
  if (NULL == buf || 0 >= size || OB_ISNULL(cb) || !base_scn.is_valid()
      || retry_timeout_us < 0 || size > palf::MAX_LOG_BODY_SIZE) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(buf), K(size), K(base_scn), KP(cb));
  } else if (OB_ISNULL(log_handler_) || !log_handler_->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(log_handler_));
  } else {
    static const int64_t MAX_SLEEP_US = 100;
    int64_t retry_cnt = 0;
    int64_t sleep_us = 0;
    int64_t expire_us = INT64_MAX;
    bool block_flag = need_nonblock;
    if (retry_timeout_us < INT64_MAX - cur_ts) {
      expire_us = cur_ts + retry_timeout_us;
    }
    if (expire_us == INT64_MAX) {
      block_flag = false;
    }
    do {
      if (is_big_log && OB_FAIL(log_handler_->append_big_log(buf, size, base_scn, block_flag,
                                                              allow_compression, cb, lsn, scn))) {
        TRANS_LOG(WARN, "append big log to palf failed", K(ret), KP(log_handler_), KP(buf), K(size), K(base_scn),
              K(need_nonblock), K(block_flag), K(expire_us), K(is_big_log));
      } else if (!is_big_log && OB_FAIL(log_handler_->append(buf, size, base_scn, block_flag,
                                                         allow_compression, cb, lsn, scn))) {
        TRANS_LOG(WARN, "append log to palf failed", K(ret), KP(log_handler_), KP(buf), K(size), K(base_scn),
              K(need_nonblock), K(block_flag), K(expire_us));
      } else {
        cb->set_base_ts(base_scn);
        cb->set_lsn(lsn);
        cb->set_log_ts(scn);
        cb->set_submit_ts(cur_ts);
        ObTransStatistic::get_instance().add_clog_submit_count(MTL_ID(), 1);
        ObTransStatistic::get_instance().add_trans_log_total_size(MTL_ID(), size);
      }
      if (!need_nonblock) {
        // retries are not needed in block mode.
        break;
      } else if (OB_EAGAIN == ret) {
        retry_cnt++;
        sleep_us = retry_cnt * 10;
        sleep_us = sleep_us > MAX_SLEEP_US ? MAX_SLEEP_US : sleep_us;
        ob_usleep(sleep_us);
        cur_ts = ObTimeUtility::current_time();
      }
    } while (OB_EAGAIN == ret && cur_ts < expire_us);
    
  }
  TRANS_LOG(DEBUG, "ObLSTxLogAdapter::submit_ls_log", KR(ret), KP(cb));

  return ret;
}

int ObLSTxLogAdapter::get_role(bool &is_leader, int64_t &epoch)
{
  int ret = OB_SUCCESS;

  ObRole role = INVALID_ROLE;
  if (OB_ISNULL(log_handler_)) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", KR(ret), KP(log_handler_));
  } else if (OB_FAIL(log_handler_->get_role(role, epoch))) {
    if (ret == OB_NOT_INIT || ret == OB_NOT_RUNNING) {
      ret = OB_SUCCESS;
      is_leader = false;
    } else {
      TRANS_LOG(WARN, "get role failed", K(ret));
    }
  } else if (LEADER == role) {
    is_leader = true;
  } else {
    is_leader = false;
  }

  return ret;
}

int ObLSTxLogAdapter::get_max_decided_scn(SCN &scn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(log_handler_) || !log_handler_->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(log_handler_));
  } else {
    ret = log_handler_->get_max_decided_scn(scn);
  }
  return ret;
}

int ObLSTxLogAdapter::get_palf_committed_max_scn(share::SCN &scn) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(log_handler_) || !log_handler_->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(log_handler_));
  } else if (OB_FAIL(log_handler_->get_max_decided_scn_as_leader(scn))) {
    TRANS_LOG(WARN, "get palf committed_max_scn fail", K(ret));
  } else if (!scn.is_valid_and_not_min()) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "get an invalid scn", K(ret), K(scn));
  }
  return ret;
}

int ObLSTxLogAdapter::get_append_mode_initial_scn(share::SCN &ref_scn)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(log_handler_) || !log_handler_->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid argument", K(ret), KP(log_handler_));
  } else {
    ret = log_handler_->get_append_mode_initial_scn(ref_scn);
  }
  return ret;
}

}
}
