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
#include "ob_log_restore_rpc.h"

namespace oceanbase
{
namespace logservice
{
int ObLogResSvrRpc::init(const rpc::frame::ObReqTransport *transport)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_ISNULL(transport)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(proxy_.init(transport))) {
    CLOG_LOG(WARN, "init rpc proxy failed", K(ret), K(transport));
  } else {
    inited_ = true;
  }
  return ret;
}

void ObLogResSvrRpc::destroy()
{
  inited_ = false;
  proxy_.destroy();
}

int ObLogResSvrRpc::fetch_log(const ObAddr &server,
    const obrpc::ObRemoteFetchLogRequest &req,
    obrpc::ObRemoteFetchLogResponse &res)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(! inited_)) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(! server.is_valid() || ! req.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = proxy_.to(server)
          .trace_time(true)
          .max_process_handler_time(MAX_PROCESS_HANDLER_TIME)
          .by(req.tenant_id_)
          .dst_cluster_id(1)
          .remote_fetch_log(req, res);
  }
  return ret;
}

} // namespace logservice
} // namespace oceanbase
