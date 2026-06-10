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

#define USING_LOG_PREFIX TRANS

#include "ob_weak_read_service_rpc_define.h"
#include "ob_weak_read_service_rpc.h"

namespace oceanbase
{
using namespace common;
namespace obcall
{

OB_SERIALIZE_MEMBER(ObWrsGetClusterVersionRequest, req_server_);
OB_SERIALIZE_MEMBER(ObWrsGetClusterVersionResponse, err_code_, version_, version_duration_us_);
OB_SERIALIZE_MEMBER(ObWrsClusterHeartbeatRequest,
    req_server_,
    version_,
    valid_part_count_,
    total_part_count_,
    generate_timestamp_);
OB_SERIALIZE_MEMBER(ObWrsClusterHeartbeatResponse, err_code_);


}
}
