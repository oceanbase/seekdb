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
#include "ob_px_coord_msg_proc.h"
#include "sql/engine/px/ob_sqc_ctx.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
// ObDhWholeeMsgProc is only used in this cpp file, so it can be placed here
// Dedicated to processing datahub whole messages logic
template <typename WholeMsg>
class ObDhWholeeMsgProc
{
public:
  ObDhWholeeMsgProc() = default;
  ~ObDhWholeeMsgProc() = default;
  int on_whole_msg(ObSqcCtx &sqc_ctx, dtl::ObDtlMsgType msg_type, const WholeMsg &pkt) const
  {
    int ret = OB_SUCCESS;
    ObPxDatahubDataProvider *p = nullptr;
    if (OB_FAIL(sqc_ctx.get_whole_msg_provider(pkt.op_id_, msg_type, p))) {
      LOG_WARN("fail get whole msg provider", K(ret));
    } else {
      typename WholeMsg::WholeMsgProvider *provider =
          static_cast<typename WholeMsg::WholeMsgProvider *>(p);
      if (OB_FAIL(provider->add_msg(pkt))) {
        LOG_WARN("fail set whole msg to provider", K(ret));
      }
    }
    return ret;
  }
};


int ObPxSubCoordMsgProc::on_transmit_data_ch_msg(
    const ObPxTransmitDataChannelMsg &pkt) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sqc_ctx_.transmit_data_ch_provider_.add_msg(pkt))) {
    LOG_WARN("fail set transmit channel msg to ch provider", K(ret));
  }
  return ret;
}


int ObPxSubCoordMsgProc::on_receive_data_ch_msg(
    const ObPxReceiveDataChannelMsg &pkt) const
{
  int ret = OB_SUCCESS;
  // FIXME:
  // If there are two receives in dfo, the timing of their calls to get the channel is uncertain,
  // Then they might get the wrong channel (got it backwards)
  // To avoid this situation, a shared memory approach is taken, where the receive op itself determines whether the channel belongs to it
  if (OB_FAIL(sqc_ctx_.receive_data_ch_provider_.add_msg(pkt))) {
    LOG_WARN("fail set receive channel msg to ch provider", K(ret));
  }
  return ret;
}
// NOTE: QC, Task can both interrupt SQC, if SQC is in the message receiving process, this method will be called
// If SQC has already left the message receiving process, this method will not be triggered.
int ObPxSubCoordMsgProc::on_interrupted(const ObInterruptCode &ic) const
{
  int ret = OB_SUCCESS;
  sqc_ctx_.interrupted_ = true;
  // Throw error code to main processing routine, end SQC
  ret = ic.code_;
  LOG_TRACE("sqc received a interrupt and throw out of msg proc", K(ic));
  return ret;
}

int ObPxSubCoordMsgProc::on_create_filter_ch_msg(
    const ObPxCreateBloomFilterChannelMsg &pkt) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sqc_ctx_.bf_ch_provider_.add_msg(pkt))) {
    LOG_WARN("fail set transmit channel msg to ch provider", K(ret));
  }
  return ret;
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObBarrierWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObBarrierWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_BARRIER_WHOLE_MSG, pkt);
}
int ObPxSubCoordMsgProc::on_whole_msg(
    const ObWinbufWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObWinbufWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_WINBUF_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObDynamicSampleWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObDynamicSampleWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_DYNAMIC_SAMPLE_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObRollupKeyWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObRollupKeyWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_ROLLUP_KEY_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObRDWFWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObRDWFWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_RANGE_DIST_WF_PIECE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObInitChannelWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObInitChannelWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_INIT_CHANNEL_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObReportingWFWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObReportingWFWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_SECOND_STAGE_REPORTING_WF_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(
    const ObOptStatsGatherWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObOptStatsGatherWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_OPT_STATS_GATHER_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(const SPWinFuncPXWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<SPWinFuncPXWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_SP_WINFUNC_PX_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(const RDWinFuncPXWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<RDWinFuncPXWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_RD_WINFUNC_PX_WHOLE_MSG, pkt);
}

int ObPxSubCoordMsgProc::on_whole_msg(const ObJoinFilterCountRowWholeMsg &pkt) const
{
  ObDhWholeeMsgProc<ObJoinFilterCountRowWholeMsg> proc;
  return proc.on_whole_msg(sqc_ctx_, dtl::DH_JOIN_FILTER_COUNT_ROW_WHOLE_MSG, pkt);
}
