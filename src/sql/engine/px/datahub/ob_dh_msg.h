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

#ifndef _OB_SQL_ENGINE_PX_DATAHUB_DH_MSG_H__
#define _OB_SQL_ENGINE_PX_DATAHUB_DH_MSG_H__

#include "sql/dtl/ob_dtl_msg.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/sort/ob_sort_basic_info.h"
#include "sql/engine/ob_io_event_observer.h"
#include "sql/engine/px/ob_dfo.h"

namespace oceanbase
{
namespace sql
{

/* base header for all piece and whole msgs*/

template <dtl::ObDtlMsgType T>
class ObDatahubPieceMsg : public dtl::ObDtlMsgTemp<T>
{
  OB_UNIS_VERSION_V(1);
public:
  ObDatahubPieceMsg()
      : op_id_(common::OB_INVALID_ID),
        source_dfo_id_(common::OB_INVALID_ID),
        target_dfo_id_(common::OB_INVALID_ID),
        thread_id_(0),
        piece_count_(0),
        child_dfo_(nullptr)
    {}
  virtual ~ObDatahubPieceMsg() = default;
  int assign(const ObDatahubPieceMsg<T> &other) {
    op_id_ = other.op_id_;
    source_dfo_id_ = other.source_dfo_id_;
    target_dfo_id_ = other.target_dfo_id_;
    thread_id_ = other.thread_id_;
    piece_count_ = other.piece_count_;
    return OB_SUCCESS;
  }

  /*
    This interface is for merge all piece msgs in one sqc into a big piece
          SQC
    worker 1  piece ---\
                        \         rpc
                       big piece -----> QC
                        /
    worker 2  piece ---/
    ....              /
    worker N  piece -/
   */
  virtual int aggregate_piece(const dtl::ObDtlMsg &other_piece) { return OB_NOT_IMPLEMENT; }
  VIRTUAL_TO_STRING_KV(K_(op_id), K_(source_dfo_id), K_(thread_id), K_(target_dfo_id),
                       K_(piece_count));
  uint64_t op_id_;   // In piece message processing, used for addressing QC end ctx
  /*
              piece     whole
      SRC_DFO  --->  QC  ---> TGT_DFO
      we need use source dfo id to add up piece count
      use target dfo id to find SQC gateway
      for most DH msg type, src is same with tgt
      but for init_channel msg, src is different from tgt
  */
  uint64_t source_dfo_id_;
  uint64_t target_dfo_id_;
  uint64_t thread_id_; // debug
  uint64_t piece_count_; //record piece count
  ObDfo *child_dfo_;
};

template <dtl::ObDtlMsgType T>
class ObDatahubWholeMsg : public dtl::ObDtlMsgTemp<T>
{
  OB_UNIS_VERSION_V(1);
public:
  ObDatahubWholeMsg() : op_id_(common::OB_INVALID_ID) {}
  virtual ~ObDatahubWholeMsg() = default;

  /*
    This interface is for merge all piece msgs to a whole if there is only one sqc.
            SQC
    worker 1  piece ---\
                        \
                        whole
                        /
    worker 2  piece ---/
    ....              /
    worker N  piece -/
   */
  virtual int aggregate_piece(const dtl::ObDtlMsg &piece) { return OB_NOT_IMPLEMENT; }
  virtual int after_aggregate_piece() { return OB_NOT_IMPLEMENT; }
  VIRTUAL_TO_STRING_KV(K_(op_id));
  uint64_t op_id_;   // In whole message processing, used for addressing SQC end msg provider
};

OB_SERIALIZE_MEMBER_TEMP(template<dtl::ObDtlMsgType T>, ObDatahubWholeMsg<T>,
                         op_id_);

OB_DEF_SERIALIZE(ObDatahubPieceMsg<T>, template<dtl::ObDtlMsgType T>)
{
  int ret = common::OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              op_id_,
              source_dfo_id_,
              thread_id_,
              piece_count_,
              target_dfo_id_);
  return ret;
}

OB_DEF_DESERIALIZE(ObDatahubPieceMsg<T>, template<dtl::ObDtlMsgType T>)
{
  int ret = common::OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              op_id_,
              source_dfo_id_,
              thread_id_,
              piece_count_);
  // for compat
  target_dfo_id_ = source_dfo_id_;
  LST_DO_CODE(OB_UNIS_DECODE,
              target_dfo_id_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDatahubPieceMsg<T>, template<dtl::ObDtlMsgType T>)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              op_id_,
              source_dfo_id_,
              thread_id_,
              piece_count_,
              target_dfo_id_);
  return len;
}


}
}
#endif /* _OB_SQL_ENGINE_PX_DATAHUB_DH_MSG_H__ */
//// end of header file

