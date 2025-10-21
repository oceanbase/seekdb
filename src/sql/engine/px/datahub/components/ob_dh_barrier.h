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

#ifndef __OB_SQL_ENG_PX_DH_BARRIER_H__
#define __OB_SQL_ENG_PX_DH_BARRIER_H__

#include "sql/engine/px/datahub/ob_dh_msg.h"
#include "sql/engine/px/datahub/ob_dh_dtl_proc.h"
#include "sql/engine/px/datahub/ob_dh_msg_ctx.h"
#include "sql/engine/px/datahub/ob_dh_msg_provider.h"

namespace oceanbase
{
namespace sql
{

class ObBarrierPieceMsg;
class ObBarrierWholeMsg;
typedef ObPieceMsgP<ObBarrierPieceMsg> ObBarrierPieceMsgP;
typedef ObWholeMsgP<ObBarrierWholeMsg> ObBarrierWholeMsgP;
class ObBarrierPieceMsgListener;
class ObBarrierPieceMsgCtx;
class ObPxCoordInfo;

/* Various datahub subclass message definitions are as follows */
class ObBarrierPieceMsg
  : public ObDatahubPieceMsg<dtl::ObDtlMsgType::DH_BARRIER_PIECE_MSG>

{
  OB_UNIS_VERSION_V(1);
public:
  using PieceMsgListener = ObBarrierPieceMsgListener;
  using PieceMsgCtx = ObBarrierPieceMsgCtx;
public:
  ObBarrierPieceMsg() = default;
  ~ObBarrierPieceMsg() = default;
  void reset()
  {
  }
  INHERIT_TO_STRING_KV("meta", ObDatahubPieceMsg<dtl::ObDtlMsgType::DH_BARRIER_PIECE_MSG>,
                       K_(op_id));
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObBarrierPieceMsg);
};


class ObBarrierWholeMsg
    : public ObDatahubWholeMsg<dtl::ObDtlMsgType::DH_BARRIER_WHOLE_MSG>
{
  OB_UNIS_VERSION_V(1);
public:
  using WholeMsgProvider = ObWholeMsgProvider<ObBarrierPieceMsg, ObBarrierWholeMsg>;
public:
  ObBarrierWholeMsg() : ready_state_(0) {}
  ~ObBarrierWholeMsg() = default;
  int assign(const ObBarrierWholeMsg &other)
  {
    ready_state_ = other.ready_state_;
    return common::OB_SUCCESS;
  }
  void reset()
  {
    ready_state_ = 0;
  }
  VIRTUAL_TO_STRING_KV(K_(ready_state));
  int ready_state_; // placeholder, not actually used
};

class ObBarrierPieceMsgCtx : public ObPieceMsgCtx
{
public:
  ObBarrierPieceMsgCtx(uint64_t op_id, int64_t task_cnt, int64_t timeout_ts)
    : ObPieceMsgCtx(op_id, task_cnt, timeout_ts), received_(0) {}
  ~ObBarrierPieceMsgCtx() = default;
  virtual int send_whole_msg(common::ObIArray<ObPxSqcMeta> &sqcs) override;
  virtual void reset_resource() override;
  static int alloc_piece_msg_ctx(const ObBarrierPieceMsg &pkt,
                                 ObPxCoordInfo &coord_info,
                                 ObExecContext &ctx,
                                 int64_t task_cnt,
                                 ObPieceMsgCtx *&msg_ctx);
  INHERIT_TO_STRING_KV("meta", ObPieceMsgCtx, K_(received));
  int received_; // number of pieces already received
private:
  DISALLOW_COPY_AND_ASSIGN(ObBarrierPieceMsgCtx);
};

class ObBarrierPieceMsgListener
{
public:
  ObBarrierPieceMsgListener() = default;
  ~ObBarrierPieceMsgListener() = default;
  static int on_message(
      ObBarrierPieceMsgCtx &ctx,
      common::ObIArray<ObPxSqcMeta> &sqcs,
      const ObBarrierPieceMsg &pkt);
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObBarrierPieceMsgListener);
};

}
}
#endif /* __OB_SQL_ENG_PX_DH_BARRIER_H__ */
//// end of header file

