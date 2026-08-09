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

#ifndef OCEANBASE_STANDBY_GRPC_SERVICE_H_
#define OCEANBASE_STANDBY_GRPC_SERVICE_H_

namespace oceanbase
{
namespace obgrpc
{
class ObGrpcServer;
}
namespace standby
{

struct StandbyConfig;
class StandbyGrpcService;

int create_and_register_standby_grpc_service(
    obgrpc::ObGrpcServer &grpc_server,
    const StandbyConfig &config,
    StandbyGrpcService *&service);
void destroy_standby_grpc_service(StandbyGrpcService *&service);

} // namespace standby
} // namespace oceanbase

#endif // OCEANBASE_STANDBY_GRPC_SERVICE_H_
