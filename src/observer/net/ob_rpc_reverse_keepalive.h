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

#ifndef OCEANBASE_OBSERVER_RPC_REVERSE_KEEPALIVE_H
#define OCEANBASE_OBSERVER_RPC_REVERSE_KEEPALIVE_H

#include "observer/ob_rpc_processor_simple.h"
#include "lib/utility/ob_unify_serialize.h"
#include "lib/net/ob_addr.h"
#include "lib/utility/ob_print_utils.h"
#include "share/ob_srv_rpc_proxy.h"

namespace oceanbase
{
namespace obrpc
{
class ObRpcReverseKeepAliveService
{
public:
  ObRpcReverseKeepAliveService() : init_(false), rpc_proxy_(NULL), map_attr_(), rpc_pkt_id_map_() {}
  // called from the RPC receiver that needs to probe
  int receiver_probe(const ObRpcReverseKeepaliveArg& reverse_keepalive_arg);
  int sender_register(const int64_t pkt_id, int64_t pcode);
  int sender_unregister(const int64_t pkt_id);
  int check_status(const int64_t send_ts, const int64_t pkt_id);
  bool init_;
public:
  struct RpcPktID
  {
    int64_t rpc_pkt_id_;
    RpcPktID(const int64_t id) : rpc_pkt_id_(id) {}
    int64_t hash() const
    {
      return rpc_pkt_id_;
    }
    int hash(uint64_t &hash_val) const
    {
      hash_val = hash();
      return OB_SUCCESS;
    }
    bool operator== (const RpcPktID &other) const
    {
      return rpc_pkt_id_ == other.rpc_pkt_id_;
    }
    TO_STRING_KV(K_(rpc_pkt_id));
  };
private:
  ObSrvRpcProxy *rpc_proxy_;
  ObMemAttr map_attr_;
  common::ObLinearHashMap<RpcPktID, int64_t> rpc_pkt_id_map_;
};
extern ObRpcReverseKeepAliveService rpc_reverse_keepalive_instance;
}; // end namespace obrpc
namespace observer
{
OB_DEFINE_PROCESSOR_S(Srv, OB_RPC_REVERSE_KEEPALIVE, ObRpcReverseKeepaliveP);

}; // end of namespace observer
}; // end namespace oceanbase

#endif /* !OCEANBASE_OBSERVER_RPC_REVERSE_KEEPALIVE_H */
