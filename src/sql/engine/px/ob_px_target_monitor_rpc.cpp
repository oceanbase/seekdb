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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_px_target_monitor_rpc.h"

namespace oceanbase
{
namespace sql
{

OB_SERIALIZE_MEMBER(ObPxRpcAddrTarget, addr_, target_);
OB_SERIALIZE_MEMBER(ObPxRpcFetchStatArgs, tenant_id_, follower_version_, addr_target_array_, need_refresh_all_);
OB_SERIALIZE_MEMBER(ObPxRpcFetchStatResponse, status_, tenant_id_, leader_version_, addr_target_array_);

}
}
