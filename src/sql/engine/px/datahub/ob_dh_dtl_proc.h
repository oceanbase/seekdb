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

#ifndef _OB_SQL_DH_DTL_PROC_H_
#define _OB_SQL_DH_DTL_PROC_H_

#include "sql/dtl/ob_dtl_processor.h"
#include "sql/engine/px/datahub/ob_dh_msg.h"
#include "sql/engine/px/ob_px_coord_msg_proc.h"

namespace oceanbase
{
namespace sql
{

class ObPxCoordMsgProc;
class ObExecContext;
class ObIPxSubCoordMsgProc;
class ObIPxCoordMsgProc;
////////////////////////////  FOR QC ////////////////////////////

template <typename PieceMsg>
class ObPieceMsgP : public dtl::ObDtlPacketProc<PieceMsg>
{
public:
  ObPieceMsgP(ObExecContext &ctx, ObIPxCoordMsgProc &msg_proc)
      : ctx_(ctx), msg_proc_(msg_proc) {}
  virtual ~ObPieceMsgP() = default;
  int process(const PieceMsg &pkt) override
  {
    // FIXME on_piece_msg theoretically can be template processed.
    // Temporarily bypass by overloading.
    return msg_proc_.on_piece_msg(ctx_, pkt);
  }
private:
  ObExecContext &ctx_;
  ObIPxCoordMsgProc &msg_proc_;
};


////////////////////////////  FOR SQC ////////////////////////////
template <typename WholeMsg>
class ObWholeMsgP : public dtl::ObDtlPacketProc<WholeMsg>
{
public:
  ObWholeMsgP(ObIPxSubCoordMsgProc &msg_proc)
      : msg_proc_(msg_proc) {}
  virtual ~ObWholeMsgP() = default;
  int process(const WholeMsg &pkt) override
  {
    return msg_proc_.on_whole_msg(pkt);
  }
private:
  ObIPxSubCoordMsgProc &msg_proc_;
};

}
}

#endif
