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

#ifndef OCEANBASE_OBSERVER_RPC_REVERSE_KEEPALIVE_STRUCT_H
#define OCEANBASE_OBSERVER_RPC_REVERSE_KEEPALIVE_STRUCT_H

#include "lib/utility/ob_unify_serialize.h"
#include "lib/net/ob_addr.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace obrpc
{
struct ObRpcReverseKeepaliveArg
{
  OB_UNIS_VERSION(1);
public:
  ObRpcReverseKeepaliveArg()
   : dst_(), first_send_ts_(OB_INVALID_TIMESTAMP), pkt_id_(-1)
  {}
  ObRpcReverseKeepaliveArg(const ObAddr& dst, int64_t first_send_ts_, const int64_t pkt_id)
    : dst_(dst), first_send_ts_(first_send_ts_), pkt_id_(pkt_id)
  {}
  ~ObRpcReverseKeepaliveArg()
  {}
  bool is_valid() const
  {
    return dst_.is_valid() && pkt_id_ >= 0 && pkt_id_ <= UINT32_MAX && first_send_ts_ > 0;
  }
  TO_STRING_KV(K_(dst), K_(pkt_id), K_(first_send_ts));

  ObAddr dst_;
  int64_t first_send_ts_;
  int64_t pkt_id_;
  DISALLOW_COPY_AND_ASSIGN(ObRpcReverseKeepaliveArg);
};

struct ObRpcReverseKeepaliveResp
{
  OB_UNIS_VERSION(1);
public:
  ObRpcReverseKeepaliveResp() : ret_(OB_SUCCESS)
  {}
  ~ObRpcReverseKeepaliveResp()
  {}
  int assign(const ObRpcReverseKeepaliveResp &other)
  {
    ret_ = other.ret_;
    return OB_SUCCESS;
  }
  TO_STRING_KV(K_(ret));
  int32_t ret_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObRpcReverseKeepaliveResp);
};
}; // end namespace obrpc
}; // end namespace oceanbase

#endif /* !OCEANBASE_OBSERVER_RPC_REVERSE_KEEPALIVE_STRUCT_H */
