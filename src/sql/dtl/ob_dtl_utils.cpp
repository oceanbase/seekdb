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

#define USING_LOG_PREFIX SQL_DTL

#include <algorithm>
#include "ob_dtl_utils.h"
#include "ob_dtl_flow_control.h"
#include "share/config/ob_server_config.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace sql {
namespace dtl {

int ObDtlAsynSender::calc_batch_buffer_cnt(int64_t &max_batch_size, int64_t &max_loop_cnt)
{
  int ret = OB_SUCCESS;
  if (channels_.empty()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot batch an empty local channel set", K(ret));
  } else {
    const int64_t queue_size =
        common::ObServerConfig::get_instance().server_task_queue_size;
    const int64_t max_buffer_cnt = std::max<int64_t>(1, (queue_size + 1) / 4);
    max_loop_cnt = channels_.count();
    max_batch_size = std::min(max_loop_cnt, max_buffer_cnt);
    LOG_DEBUG("calc local channel batch size", K(max_batch_size), K(max_loop_cnt),
              K(max_buffer_cnt), K(lbt()));
  }
  return ret;
}

int ObDtlAsynSender::syn_send()
{
  int ret = OB_SUCCESS;
  dtl::ObDtlChannel *ch = NULL;
  for (int64_t slice_idx = 0; (OB_SUCCESS == ret) && slice_idx < channels_.count(); ++slice_idx) {
    if (NULL == (ch = channels_.at(slice_idx))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected NULL ptr", K(ret));
    } else if (OB_FAIL(action(ch))) {
      LOG_WARN("failed to send message", K(ret));
    } else if (OB_FAIL(ch->wait_response())) {
      LOG_WARN("failed to wait response", K(ret));
    }
  }
  return ret;
}

int ObDtlAsynSender::asyn_send()
{
  int ret = OB_SUCCESS;
  int64_t max_batch_size = 0;
  int64_t max_loop_times = 0;
  LOG_TRACE("Send eof/drain row", "ch_cnt", channels_.count(), K(ret));
  if (0 == channels_.count()
      || OB_FAIL(calc_batch_buffer_cnt(max_batch_size, max_loop_times))) {
    if (OB_FAIL(syn_send())) {
      LOG_WARN("failed to syn send message", K(ret));
    }
    LOG_TRACE("failed to calc batch buffer cnt", K(ret));
  } else {
    dtl::ObDtlChannel *ch = NULL;
    int tmp_ret = OB_SUCCESS;
    ObArray<ObDtlChannel*> wait_channels;
    if (OB_FAIL(wait_channels.prepare_allocate(max_batch_size))) {
      LOG_WARN("fail alloc memory", K(max_batch_size), K(ret));
    }
    int64_t send_eof_cnt = 0;
    for (int64_t loop = 0; loop < max_loop_times && OB_SUCC(ret); loop += max_batch_size) {
      ch = nullptr;
      int64_t nth_ch = 0;
      for (int64_t batch_idx = 0;
           batch_idx < max_batch_size && loop + batch_idx < channels_.count() && OB_SUCC(ret);
           ++batch_idx) {
        if (NULL == (ch = channels_.at(loop + batch_idx))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected NULL ptr", K(ret));
        } else {
          wait_channels.at(nth_ch++) = ch;
          ++send_eof_cnt;
          if (OB_FAIL(action(ch))) {
            tmp_ret = ret;
            ret = OB_SUCCESS;
            LOG_WARN("failed to send", K(ret));
          }
        }
      }
      if (OB_SUCC(ret)) {
        for (int64_t wait = 0; wait < nth_ch && OB_SUCC(ret); wait++) {
          ch = wait_channels.at(wait);
          if (OB_NOT_NULL(ch) && OB_FAIL(ch->flush())) {
            tmp_ret = ret;
            ret = OB_SUCCESS;
            LOG_WARN("failed to wait", K(ret), K(loop), K(max_loop_times), K(max_batch_size),
              K(channels_.count()));
          }
        }
      }
    }
    if (OB_SUCC(ret) && send_eof_cnt != channels_.count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected status: send eof failed", K(ret),
        K(send_eof_cnt), K(channels_.count()), K(max_batch_size), K(max_loop_times));
    }
    if (OB_SUCC(ret) && OB_SUCCESS != tmp_ret) {
      ret = tmp_ret;
    }
  }
  return ret;
}

int ObTransmitEofAsynSender::action(ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  ObPxNewRow px_eof_row;
  px_eof_row.set_eof_row();
  px_eof_row.set_data_type(type_);
  if (OB_FAIL(ch->send(px_eof_row, timeout_ts_, eval_ctx_, true))) {
    LOG_WARN("fail send eof row to slice channel", K(px_eof_row), K(ret));
  } else if (OB_FAIL(ch->flush(true, false))) {
    LOG_WARN("failed to flush send msg", K(px_eof_row), K(ret));
  }
  return ret;
}

int ObDfcDrainAsynSender::action(ObDtlChannel* ch)
{
  int ret = OB_SUCCESS;
  ObDtlDrainMsg drain_msg;
  LOG_TRACE("drain channel", K(ret), KP(ch->get_id()));
  if (OB_FAIL(ch->send(drain_msg, timeout_ts_))) {
    LOG_WARN("failed to push data to channel", K(ret), KP(ch->get_id()));
  } else if (OB_FAIL(ch->flush(true, false))) {
    LOG_WARN("failed to drain msg", K(ret));
  }
  return ret;
}

int ObDfcUnblockAsynSender::action(ObDtlChannel *ch)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(dfc_.notify_channel_unblocking(ch, unblock_cnt_))) {
    LOG_WARN("failed to notify channel unblocking", K(ret));
  }
  return ret;
}

}  // dtl
}  // sql
}  // oceanbase
