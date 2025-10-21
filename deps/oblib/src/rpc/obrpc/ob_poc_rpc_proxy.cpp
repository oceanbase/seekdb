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
