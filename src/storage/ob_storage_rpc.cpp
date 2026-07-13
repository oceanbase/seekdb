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
#include "ob_storage_rpc.h"
#include "logservice/ob_log_service.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/tablet/ob_tablet_iterator.h"
#include "storage/ddl/ob_direct_load_mgr_utils.h"
#include "lib/thread/thread.h"
#include "lib/worker.h"

namespace oceanbase
{
using namespace lib;
using namespace common;
using namespace share;
using namespace obcall;
using namespace storage;
using namespace blocksstable;
using namespace memtable;
using namespace share::schema;

namespace obcall
{


// Dead obcall copy/restore/LS RPC payload implementations removed.


// ObStorageStreamRpcP<> obcall stream-RPC processor impls deleted — dead in seekdb.


// Legacy shared-storage migrate-warmup obcall RPC arg/result struct impls removed
// (replaced by gRPC; see ob_storage_grpc.cpp).


// cross-tenant LOB obcall RPC removed: ObLobQueryP processor (OB_LOB_QUERY) deleted —
// cross-tenant LOB read now runs in-process (storage/lob/ob_lob_remote.cpp).
// Legacy shared-storage migrate-warmup obcall RPC processor impls removed
// (ObFetchMicroBlockKeysP / ObFetchMicroBlockP / ObGetMicroBlockCacheInfoP /
//  ObGetMigrationCacheJobInfoP / ObFetchReplicaPrewarmMicroBlockP) — replaced by gRPC.

} //namespace obcall

namespace storage
{

ObStorageRpc::ObStorageRpc()
    : is_inited_(false),
      rpc_proxy_(NULL)
{
}

ObStorageRpc::~ObStorageRpc()
{
  destroy();
}

int ObStorageRpc::init(
    obcall::ObStorageRpcProxy *rpc_proxy,
    const common::ObAddr &self)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "storage rpc has inited", K(ret));
  } else if (OB_ISNULL(rpc_proxy) || !self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "ObStorageRpc init with invalid argument",
        KP(rpc_proxy), K(self));
  } else {
    rpc_proxy_ = rpc_proxy;
    self_ = self;
    is_inited_ = true;
  }
  return ret;
}

void ObStorageRpc::destroy()
{
  if (is_inited_) {
    is_inited_ = false;
    rpc_proxy_ = NULL;
    self_ = ObAddr();
  }
}


// Legacy shared-storage migrate-warmup ObStorageRpc wrapper impls removed
// (get_ls_micro_block_cache_info / get_ls_migration_cache_job_info /
//  get_micro_block_key_set) — replaced by gRPC.
} // storage
} // oceanbase
