/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
