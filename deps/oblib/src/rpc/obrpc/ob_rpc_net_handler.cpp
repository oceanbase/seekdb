/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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

int64_t ObRpcNetHandler::CLUSTER_ID = common::INVALID_CLUSTER_ID;
uint64_t ObRpcNetHandler::CLUSTER_NAME_HASH = 0;


} // end of namespace obrpc
} // end of namespace oceanbase
