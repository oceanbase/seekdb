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
#ifndef OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_RPC_H_
#define OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_RPC_H_
#include "rpc/frame/ob_req_transport.h"    // ObReqTransport
#include "ob_log_restore_rpc_define.h"     // ObRemoteFetchLog*
namespace oceanbase
{
namespace logservice
{
class ObLogResSvrRpc
{
  static const int64_t MAX_PROCESS_HANDLER_TIME = 100 * 1000L;
public:
  ObLogResSvrRpc() : inited_(false), proxy_() {}
  ~ObLogResSvrRpc() {}

  int init(const rpc::frame::ObReqTransport *transport);
  void destroy();
  int fetch_log(const ObAddr &server,
      const obrpc::ObRemoteFetchLogRequest &req,
      obrpc::ObRemoteFetchLogResponse &res);
private:
  bool inited_;
  obrpc::ObLogResSvrProxy proxy_;
};
} // namespace logservice
} // namespace oceanbase
#endif /* OCEANBASE_LOGSERVICE_OB_LOG_RESTORE_RPC_H_ */
