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
#include "ob_dtl_channel_group.h"
#include "sql/dtl/ob_dtl.h"

using namespace oceanbase::common;

namespace oceanbase {
namespace sql {
namespace dtl {

/**
 * This interface is similar to make_receive_channel, mainly used for constructing channels after the transmit and receive ends receive channel information
 * The previous make_channel was created at the PX(coord) end, then sent sqc to both the transmit and receive ends,
 * where the transmit and receive ends directly create channel instances based on the created channel information. However, this method has performance issues
 * The algorithm complexity of the previous approach is: transmit_dfo_task_cnt * receive_dfo_task_cnt, assuming dop=512, it would be at least 512*512
 * As dop increases, the time taken becomes longer. See bug
 * New solution:
 *        The new solution no longer constructs all channel specific information at the PX end,
 *        but instead constructs overall channel information at the PX end, sending this overall channel information to all dfo sqcs,
 *        then each (task) worker constructs its own information based on the overall channel information,
 *        replacing the previous make_channel with make_transmit(receive)_channel
 *        That is, the channel provider provides the get_data_ch interface, then transmit and receive construct their own channels respectively
 * Assumption: M: Number of workers in Transmit dfo
 *             N: Number of workers in Receive dfo
 *             start_ch_id: Is the start id of the (M * N) requested channel count
 * For a certain worker, the formula for constructing the channel is approximately:
 * transmit end:
 * i = cur_worker_idx;
 * for (j = 0; j < N; j++) {
 *  chid = start_ch_id + j + i * N;
 * }
 * receive end:
 * i = cur_worker_idx;
 * for (j = 0; j < M; j++) {
 *  chid = start_ch_id + j * N + i;
 * }
 **/
void ObDtlChannelGroup::make_transmit_channel(const uint64_t tenant_id,
                                    const ObAddr &peer_exec_addr,
                                    uint64_t chid,
                                    ObDtlChannelInfo &ci_producer,
                                    bool is_local)
{
  UNUSED(is_local);
  ci_producer.chid_ = chid << 1;
  if (is_local) {
    ci_producer.type_ = DTL_CT_LOCAL;
  } else {
    ci_producer.type_ = DTL_CT_RPC;
  }
  ci_producer.peer_ = peer_exec_addr;
  ci_producer.role_ = DTL_CR_PUSHER;
  ci_producer.tenant_id_ = tenant_id;
}

void ObDtlChannelGroup::make_receive_channel(const uint64_t tenant_id,
                                    const ObAddr &peer_exec_addr,
                                    uint64_t chid,
                                    ObDtlChannelInfo &ci_consumer,
                                    bool is_local)
{
  UNUSED(is_local);
  ci_consumer.chid_ = (chid << 1) + 1;
  if (is_local) {
    ci_consumer.type_ = DTL_CT_LOCAL;
  } else {
    ci_consumer.type_ = DTL_CT_RPC;
  }
  ci_consumer.peer_ = peer_exec_addr;
  ci_consumer.role_ = DTL_CR_PUSHER;
  ci_consumer.tenant_id_ = tenant_id;
}

int ObDtlChannelGroup::make_channel(const uint64_t tenant_id,
                                    const ObAddr &producer_exec_addr,
                                    const ObAddr &consumer_exec_addr,
                                    ObDtlChannelInfo &ci_producer,
                                    ObDtlChannelInfo &ci_consumer)
{
  int ret = OB_SUCCESS;
  const uint64_t chid = ObDtlChannel::generate_id();
  if (producer_exec_addr != consumer_exec_addr) {
    // @TODO: rpc channel isn't supported right now
    ci_producer.chid_ = chid << 1;
    ci_producer.type_ = DTL_CT_RPC;
    ci_producer.peer_ = consumer_exec_addr;
    ci_producer.role_ = DTL_CR_PUSHER;
    ci_producer.tenant_id_ = tenant_id;
    ci_consumer.chid_ = (chid << 1) + 1;
    ci_consumer.type_ = DTL_CT_RPC;
    ci_consumer.peer_ = producer_exec_addr;
    ci_consumer.role_ = DTL_CR_PULLER;
    ci_consumer.tenant_id_ = tenant_id;
  } else {
    // If producer and consumer are in the same execution process, we
    // can use in memory channel.
    ci_producer.chid_ = chid << 1;
    ci_producer.type_ = DTL_CT_LOCAL;
    ci_producer.peer_ = consumer_exec_addr;
    ci_producer.role_ = DTL_CR_PUSHER;
    ci_producer.tenant_id_ = tenant_id;
    ci_consumer.chid_ = (chid << 1) + 1;
    ci_consumer.type_ = DTL_CT_LOCAL;
    ci_consumer.peer_ = producer_exec_addr;
    ci_consumer.role_ = DTL_CR_PULLER;
    ci_consumer.tenant_id_ = tenant_id;
  }
  return ret;
}

int ObDtlChannelGroup::link_channel(const ObDtlChannelInfo &ci, ObDtlChannel *&chan, ObDtlFlowControl *dfc)
{
  int ret = OB_SUCCESS;
  const auto chid = ci.chid_;
  // Flow control can use local, i.e., data channel
  if (nullptr != dfc && ci.type_ == DTL_CT_LOCAL) {
    if (OB_FAIL(DTL.create_local_channel(ci.tenant_id_, ci.chid_, ci.peer_, chan, dfc))) {
      LOG_WARN("create local channel fail", KP(chid), K(ret));
    }
    LOG_TRACE("trace create local channel", KP(chid), K(ret), K(ci.peer_), K(ci.type_));
  } else {
    if (OB_FAIL(DTL.create_rpc_channel(ci.tenant_id_, ci.chid_, ci.peer_, chan, dfc))) {
      LOG_WARN("create rpc channel fail", KP(chid), K(ret), K(ci.peer_));
    }
    LOG_TRACE("trace create rpc channel", KP(chid), K(ret), K(ci.peer_), K(ci.type_));
  }
  return ret;
}

int ObDtlChannelGroup::unlink_channel(const ObDtlChannelInfo &ci)
{
  return DTL.destroy_channel(ci.chid_);
}
// Remove channel from dtl's map
int ObDtlChannelGroup::remove_channel(const ObDtlChannelInfo &ci, ObDtlChannel *&ch)
{
  return DTL.remove_channel(ci.chid_, ch);
}

}  // dtl
}  // sql
}  // oceanbase
