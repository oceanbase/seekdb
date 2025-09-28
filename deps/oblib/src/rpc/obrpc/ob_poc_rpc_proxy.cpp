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

#include "ob_poc_rpc_proxy.h"
#include "rpc/obrpc/ob_rpc_proxy.h"
#include "rpc/obrpc/ob_net_keepalive.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace obrpc
{
extern const int easy_head_size = 16;



int64_t ObPocClientStub::get_proxy_timeout(ObRpcProxy& proxy) {
  return proxy.timeout();
}

void ObPocClientStub::set_rcode(ObRpcProxy& proxy, const ObRpcResultCode& rcode) {
  proxy.set_result_code(rcode);
}
void ObPocClientStub::set_handle(ObRpcProxy& proxy, Handle* handle, const ObRpcPacketCode& pcode, const ObRpcOpts& opts, bool is_stream_next, int64_t session_id, int64_t pkt_id, int64_t send_ts) {
  proxy.set_handle_attr(handle, pcode, opts, is_stream_next, session_id, pkt_id, send_ts);
}

int ObPocClientStub::log_user_error_and_warn(const ObRpcResultCode &rcode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_SUCCESS != rcode.rcode_)) {
    FORWARD_USER_ERROR(rcode.rcode_, rcode.msg_);
  }
  for (int i = 0; OB_SUCC(ret) && i < rcode.warnings_.count(); ++i) {
    const common::ObWarningBuffer::WarningItem warning_item = rcode.warnings_.at(i);
    if (ObLogger::USER_WARN == warning_item.log_level_) {
      FORWARD_USER_WARN(warning_item.code_, warning_item.msg_);
    } else if (ObLogger::USER_NOTE == warning_item.log_level_) {
      FORWARD_USER_NOTE(warning_item.code_, warning_item.msg_);
    } else {
      ret = common::OB_ERR_UNEXPECTED;
      RPC_LOG(WARN, "unknown log type", K(ret));
    }
  }
  return ret;
}

}; // end namespace obrpc
}; // end namespace oceanbase
extern "C" {
int pn_terminate_pkt(uint64_t gtid, uint32_t pkt_id)
{
  return 0;
}
}
