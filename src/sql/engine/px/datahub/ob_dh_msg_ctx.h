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

#ifndef __OB_SQL_ENGINE_PX_DATAHUB_BARRIER_PIECE_MSG_CTX_H__
#define __OB_SQL_ENGINE_PX_DATAHUB_BARRIER_PIECE_MSG_CTX_H__

#include "lib/container/ob_se_array.h"
#include "sql/engine/px/ob_dfo.h"

namespace oceanbase
{
namespace sql
{

class ObPieceMsgCtx
{
public:
  ObPieceMsgCtx(uint64_t op_id, int64_t task_cnt, int64_t timeout_ts)
      : op_id_(op_id), task_cnt_(task_cnt), timeout_ts_(timeout_ts) {}
  virtual ~ObPieceMsgCtx() {}
  virtual int send_whole_msg(common::ObIArray<ObPxSqcMeta> &sqcs) { return OB_SUCCESS; };
  virtual void reset_resource() = 0;
  VIRTUAL_TO_STRING_KV(K_(op_id), K_(task_cnt));
  virtual void destroy() {}
  uint64_t op_id_;    // which operator uses the datahub service
  int64_t task_cnt_;  // The actual number of tasks executed under this dfo, i.e.: the expected number of pieces to be received
  int64_t timeout_ts_; // timeout time, DTL sends messages when it will use
};

class ObPieceMsgCtxMgr
{
public:
  ObPieceMsgCtxMgr() = default;
  ~ObPieceMsgCtxMgr() = default;
  void reset()
  {
    for (int i = 0; i < ctxs_.count(); ++i) {
      if (OB_NOT_NULL(ctxs_[i])) {
        ctxs_[i]->destroy();
        ctxs_[i]->~ObPieceMsgCtx();
      }
    }
    ctxs_.reset();
    types_.reset();
  }
  int find_piece_ctx(uint64_t op_id, dtl::ObDtlMsgType type, ObPieceMsgCtx *&ctx)
  {
    int ret = common::OB_ENTRY_NOT_EXIST;;
    for (int i = 0; i < ctxs_.count(); ++i) {
      if (ctxs_.at(i)->op_id_ == op_id && types_.at(i) == type) {
        ret = common::OB_SUCCESS;
        ctx = ctxs_.at(i);
        break;
      }
    }
    return ret;
  }
  int add_piece_ctx(ObPieceMsgCtx *ctx, dtl::ObDtlMsgType type)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(ctxs_.push_back(ctx))) {
    } else if (OB_FAIL(types_.push_back(type))) {
    }
    return ret;
  }
private:
  common::ObSEArray<ObPieceMsgCtx *, 2> ctxs_;
  common::ObSEArray<dtl::ObDtlMsgType, 2> types_; 
};

}
}
#endif /* __OB_SQL_ENGINE_PX_DATAHUB_BARRIER_PIECE_MSG_CTX_H__ */
//// end of header file


