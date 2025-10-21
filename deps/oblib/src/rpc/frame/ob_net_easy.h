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

#ifndef _OCEABASE_RPC_FRAME_OB_NET_EASY_H_
#define _OCEABASE_RPC_FRAME_OB_NET_EASY_H_

#include "rpc/frame/ob_req_handler.h"
#include "rpc/frame/ob_req_transport.h"
#include "rpc/obrpc/ob_listener.h"
#include "rpc/obrpc/ob_net_keepalive.h"
#include "lib/ssl/ob_ssl_config.h"

using namespace oceanbase::obrpc;
namespace oceanbase
{
namespace common
{
}
namespace rpc
{
namespace frame
{

struct ObNetOptions {
  int rpc_io_cnt_; // rpc io thread count
  int high_prio_rpc_io_cnt_; // high priority io thread count
  int mysql_io_cnt_; // mysql io thread count
  int batch_rpc_io_cnt_; // batch rpc io thread count
  bool use_ipv6_; // support ipv6 protocol
  int64_t tcp_user_timeout_;
  int64_t tcp_keepidle_;
  int64_t tcp_keepintvl_;
  int64_t tcp_keepcnt_;
  int enable_tcp_keepalive_;
  ObNetOptions() : rpc_io_cnt_(0), high_prio_rpc_io_cnt_(0),
      mysql_io_cnt_(0), batch_rpc_io_cnt_(0), use_ipv6_(false), tcp_user_timeout_(0),
      tcp_keepidle_(0), tcp_keepintvl_(0), tcp_keepcnt_(0), enable_tcp_keepalive_(0) {}
  TO_STRING_KV(K(rpc_io_cnt_),
               K(high_prio_rpc_io_cnt_),
               K(mysql_io_cnt_),
               K(batch_rpc_io_cnt_),
               K(use_ipv6_),
               K(tcp_user_timeout_),
               K(tcp_keepidle_),
               K(tcp_keepintvl_),
               K(tcp_keepcnt_),
               K(enable_tcp_keepalive_));
};

} // end of namespace frame
} // end of namespace rpc
} // end of namespace oceanbase


#endif /* _OCEABASE_RPC_FRAME_OB_NET_EASY_H_ */
