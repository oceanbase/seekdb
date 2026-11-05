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
// Stub: reverse keepalive dead in seekdb. Matches original API.
#ifndef OCEANBASE_RPC_OB_REVERSE_KEEPALIVE_STUB_H_
#define OCEANBASE_RPC_OB_REVERSE_KEEPALIVE_STUB_H_
#include "lib/utility/ob_print_utils.h"
#include "lib/net/ob_addr.h"
namespace oceanbase { namespace rpc { namespace frame {
struct ObReverseKeepaliveArg {
  OB_UNIS_VERSION(1);
public:
  ObReverseKeepaliveArg() : first_send_ts_(0), pkt_id_(-1) {}
  bool is_valid() const { return false; }
  TO_STRING_KV(K_(dst), K_(pkt_id), K_(first_send_ts));
  ObAddr dst_;
  int64_t first_send_ts_;
  int64_t pkt_id_;
};
struct ObReverseKeepaliveResp {
  OB_UNIS_VERSION(1);
public:
  ObReverseKeepaliveResp() : ret_(0) {}
  TO_STRING_KV(K_(ret));
  int32_t ret_;
};
// Reverse-keepalive is dead in seekdb: ObReverseKeepaliveArg::is_valid() is always false,
// so the stream session handler never reaches these. No-op definitions keep it linking.
inline void stream_rpc_register(const int64_t, int64_t) {}
inline int stream_rpc_reverse_probe(const ObReverseKeepaliveArg&) { return 0; /*OB_SUCCESS*/ }
} } }
#endif
