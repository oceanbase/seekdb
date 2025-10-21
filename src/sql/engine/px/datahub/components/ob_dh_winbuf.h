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

#ifndef __OB_SQL_ENG_PX_DH_WINBUF_H__
#define __OB_SQL_ENG_PX_DH_WINBUF_H__

#include "sql/engine/px/datahub/ob_dh_msg.h"
#include "sql/engine/px/datahub/ob_dh_dtl_proc.h"
#include "sql/engine/px/datahub/ob_dh_msg_ctx.h"
#include "sql/engine/px/datahub/ob_dh_msg_provider.h"
#include "sql/engine/basic/ob_chunk_row_store.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/basic/ob_temp_row_store.h"

namespace oceanbase
{
namespace sql
{

class ObWinbufPieceMsg;
class ObWinbufWholeMsg;
typedef ObPieceMsgP<ObWinbufPieceMsg> ObWinbufPieceMsgP;
typedef ObWholeMsgP<ObWinbufWholeMsg> ObWinbufWholeMsgP;
class ObWinbufPieceMsgListener;
class ObWinbufPieceMsgCtx;
class ObPxCoordInfo;

/* Various datahub subclass message definitions are as follows */
class ObWinbufPieceMsg
  : public ObDatahubPieceMsg<dtl::ObDtlMsgType::DH_WINBUF_PIECE_MSG>

{
  OB_UNIS_VERSION_V(1);
public:
  using PieceMsgListener = ObWinbufPieceMsgListener;
  using PieceMsgCtx = ObWinbufPieceMsgCtx;
public:
  ObWinbufPieceMsg() : is_end_(false), is_datum_(false), col_count_(0),
      row_(), datum_row_(NULL), row_size_(0), payload_len_(0), deseria_allocator_() {}
  ~ObWinbufPieceMsg() = default;
  void reset() { deseria_allocator_.reset(); }
  INHERIT_TO_STRING_KV("meta", ObDatahubPieceMsg<dtl::ObDtlMsgType::DH_WINBUF_PIECE_MSG>,
                       K_(op_id));
public:
  /* functions */
  /* variables */
  // window function is only used when partition by is empty
  // That is, this parallel mechanism will only be used when there is only one group
  // Because there is only one group, therefore each received piece msg has only one line which is the aggregated result

  bool is_end_;     // Mark this piece has no win buf data.
  bool is_datum_;  // data is ObNewRow or datum array (static engine) format.
  int64_t col_count_;
  ObNewRow row_;
  ObChunkDatumStore::StoredRow *datum_row_;
  uint32_t row_size_;  // for datum row use
  int64_t payload_len_; //for datum row use
  common::ObArenaAllocator deseria_allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObWinbufPieceMsg);
};


class ObWinbufWholeMsg
    : public ObDatahubWholeMsg<dtl::ObDtlMsgType::DH_WINBUF_WHOLE_MSG>
{
  OB_UNIS_VERSION_V(1);
public:
  using WholeMsgProvider = ObWholeMsgProvider<ObWinbufPieceMsg, ObWinbufWholeMsg>;
public:
  ObWinbufWholeMsg() : ready_state_(0), is_empty_(true), is_datum_(false),
      row_store_(), datum_store_("PXDhWinbuf"), assign_allocator_()
  {}
  ~ObWinbufWholeMsg() = default;
  int assign(const ObWinbufWholeMsg &other, common::ObIAllocator *allocator = NULL);
  void reset()
  {
    ready_state_ = 0;
    is_empty_ = true;
    is_datum_ = false;
    row_store_.reset();
    datum_store_.reset();
    assign_allocator_.reset();
  }
  VIRTUAL_TO_STRING_KV(K_(ready_state));
  int ready_state_; // placeholder, not actually used
  bool is_empty_; // There is no data at all, so there is no need to serialize the store
  bool is_datum_; // data is ObNewRow or datum array (static engine) format.
  sql::ObChunkRowStore row_store_;
  sql::ObChunkDatumStore datum_store_;
  common::ObArenaAllocator assign_allocator_;
};

class ObWinbufPieceMsgCtx : public ObPieceMsgCtx
{
public:
  ObWinbufPieceMsgCtx(uint64_t op_id, int64_t task_cnt, int64_t timeout_ts, int64_t tenant_id)
    : ObPieceMsgCtx(op_id, task_cnt, timeout_ts), received_(0),
                    tenant_id_(tenant_id), whole_msg_() {}
  ~ObWinbufPieceMsgCtx() = default;
  virtual void destroy() { whole_msg_.reset(); }
  INHERIT_TO_STRING_KV("meta", ObPieceMsgCtx, K_(received));
  static int alloc_piece_msg_ctx(const ObWinbufPieceMsg &pkt,
                                 ObPxCoordInfo &coord_info,
                                 ObExecContext &ctx,
                                 int64_t task_cnt,
                                 ObPieceMsgCtx *&msg_ctx);
  virtual int send_whole_msg(common::ObIArray<ObPxSqcMeta> &sqcs) override;
  virtual void reset_resource() override;
  int received_; // number of pieces already received
  int64_t tenant_id_;
  ObWinbufWholeMsg whole_msg_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObWinbufPieceMsgCtx);
};

class ObWinbufPieceMsgListener
{
public:
  ObWinbufPieceMsgListener() = default;
  ~ObWinbufPieceMsgListener() = default;
  static int on_message(
      ObWinbufPieceMsgCtx &ctx,
      common::ObIArray<ObPxSqcMeta> &sqcs,
      const ObWinbufPieceMsg &pkt);
private:
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObWinbufPieceMsgListener);
};

// piece/whole msg for single part winfunc parallel execution
// used for ObWindowFunctionVecOp
// SP stands for Single Partition
class SPWinFuncPXPieceMsg;
class SPWinFuncPXWholeMsg;
typedef ObPieceMsgP<SPWinFuncPXPieceMsg> ObSPWinFuncPXPieceMsgP;
typedef ObWholeMsgP<SPWinFuncPXWholeMsg> ObSPWinFuncPXWholeMsgP;

class SPWinFuncPXPieceMsgListener;
class SPWinFuncPXPieceMsgCtx;

class SPWinFuncPXPieceMsg: public ObDatahubPieceMsg<dtl::ObDtlMsgType::DH_SP_WINFUNC_PX_PIECE_MSG>
{
  OB_UNIS_VERSION_V(1);
public:
  using PieceMsgListener = SPWinFuncPXPieceMsgListener;
  using PieceMsgCtx = SPWinFuncPXPieceMsgCtx;
public:
  SPWinFuncPXPieceMsg(const lib::ObMemAttr &mem_attr) :
    deserial_allocator_(mem_attr), is_empty_(true), row_meta_(&deserial_allocator_), row_(nullptr)
  {}
  SPWinFuncPXPieceMsg():
    deserial_allocator_(), is_empty_(true), row_meta_(&deserial_allocator_), row_(nullptr) {}
  ~SPWinFuncPXPieceMsg() = default;

  void reset() { deserial_allocator_.reset(); }

  INHERIT_TO_STRING_KV("meta", ObDatahubPieceMsg<dtl::ObDtlMsgType::DH_SP_WINFUNC_PX_PIECE_MSG>,
                       K_(op_id));

public:
  common::ObArenaAllocator deserial_allocator_;
  bool is_empty_; // wether this piece contains data
  RowMeta row_meta_; // row meta of compact row
  ObCompactRow *row_;
};

class SPWinFuncPXWholeMsg: public ObDatahubWholeMsg<dtl::ObDtlMsgType::DH_SP_WINFUNC_PX_WHOLE_MSG>
{
  OB_UNIS_VERSION_V(1);
public:
  using WholeMsgProvider = ObWholeMsgProvider<SPWinFuncPXPieceMsg, SPWinFuncPXWholeMsg>;
public:
  SPWinFuncPXWholeMsg(const common::ObMemAttr &mem_attr) :
    assign_allocator_(mem_attr), is_empty_(true), row_meta_(&assign_allocator_),
    row_store_(&assign_allocator_)
  {}

  SPWinFuncPXWholeMsg():
    assign_allocator_(), is_empty_(true), row_meta_(), row_store_() {}

  int assign(const SPWinFuncPXWholeMsg &other, common::ObIAllocator *allocator = NULL);
  void reset()
  {
    is_empty_ = true;
    row_meta_.reset();
    row_store_.reset();
  }

  TO_STRING_KV(K_(is_empty), K(row_store_.get_row_cnt()));

public:
  common::ObArenaAllocator assign_allocator_;
  bool is_empty_; // wether row_store_ is empty, if so, do not serialize row_store_
  RowMeta row_meta_; // row meta of stored rows
  sql::ObTempRowStore row_store_;
};

class SPWinFuncPXPieceMsgCtx : public ObPieceMsgCtx
{
public:
  SPWinFuncPXPieceMsgCtx(uint64_t op_id, int64_t task_cnt, int64_t timeout_ts, uint64_t tenant_id,
                         int64_t max_batch_size, const common::ObMemAttr &mem_attr) :
    ObPieceMsgCtx(op_id, task_cnt, timeout_ts),
    received_(0), tenant_id_(tenant_id), max_batch_size_(max_batch_size), whole_msg_(mem_attr)
  {}

  static int alloc_piece_msg_ctx(const SPWinFuncPXPieceMsg &pkt, ObPxCoordInfo &coord_info,
                                 ObExecContext &ctx, int64_t task_cnt, ObPieceMsgCtx *&msg_ctx);

  virtual int send_whole_msg(common::ObIArray<ObPxSqcMeta> &sqcs) override;
  virtual void reset_resource() override;

public:
  int64_t received_; // number of piece msgs received
  uint64_t tenant_id_;
  int64_t max_batch_size_;
  SPWinFuncPXWholeMsg whole_msg_;

private:
  DISALLOW_COPY_AND_ASSIGN(SPWinFuncPXPieceMsgCtx);
};

class SPWinFuncPXPieceMsgListener
{
public:
  static int on_message(SPWinFuncPXPieceMsgCtx &ctx, common::ObIArray<ObPxSqcMeta> &sqcs,
                        const SPWinFuncPXPieceMsg &pkt);

private:
  DISALLOW_COPY_AND_ASSIGN(SPWinFuncPXPieceMsgListener);
};
}
}
#endif /* __OB_SQL_ENG_PX_DH_WINBUF_H__ */
//// end of header file

