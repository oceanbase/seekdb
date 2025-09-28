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




