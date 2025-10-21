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

#include "ob_blacklist_resp_processor.h"
#include "share/ob_server_blacklist.h"

namespace oceanbase
{
using namespace common;

namespace obrpc
{
class ObServerBlacklist;

int ObBlacklistRespP::process()
{
  int ret = OB_SUCCESS;
  const int64_t src_cluster_id = get_src_cluster_id();
  if (OB_FAIL(share::ObServerBlacklist::get_instance().handle_resp(arg_, src_cluster_id))) {
    RPC_LOG(WARN, "handle_msg failed", K(ret));
  }
  req_->set_trace_point();
  return ret;
}

}; // end namespace rpc
}; // end namespace oceanbase
