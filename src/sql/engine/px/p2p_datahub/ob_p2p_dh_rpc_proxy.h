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
#ifndef OB_P2P_DH_RPC_PROXY_H
#define OB_P2P_DH_RPC_PROXY_H

#include "rpc/obrpc/ob_rpc_proxy.h"
#include "observer/ob_server_struct.h"
namespace oceanbase {
namespace sql {

class ObP2PDatahubMsgBase;
struct ObPxP2PDatahubArg
{
  OB_UNIS_VERSION(1);
public:
  ObPxP2PDatahubArg() : msg_(nullptr) {}
  void destroy_arg();
public:
  ObP2PDatahubMsgBase *msg_;
  TO_STRING_KV(KP(msg_));
};

struct ObPxP2PDatahubMsgResponse
{
  OB_UNIS_VERSION(1);
public:
  ObPxP2PDatahubMsgResponse() : rc_(0) {}
public:
  int rc_;
  TO_STRING_KV(K_(rc));
};

struct ObPxP2PClearMsgArg
{
  OB_UNIS_VERSION(1);
public:
  ObPxP2PClearMsgArg() : p2p_dh_ids_(), px_seq_id_(0) {}
public:
  ObSArray<int64_t> p2p_dh_ids_;
  int64_t px_seq_id_;
  TO_STRING_KV(K(p2p_dh_ids_));
};

}
namespace obrpc {

class ObP2PDhRpcProxy
    : public ObRpcProxy
{
public:
  DEFINE_TO(ObP2PDhRpcProxy);
  RPC_AP(PR5 send_p2p_dh_message, OB_PX_P2P_DH_MSG, (sql::ObPxP2PDatahubArg), sql::ObPxP2PDatahubMsgResponse);
  RPC_AP(PR5 clear_dh_msg, OB_PX_CLAER_DH_MSG, (sql::ObPxP2PClearMsgArg), sql::ObPxP2PDatahubMsgResponse);
};

}
}

#endif
