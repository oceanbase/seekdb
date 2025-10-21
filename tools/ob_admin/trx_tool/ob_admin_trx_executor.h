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

#ifndef OB_ADMIN_TRX_EXECUTOR_H_
#define OB_ADMIN_TRX_EXECUTOR_H_

#include "../ob_admin_executor.h"
#include "lib/string/ob_string.h"
#include "share/ob_srv_rpc_proxy.h"
#include "rpc/obrpc/ob_net_client.h"

namespace oceanbase
{
namespace tools
{
class ObAdminTransExecutor : public ObAdminExecutor
{
public:
  ObAdminTransExecutor();
  virtual ~ObAdminTransExecutor();
  virtual int execute(int argc, char *argv[]);
private:
  void print_usage();
  int parse_options(int argc, char *argv[]);
  int modify_trans();
  int dump_trans();
  int kill_trans();
 
private:
  bool inited_;
  obrpc::ObNetClient client_;
  obrpc::ObSrvRpcProxy srv_proxy_;

  common::ObAddr dst_server_;
  transaction::ObTransID trans_id_;
  int64_t status_;
  int64_t trans_version_;
  int64_t end_log_ts_;
  int32_t cmd_;
  int64_t timeout_;
  static const int64_t DEFAULT_TIMEOUT;
};

}
}

#endif /* OB_ADMIN_TRX_EXECUTOR_H_ */


