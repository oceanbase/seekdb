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

#define USING_LOG_PREFIX TABLELOCK

#include "ob_table_lock_rpc_client.h"

namespace oceanbase
{
using namespace share;

namespace transaction
{
namespace tablelock
{

int ObTableLockRpcClient::init()
{
  // No-op: the table-lock RPC proxy was removed (single-replica; locking goes through the
  // local ObTableLockService). Kept so the existing init() call site stays valid.
  return OB_SUCCESS;
}

ObTableLockRpcClient &ObTableLockRpcClient::get_instance()
{
  static ObTableLockRpcClient instance_;
  return instance_;
}



}
}
}
