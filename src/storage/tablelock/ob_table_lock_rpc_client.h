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

#ifndef OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_RPC_CLIENT_H_
#define OCEANBASE_STORAGE_TABLELOCK_OB_TABLE_LOCK_RPC_CLIENT_H_

#include <stdint.h>

#include "common/ob_tablet_id.h"
#include "storage/tablelock/ob_table_lock_common.h"
#include "storage/tablelock/ob_table_lock_rpc_proxy.h"
#include "storage/tablelock/ob_table_lock_rpc_struct.h"

namespace oceanbase
{
namespace transaction
{
namespace tablelock
{

class ObTableLockRpcClient
{
public:
  static ObTableLockRpcClient &get_instance();

  int init();


private:
  // TODO: yanyuan.cxf use parallel rpc and modify this to 5s
  static const int64_t DEFAULT_TIMEOUT_US = 15L * 1000L * 1000L; // 15s
  ObTableLockRpcClient() : table_lock_rpc_proxy_() {}
  ~ObTableLockRpcClient() {}

private:
  obrpc::ObTableLockRpcProxy table_lock_rpc_proxy_;
};

}
}
}
#endif
