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

#ifndef OCEANBASE_ROOTSERVER_STANDBY_OB_SERVICE_GRPC_H_
#define OCEANBASE_ROOTSERVER_STANDBY_OB_SERVICE_GRPC_H_

#include "lib/ob_define.h"
#include "grpc/serverservice.grpc.pb.h"
#include "grpc/ob_grpc_context.h"
#include "share/ob_all_tenant_info.h"
#include "lib/oblog/ob_log_module.h"
#include "logservice/palf/palf_options.h"  // palf::AccessMode
#include "share/scn.h"  // share::SCN

namespace oceanbase
{
namespace rootserver
{
namespace standby
{

class ObServerServiceImpl final : public serverservice::ServerService::Service
{
public:
  ObServerServiceImpl() {}
  virtual ~ObServerServiceImpl() {}

  grpc::Status get_tenant_info(grpc::ServerContext* context,
                                const serverservice::GetTenantInfoReq* request,
                                serverservice::GetTenantInfoRes* response) override;

  grpc::Status get_max_log_info(grpc::ServerContext* context,
                                 const serverservice::GetMaxLogInfoReq* request,
                                 serverservice::GetMaxLogInfoRes* response) override;
};

class ObServiceGrpcClient
{
public:
  ObServiceGrpcClient();
  ~ObServiceGrpcClient();

  int init(const common::ObAddr& addr, int64_t timeout);
  int get_tenant_info(share::ObAllTenantInfo& tenant_info);
  int get_max_log_info(palf::AccessMode& mode, share::SCN& scn);

private:
  bool is_inited_;
  obgrpc::ObGrpcClient<serverservice::ServerService> grpc_client_;
};

} // namespace standby
} // namespace rootserver
} // namespace oceanbase

#endif /* OCEANBASE_ROOTSERVER_STANDBY_OB_SERVICE_GRPC_H_ */
