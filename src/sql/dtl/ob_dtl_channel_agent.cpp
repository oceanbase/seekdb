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
      } else if (DtlWriterType::VECTOR_FIXED_WRITER == msg_writer_map[px_row.get_data_type()]) {
        vector_fixed_msg_writer_.set_size_per_buffer(size_per_buffer_);
        msg_writer_ = &vector_fixed_msg_writer_;
      } else if (DtlWriterType::VECTOR_ROW_WRITER == msg_writer_map[px_row.get_data_type()]) {
        vector_row_msg_writer_.set_row_meta(meta_);
        msg_writer_ = &vector_row_msg_writer_;
      } else if (DtlWriterType::VECTOR_WRITER == msg_writer_map[px_row.get_data_type()]) {
        //TODO : support local channel shuffle in vector mode
        msg_writer_ = &vector_row_msg_writer_;
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
  } else {
// #ifndef NDEBUG
    // if (msg.is_data_msg() && msg_writer_->type() != DtlWriterType::VECTOR_ROW_WRITER) {
    //   const ObPxNewRow &px_row = static_cast<const ObPxNewRow&>(msg);
    //   if (msg_writer_map[px_row.get_data_type()] != msg_writer_->type()) {
    //     ret = OB_ERR_UNEXPECTED;
    //   }
    // } else {
    //   if (msg_writer_map[msg.get_type()] != msg_writer_->type()) {
    //     ret = OB_ERR_UNEXPECTED;
    //   }
    // }
// #endif
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

int ObDtlChanAgent::init(dtl::ObDtlFlowControl &dfc,
                         ObIArray<ObDtlChannel *> &channels,
                         int64_t time_ts)
{
  int ret = OB_SUCCESS;
  
  dtl_buf_allocator_.set_timeout_ts(time_ts);
  
  sys_dtl_buf_size_ = GCONF.dtl_buffer_size;
  dfo_key_ = dfc.get_dfo_key();

  for (int64_t i = 0; i < channels.count() && OB_SUCC(ret); ++i) {
    ObDtlBasicChannel *data_ch = (ObDtlBasicChannel*)channels.at(i);
    int64_t sys_buffer_size = data_ch->get_send_buffer_size();
    dtl_buf_allocator_.set_sys_buffer_size(sys_buffer_size);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(local_channels_.push_back((ObDtlLocalChannel *)data_ch))) {
      LOG_WARN("failed to add local channel", K(ret));
    } else {
      LOG_DEBUG("local broadcast channel", KP(data_ch->get_id()));
    }
  }

  if (OB_SUCC(ret)) {
    if (!local_channels_.empty()) {
      bcast_channel_ = local_channels_.at(BROADCAST_CH_IDX);
    }
  }

  LOG_TRACE("initialized local broadcast channels", K(local_channels_.count()), KP(bcast_channel_));
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
  return ret;
}
