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

#include "observer/net/ob_shared_storage_net_throt_rpc_struct.h"

#define USING_LOG_PREFIX RPC

namespace oceanbase
{

namespace obrpc
{
// for the serialize need of rpc
OB_SERIALIZE_MEMBER(ObSharedDeviceResource, key_, type_, value_);
OB_SERIALIZE_MEMBER(ObSharedDeviceResourceArray, array_);
OB_SERIALIZE_MEMBER(ObSSNTKey, addr_, key_);
OB_SERIALIZE_MEMBER(ObSSNTValue, predicted_resource_, assigned_resource_, expire_time_);
OB_SERIALIZE_MEMBER(ObSSNTResource, ops_, ips_, iops_, obw_, ibw_, iobw_, tag_);

OB_SERIALIZE_MEMBER(ObSSNTEndpointArg, addr_, storage_keys_, expire_time_);
OB_SERIALIZE_MEMBER(ObSSNTSetRes, res_);
}  // namespace obrpc
}  // namespace oceanbase
