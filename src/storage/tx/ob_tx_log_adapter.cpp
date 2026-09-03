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

int ObLSTxLogAdapter::submit_log(palf::PalfLogBuffer &buffer,
                                 const SCN &base_scn,
                                 ObTxBaseLogCb *cb,
                                 const bool need_nonblock,
                                 const int64_t retry_timeout_us)
{
  int ret = OB_SUCCESS;
  palf::LSN lsn;
  SCN scn;
  int64_t cur_ts = ObTimeUtility::current_time();
  if (!buffer.is_valid() || !buffer.is_sealed() || buffer.get_size() <= 0
      || buffer.get_size() > palf::MAX_LOG_BODY_SIZE || OB_ISNULL(cb)
      || !base_scn.is_valid() || retry_timeout_us < 0) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "invalid owned log buffer", K(ret), K(buffer), K(base_scn), KP(cb));
  } else if (OB_ISNULL(log_handler_) || !log_handler_->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    static const int64_t MAX_SLEEP_US = 100;
    int64_t retry_cnt = 0;
    int64_t expire_us = INT64_MAX;
    bool block_flag = need_nonblock;
    if (retry_timeout_us < INT64_MAX - cur_ts) {
      expire_us = cur_ts + retry_timeout_us;
    }
    if (INT64_MAX == expire_us) {
      block_flag = false;
    }
    do {
      if (OB_FAIL(log_handler_->append_owned(buffer, base_scn, block_flag,
                                             cb, lsn, scn))) {
        if (OB_EAGAIN != ret) {
          TRANS_LOG(WARN, "append owned log to palf failed", K(ret), K(base_scn),
              K(need_nonblock), K(block_flag), K(buffer));
        }
      } else {
        cb->set_base_ts(base_scn);
        cb->set_lsn(lsn);
        cb->set_log_ts(scn);
        cb->set_submit_ts(cur_ts);
      }
      if (!need_nonblock) {
        break;
      } else if (OB_EAGAIN == ret && buffer.is_valid()) {
        ++retry_cnt;
        ob_usleep(MIN(retry_cnt * 10, MAX_SLEEP_US));
        cur_ts = ObTimeUtility::current_time();
      }
    } while (OB_EAGAIN == ret && buffer.is_valid() && cur_ts < expire_us);
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
