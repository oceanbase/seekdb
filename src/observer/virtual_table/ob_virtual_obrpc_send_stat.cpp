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

#include "ob_virtual_obrpc_send_stat.h"
#include "observer/ob_server_utils.h"
#include "observer/ob_server.h"

using namespace oceanbase::rpc;
using namespace oceanbase::obrpc;
using namespace oceanbase::common;
using namespace oceanbase::observer;

ObVirtualObRpcSendStat::ObVirtualObRpcSendStat()
{}

ObVirtualObRpcSendStat::~ObVirtualObRpcSendStat()
{
  reset();
}

void ObVirtualObRpcSendStat::reset()
{
}

int ObVirtualObRpcSendStat::inner_get_next_row(ObNewRow *&)
{
  return OB_ITER_END;
}
