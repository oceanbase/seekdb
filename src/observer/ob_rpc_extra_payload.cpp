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

#define USING_LOG_PREFIX SERVER
#include "ob_rpc_extra_payload.h"
#include "share/ob_debug_sync.h"

namespace oceanbase
{
namespace observer
{

int64_t ObRpcExtraPayload::get_serialize_size() const
{
  return GDS.rpc_spread_actions().get_serialize_size();
}

int ObRpcExtraPayload::serialize(SERIAL_PARAMS) const
{
  return GDS.rpc_spread_actions().serialize(buf, buf_len, pos);
}

int ObRpcExtraPayload::deserialize(DESERIAL_PARAMS)
{
  return GDS.rpc_spread_actions().deserialize(buf, data_len, pos);
}

} // end namespace server
} // end namespace oceanbase
