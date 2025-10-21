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

#define USING_LOG_PREFIX RPC_OBRPC
#include "rpc/obrpc/ob_rpc_net_handler.h"

#include <byteswap.h>
#include "rpc/obrpc/ob_poc_rpc_server.h"

using namespace oceanbase::common;
using namespace oceanbase::common::serialization;
using namespace oceanbase::rpc::frame;

namespace oceanbase
{
namespace obrpc
{

uint64_t ObRpcNetHandler::CLUSTER_NAME_HASH = 0;


} // end of namespace obrpc
} // end of namespace oceanbase
