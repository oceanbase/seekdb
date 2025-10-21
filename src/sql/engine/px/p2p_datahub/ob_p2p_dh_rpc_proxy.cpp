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
#include "ob_p2p_dh_rpc_proxy.h"
#include "sql/engine/px/p2p_datahub/ob_p2p_dh_mgr.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

OB_DEF_SERIALIZE(ObPxP2PDatahubArg)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(msg_));
  OB_UNIS_ENCODE(msg_->get_msg_type());
  OB_UNIS_ENCODE(*msg_);
  return ret;
}

OB_DEF_DESERIALIZE(ObPxP2PDatahubArg)
{
  int ret =  OB_SUCCESS;
  CK(OB_NOT_NULL(CURRENT_CONTEXT));
  ObP2PDatahubMsgBase::ObP2PDatahubMsgType msg_type = ObP2PDatahubMsgBase::NOT_INIT;
  OB_UNIS_DECODE(msg_type);
  ObIAllocator &allocator = CURRENT_CONTEXT->get_arena_allocator();
  if (OB_FAIL(PX_P2P_DH.alloc_msg(allocator, msg_type, msg_))) {
    LOG_WARN("fail to alloc msg", K(ret));
  } else {
    OB_UNIS_DECODE(*msg_);
  }
  if (OB_FAIL(ret) && OB_NOT_NULL(msg_)) {
    // DECODE failed, must destroy msg.
    msg_->destroy();
    msg_ = nullptr;
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPxP2PDatahubArg)
{
  int64_t len = 0;

  LST_DO_CODE(OB_UNIS_ADD_LEN, msg_->get_msg_type(), *msg_);
  return len;
}

void ObPxP2PDatahubArg::destroy_arg()
{
  if (OB_NOT_NULL(msg_)) {
    msg_->destroy();
    msg_ = nullptr;
  }
}

OB_SERIALIZE_MEMBER(ObPxP2PDatahubMsgResponse, rc_);

OB_SERIALIZE_MEMBER(ObPxP2PClearMsgArg, p2p_dh_ids_, px_seq_id_);
