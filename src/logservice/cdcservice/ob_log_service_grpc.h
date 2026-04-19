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

#ifndef OCEANBASE_LOGSERVICE_CDC_OB_LOG_SERVICE_GRPC_H_
#define OCEANBASE_LOGSERVICE_CDC_OB_LOG_SERVICE_GRPC_H_

#include "lib/ob_define.h"
#include "lib/net/ob_addr.h"
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifndef CONST
#define CONST const
#define _OB_UNDEF_CONST
#endif
#ifndef OPTIONAL
#define OPTIONAL
#define _OB_UNDEF_OPTIONAL
#endif
#include <mswsock.h>
#ifdef _OB_UNDEF_CONST
#undef CONST
#undef _OB_UNDEF_CONST
#endif
#ifdef _OB_UNDEF_OPTIONAL
#undef OPTIONAL
#undef _OB_UNDEF_OPTIONAL
#endif
#undef ERROR
#undef DELETE
#endif
#include "grpc/logservice.grpc.pb.h"
#include "grpc/ob_grpc_context.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace cdc
{
class ObLogServiceGrpcServiceImpl final : public ::logservice::LogService::Service
{
public:
  ObLogServiceGrpcServiceImpl() {}
  virtual ~ObLogServiceGrpcServiceImpl() {}

  ::grpc::Status fetch_log(::grpc::ServerContext* context,
                           const ::logservice::FetchLogReq* request,
                           ::logservice::FetchLogRes* response) override;

  ::grpc::Status fetch_missing_log(::grpc::ServerContext* context,
                                    const ::logservice::FetchMissingLogReq* request,
                                    ::logservice::FetchMissingLogRes* response) override;

  ::grpc::Status fetch_raw_log(::grpc::ServerContext* context,
                                const ::logservice::FetchRawLogReq* request,
                                ::logservice::FetchRawLogRes* response) override;
};

} // namespace cdc
} // namespace oceanbase

#endif /* OCEANBASE_LOGSERVICE_CDC_OB_LOG_SERVICE_GRPC_H_ */
