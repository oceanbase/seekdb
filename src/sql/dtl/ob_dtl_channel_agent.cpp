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
#include "ob_dtl_channel_agent.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;

int ObDtlBufEncoder::switch_writer(const ObDtlMsg &msg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == msg_writer_)) {
    if (msg.is_data_msg()) {
      const ObPxNewRow &px_row = static_cast<const ObPxNewRow&>(msg);
      if (DtlWriterType::CHUNK_ROW_WRITER == msg_writer_map[px_row.get_data_type()]) {
        msg_writer_ = &row_msg_writer_;
      } else if (DtlWriterType::CHUNK_DATUM_WRITER == msg_writer_map[px_row.get_data_type()]) {
        msg_writer_ = &datum_msg_writer_;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unkown msg writer", K(msg.get_type()), K(msg_writer_->type()));
      }
      LOG_TRACE("msg writer", K(px_row.get_data_type()), K(msg_writer_->type()));
    } else {
      if (DtlWriterType::CONTROL_WRITER == msg_writer_map[msg.get_type()]) {
        msg_writer_ = &ctl_msg_writer_;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unkown msg writer", K(msg.get_type()), K(msg_writer_->type()));
      }
    }
  }
  return ret;
}

int ObDtlBufEncoder::need_new_buffer(
  const ObDtlMsg &msg, ObEvalCtx *eval_ctx, int64_t &need_size, bool &need_new)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(msg_writer_->need_new_buffer(msg, eval_ctx, need_size, need_new))) {
    LOG_WARN("failed to calc need new buffer", K(ret));
  }
  return ret;
}

int ObDtlBufEncoder::write_data_msg(const ObDtlMsg &msg, ObEvalCtx *eval_ctx, bool is_eof)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(msg_writer_->write(msg, eval_ctx, is_eof))) {
    if (OB_BUF_NOT_ENOUGH != ret) {
      LOG_WARN("failed to add row", K(ret));
    }
  } else {
    LOG_DEBUG("write row", K(ret),
      K(msg_writer_->rows()), K(msg_writer_->used()), KP(buffer_));
    buffer_->pos() = (msg_writer_->rows() > 0 || is_eof) ? msg_writer_->used() : 0;
    buffer_->is_eof() = is_eof;
  }
  return ret;
}

int ObDtlBcastService::send_message(ObDtlLinkedBuffer *&bcast_buf, bool drain)
{
  int ret = OB_SUCCESS;
  /**
   * A broadcast group is shared by the sending channels on the same machine.
   * Assuming three sending channels share this bcast service. In one round of send messages, the three channels must be
   * sending the same message; if they are different messages, an error should be reported.
   * When the first two channels send messages, they are counting and do not actually send data.
   * Data is only sent when the third channel sends it.
   * The action of sending will result in asynchronous responses from all three channels.
   * State changes caused by asynchronous responses to a channel will take effect at the next send action of the channel.
   */
  ObCurTraceId::TraceId *cur_trace_id = NULL;
  if (OB_ISNULL(cur_trace_id = ObCurTraceId::get_trace_id()) || active_chs_count_ < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid trace id / invalid active count", K(ret), K(active_chs_count_));
  } else if (0 == active_chs_count_) {
    // all channel has been drain, do nothing.
  } else if (nullptr == bcast_buf_ && 0 == send_count_) {
    // a new buffer come into this broadcast group.
    bcast_buf_ = bcast_buf;
    send_count_ = bcast_ch_count_ - 1;
    // Here each time msg is sent, active_chs_count_ will be decremented, so it needs to be reset each time
    active_chs_count_ = bcast_ch_count_;
    bcast_buf = nullptr;
    if (drain) {
      // this channel has been drained.
      active_chs_count_--;
    }
  } else if (bcast_buf_ == bcast_buf) {
    send_count_--;
    bcast_buf = nullptr;
    if (drain) {
      // this channel has been drained.
      active_chs_count_--;
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("this channel write a msg to other bcast service", K(bcast_buf), K(bcast_buf_), K(send_count_));
  }
  if (OB_SUCC(ret)) {
    if (0 == send_count_ && active_chs_count_ != 0) {
      // single-replica: broadcast via rpc no longer exists. All dtl channels are
      // local, so no bcast service is ever created and this path is unreachable.
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("dtl rpc broadcast is not supported in single-replica", K(ret));
    } else if (0 == active_chs_count_) {
      bcast_buf_ = nullptr;
    }
  }
  LOG_TRACE("send message", K(ret), K(this), K(bcast_ch_count_), K(send_count_), K(bcast_buf),
    K(bcast_buf_), K(peer_ids_), K(send_count_), K(active_chs_count_));
  return ret;
}

int ObDtlChanAgent::init(dtl::ObDtlFlowControl &dfc,
                         ObPxTaskChSet &task_ch_set,
                         ObIArray<ObDtlChannel *> &channels,
                         int64_t time_ts)
{
  int ret = OB_SUCCESS;
  
  dtl_buf_allocator_.set_timeout_ts(time_ts);
  
  sys_dtl_buf_size_ = GCONF.dtl_buffer_size;
  dfo_key_ = dfc.get_dfo_key();

  if (init_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("this channel agent has been initiated", K(ret));
  }

  for (int64_t i = 0; i < channels.count() && OB_SUCC(ret); ++i) {
    bool find_bc_service = false;

    ObDtlBasicChannel *data_ch = (ObDtlBasicChannel*)channels.at(i);
    int64_t sys_buffer_size = data_ch->get_send_buffer_size();

    ObDtlChannelInfo ch_info;
    if (OB_FAIL(task_ch_set.get_channel_info(i, ch_info))) {
      LOG_WARN("failed to get channel info", K(ret));
    }
    dtl_buf_allocator_.set_sys_buffer_size(sys_buffer_size);
    UNUSED(find_bc_service);
    if (OB_FAIL(ret)) {
    } else if (ObDtlChannel::DtlChannelType::LOCAL_CHANNEL == data_ch->get_channel_type()) {
      if (OB_FAIL(local_channels_.push_back((ObDtlLocalChannel *)data_ch))) {
        LOG_WARN("failed to push back server_ch", K(ret));
      }
      LOG_DEBUG("channel info by server", KP(data_ch->get_id()), K(data_ch->get_channel_type()));
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected channel type", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (!local_channels_.empty()) {
      bcast_channel_ = local_channels_.at(BROADCAST_CH_IDX);
    }
  }

  LOG_TRACE("use shared broadcast msg optimizer", K(bc_services_), K(local_channels_.count()), KP(bcast_channel_->get_id()));
  return ret;
}

int ObDtlChanAgent::inner_broadcast_row(
  const ObDtlMsg &msg, ObEvalCtx *eval_ctx, bool is_eof)
{
  int ret = OB_SUCCESS;
  int64_t need_size = 0;
  bool need_new = false;
  LOG_DEBUG("[DTL BROADCAST] broadcast", K(is_eof), K(msg.get_type()));
  if (OB_FAIL(dtl_buf_encoder_.switch_writer(msg))) {
    LOG_WARN("failed to switch msg writer", K(ret));
  } else if (OB_FAIL(dtl_buf_encoder_.need_new_buffer(msg, eval_ctx, need_size, need_new))) {
    LOG_WARN("failed to calc need new buffer", K(ret));
  } else if (need_new) {
    if (OB_FAIL(switch_buffer(need_size))) {
      LOG_WARN("failed to switch buffer", K(ret));
    } else {
      dtl_buf_encoder_.write_msg_type(current_buffer_);
      current_buffer_->set_data_msg(msg.is_data_msg());
      current_buffer_->is_eof() = is_eof;
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(dtl_buf_encoder_.write_data_msg(msg, eval_ctx, is_eof))) {
      if (OB_BUF_NOT_ENOUGH != ret) {
        LOG_WARN("failed to write msg", K(ret));
        dtl_buf_allocator_.free_buf(*bcast_channel_, current_buffer_);
      }
    }
  }
  return ret;
}

int ObDtlChanAgent::broadcast_row(const ObDtlMsg &msg, ObEvalCtx *eval_ctx, bool is_eof)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(inner_broadcast_row(msg, eval_ctx, is_eof))) {
    if (OB_BUF_NOT_ENOUGH == ret) {
      if (OB_FAIL(inner_broadcast_row(msg, eval_ctx, is_eof))) {
        LOG_WARN("failed to broadcast row", K(ret));
      }
    } else {
      LOG_WARN("failed to broadcast row", K(ret));
    }
  }
  return ret;
}

int ObDtlChanAgent::switch_buffer(int64_t need_size)
{
  int ret = OB_SUCCESS;
  ObDtlBasicChannel *bcast_ch = bcast_channel_;
  ObDtlLinkedBuffer *last_buffer = dtl_buf_encoder_.get_buffer();
  current_buffer_ = dtl_buf_allocator_.alloc_buf(*bcast_ch, std::max(sys_dtl_buf_size_, need_size));
  LOG_DEBUG("[DTL BROADCAST] encoder need a new buffer", KP(bcast_ch->get_id()), K(need_size));
  if (nullptr == current_buffer_) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory", K(ret));
  }

  // send last buffer
  if (OB_SUCC(ret) && OB_NOT_NULL(last_buffer)) {
    if (0 != last_buffer->pos()) {
      if (OB_FAIL(dtl_buf_encoder_.serialize())) {
        LOG_WARN("failed to do serialize", K(ret));
      } else if (OB_FAIL(send_last_buffer(last_buffer))) {
        LOG_WARN("failed to send last buffer", K(ret));
      } else {
        dtl_buf_encoder_.reset_writer();
      }
    } else {
      dtl_buf_allocator_.free_buf(*bcast_ch, last_buffer);
    }
  }

  // set new buffer
  if (OB_SUCC(ret)) {
    current_buffer_->set_bcast();
    dtl_buf_encoder_.set_new_buffer(current_buffer_);
  } else if (nullptr != current_buffer_) {
    dtl_buf_allocator_.free_buf(*bcast_ch, current_buffer_);
  }

  return ret;
}

int ObDtlChanAgent::flush()
{
  int ret = OB_SUCCESS;
  ObDtlLinkedBuffer *last_buffer = dtl_buf_encoder_.get_buffer();
  if (nullptr == current_buffer_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("you should send a row before use this interface", K(ret));
  } else if (last_buffer != current_buffer_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("send buffer must be equal to last buffer", K(ret), K(last_buffer), K(current_buffer_));
  // } else if (OB_FAIL(dtl_buf_encoder_.serialize())) {
  //   LOG_WARN("failed to do serialize", K(ret));
  } else if (OB_FAIL(send_last_buffer(last_buffer))) {
    LOG_WARN("failed to send last buffer", K(ret));
  } else {
    dtl_buf_encoder_.reset_writer();
    current_buffer_ = nullptr;
  }
  return ret;

}

int ObDtlChanAgent::send_last_buffer(ObDtlLinkedBuffer *&last_buffer)
{
  int ret = OB_SUCCESS;
  ObDtlBasicChannel *ch = nullptr;
  last_buffer->set_dfo_key(dfo_key_);
  ObDtlBasicChannel *bcast_ch = bcast_channel_;
  const int64_t size = last_buffer->pos(); // yes, it is pos()
  const int64_t pos = last_buffer->pos();
  for (int64_t i = 0; i < local_channels_.count() && OB_SUCC(ret); ++i) {
    ch = local_channels_.at(i);
    if (!ch->is_drain() || last_buffer->is_eof()) {
      ObDtlLinkedBuffer *buf = dtl_buf_allocator_.alloc_buf(*ch, last_buffer->size());
      if (nullptr == buf) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory", K(ret));
      } else {
        last_buffer->size() = size;
        last_buffer->pos() = pos;
        if (OB_FAIL(ObDtlLinkedBuffer::assign(*last_buffer, buf))) {
          LOG_WARN("failed to assign buffer", K(ret));
        } else if (OB_FAIL(ch->send_buffer(buf))) {
          LOG_WARN("failed to send buffer", K(ret));
        }
        if (nullptr != buf) {
          dtl_buf_allocator_.free_buf(*ch, buf);
        }
      }
    }
  }

  if (nullptr != last_buffer) {
    if (last_buffer == current_buffer_) {
      current_buffer_ = nullptr;
    }
    dtl_buf_allocator_.free_buf(*bcast_ch, last_buffer);
  }
  return ret;
}

int ObDtlChanAgent::destroy()
{
  int ret = OB_SUCCESS;
  if (nullptr != bcast_channel_ && nullptr != current_buffer_) {
    dtl_buf_allocator_.free_buf(*bcast_channel_, current_buffer_);
  }
  for (int64_t i = 0; i < local_channels_.count(); ++i) {
    int temp_ret = local_channels_.at(i)->wait_response();
    if (OB_SUCCESS != temp_ret) {
      ret = temp_ret;
    }
  }
  for (int64_t i = 0; i < bc_services_.count(); ++i) {
    bc_services_.at(i)->~ObDtlBcastService();
  }
  return ret;
}
