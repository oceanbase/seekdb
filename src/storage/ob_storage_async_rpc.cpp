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

#define USING_LOG_PREFIX STORAGE

#include "ob_storage_async_rpc.h"
#include "share/ob_force_print_log.h"

using namespace oceanbase::common;
using namespace oceanbase::storage;


ObHAAsyncRpcArg::ObHAAsyncRpcArg()
  : tenant_id_(OB_INVALID_ID),
    group_id_(0),
    rpc_timeout_(0),
    member_addr_list_()
{
}

ObHAAsyncRpcArg::~ObHAAsyncRpcArg()
{
}

bool  ObHAAsyncRpcArg::is_valid() const
{
  return OB_INVALID_ID != tenant_id_
      && group_id_ >= 0
      && rpc_timeout_ > 0
      && !member_addr_list_.empty();
}




