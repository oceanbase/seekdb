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

#ifndef OB_DTL_CHANNEL_AGENT_H_
#define OB_DTL_CHANNEL_AGENT_H_

#include "ob_dtl_buf_allocator.h"
#include "sql/dtl/ob_dtl_msg.h"
#include "sql/dtl/ob_dtl_basic_channel.h"
#include "sql/dtl/ob_dtl_local_channel.h"

namespace oceanbase {
namespace common {
class ObNewRow;
}
namespace sql {
namespace dtl {

class ObDtlLocalChannel;

class ObDtlBufEncoder
{
public:
  ObDtlBufEncoder()
  : buffer_(nullptr),
    msg_writer_(nullptr)
  {}
  ~ObDtlBufEncoder() {}
  int switch_writer(const ObDtlMsg &msg);
  int need_new_buffer(
    const ObDtlMsg &msg, ObEvalCtx *eval_ctx, int64_t &need_size, bool &need_new);
  int write_data_msg(const ObDtlMsg &msg, ObEvalCtx *eval_ctx, bool is_eof);
  int set_new_buffer(ObDtlLinkedBuffer *buffer) {
    buffer_ = buffer;
    return msg_writer_->init(buffer_);
  }
  void reset_writer()
  {
    msg_writer_->reset();
  }
  int serialize() {
    int ret = OB_SUCCESS;
    if (CHUNK_DATUM_WRITER != msg_writer_->type()) {
      ret = msg_writer_->serialize();
    }
    if (OB_SUCC(ret)) {
      buffer_->pos() = msg_writer_->used();
    }
    return ret;
  }
  void write_msg_type(ObDtlLinkedBuffer* buffer)
  { msg_writer_->write_msg_type(buffer); }
  ObDtlLinkedBuffer *get_buffer() { return buffer_; }
private:
  ObDtlLinkedBuffer *buffer_;
  ObDtlControlMsgWriter ctl_msg_writer_;
  ObDtlRowMsgWriter row_msg_writer_;
  ObDtlDatumMsgWriter datum_msg_writer_;
  ObDtlChannelEncoder *msg_writer_;
};

class ObDtlChanAgent
{
  const static int64_t BROADCAST_CH_IDX = 0;
public:
  ObDtlChanAgent() : local_channels_(),
  bcast_channel_(nullptr), current_buffer_(nullptr), dtl_buf_encoder_(), dtl_buf_allocator_(),
  dfo_key_(), sys_dtl_buf_size_(0)
    {};
  virtual ~ObDtlChanAgent() = default;
  int broadcast_row(const ObDtlMsg &msg, ObEvalCtx *eval_ctx = nullptr, bool is_eof = false);
  int flush();
  int init(dtl::ObDtlFlowControl &dfc,
           common::ObIArray<ObDtlChannel *> &channels,
           int64_t timeout_ts);
  int destroy();
private:
  int switch_buffer(int64_t need_size);
  int send_last_buffer(ObDtlLinkedBuffer *&last_buffer);
  int inner_broadcast_row(const ObDtlMsg &msg, ObEvalCtx *eval_ctx, bool is_eof);
private:
  // all local channel in this sqc.
  common::ObArray<ObDtlLocalChannel *> local_channels_;
  // the represent channel use to allocate buf from data manager.
  ObDtlBasicChannel *bcast_channel_;
  // the buffer we are now write on.
  ObDtlLinkedBuffer *current_buffer_;
  // use to encoder msg.
  ObDtlBufEncoder dtl_buf_encoder_;
  // warpper of dtl mem manager.
  ObDtlBufAllocator dtl_buf_allocator_;
  // dfo infomation.
  ObDtlDfoKey dfo_key_;
  // sys config, default value is 64K.
  int64_t sys_dtl_buf_size_;
};

}
}
}

#endif
