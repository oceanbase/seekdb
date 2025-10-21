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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_px_dtl_proc.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;

// put your code here

int ObPxFinishSqcResultP::process(const ObPxFinishSqcResultMsg &pkt)
{
  return msg_proc_.on_sqc_finish_msg(ctx_, pkt);
}

int ObPxInitSqcResultP::process(const ObPxInitSqcResultMsg &pkt)
{
  return msg_proc_.on_sqc_init_msg(ctx_, pkt);
}

int ObPxQcInterruptedP::process(const ObInterruptCode &pkt)
{
  return msg_proc_.on_interrupted(ctx_, pkt);
}

int ObPxReceiveDataChannelMsgP::process(const ObPxReceiveDataChannelMsg &pkt)
{
  return msg_proc_.on_receive_data_ch_msg(pkt);
}

int ObPxTransmitDataChannelMsgP::process(const ObPxTransmitDataChannelMsg &pkt)
{
  return msg_proc_.on_transmit_data_ch_msg(pkt);
}

int ObPxCreateBloomFilterChannelMsgP::process(const ObPxCreateBloomFilterChannelMsg &pkt)
{
  return msg_proc_.on_create_filter_ch_msg(pkt);
}
int ObPxSqcInterruptedP::process(const ObInterruptCode &pkt)
{
  return msg_proc_.on_interrupted(pkt);
}

int ObPxReceiveRowP::process(const ObDtlLinkedBuffer &buffer, bool &transferred)
{
  int ret = OB_SUCCESS;
  if (NULL == reader_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("reader not set while receive data message", K(ret));
  } else if (OB_FAIL(reader_->add_buffer(const_cast<ObDtlLinkedBuffer &>(buffer), transferred))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("add buffer failed", K(ret));
    }
  }
  return ret;
}

int ObPxInterruptP::process(const ObInterruptCode &ic)
{
  return ic.code_;
}
