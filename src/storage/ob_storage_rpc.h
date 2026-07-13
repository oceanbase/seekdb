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

#ifndef OCEABASE_STORAGE_RPC
#define OCEABASE_STORAGE_RPC

#include "lib/net/ob_addr.h"
#include "storage/ob_storage_rpc_arg.h"
#include "storage/tx/ob_tx_result_struct.h"
#include "lib/utility/ob_unify_serialize.h"
#include "rpc/frame/ob_result_code.h"
#include "storage/ob_storage_struct.h"
#include "observer/ob_server_struct.h"
#include "storage/ob_storage_schema.h"
#include "storage/ob_storage_ha_struct.h"
#include "storage/blocksstable/ob_sstable_meta.h"
#include "tablet/ob_tablet_meta.h"
#include "share/ls/ob_ls_restore_status.h"
#include "storage/lob/ob_lob_rpc_struct.h"
#include "storage/blocksstable/ob_logic_macro_id.h"
#include "storage/meta_mem/ob_tablet_pointer.h"

namespace oceanbase
{
namespace rpc { namespace frame { class ObReqTransport; } }
namespace storage
{
class ObLogStreamService;
class ObICopySSTableMacroRangeObProducer;
}

namespace obcall
{

// Dead obcall copy/restore/LS RPC payloads removed for single-LS seekdb.

// Legacy shared-storage migrate-warmup obcall RPC arg/result structs removed
// (ObGetMicroBlockCacheInfo{Arg,Res}, ObGetMigrationCacheJobInfo{Arg,Res},
//  ObGetMicroBlockKeyArg, ObMigrateWarmupKeySet, ObCopyMicroBlockKeySetRes,
//  ObSSLSFetchMicroBlockArg) — send/recv path replaced by gRPC.

//src
// Inert shell: all obcall RPC methods are removed/dead in seekdb (single-replica;
// HA/migration is gRPC). Kept only as a pointer type for dead HA plumbing; no
// longer derives from the obcall RPC framework.
class ObStorageRpcProxy
{
public:
  static const int64_t STREAM_RPC_TIMEOUT = 30 * 1000 * 1000LL; // 30s
  int init(const common::ObAddr & = common::ObAddr())
  { return common::OB_SUCCESS; }
  void destroy() {}
};

// ObStorageStreamRpcP (obcall stream-RPC processor template) deleted — dead in seekdb.


// cross-tenant LOB obcall RPC removed: ObLobQueryP (OB_LOB_QUERY processor) deleted — the
// cross-tenant LOB read now runs in-process (see ObLobRemoteUtil in storage/lob/ob_lob_remote.cpp).
// Legacy shared-storage migrate-warmup obcall RPC processors removed
// (ObFetchMicroBlockKeysP / ObFetchMicroBlockP / ObGetMicroBlockCacheInfoP /
//  ObGetMigrationCacheJobInfoP / ObFetchReplicaPrewarmMicroBlockP) — replaced by gRPC.

} // obcall


namespace storage
{
//dst
class ObIStorageRpc
{
public:
  ObIStorageRpc() {}
  virtual ~ObIStorageRpc() {}
  virtual int init(
      obcall::ObStorageRpcProxy *rpc_proxy,
      const common::ObAddr &self) = 0;
  virtual void destroy() = 0;
public:


};

class ObStorageRpc: public ObIStorageRpc
{
public:
  ObStorageRpc();
  ~ObStorageRpc();
  int init(obcall::ObStorageRpcProxy *rpc_proxy,
      const common::ObAddr &self);
  void destroy();
public:



  // Legacy shared-storage migrate-warmup ObStorageRpc wrappers removed
  // (get_ls_micro_block_cache_info / get_ls_migration_cache_job_info /
  //  get_micro_block_key_set) — replaced by gRPC.
private:
  bool is_inited_;
  obcall::ObStorageRpcProxy *rpc_proxy_;
  common::ObAddr self_;
};

// ObStorageStreamRpcReader (obcall stream-RPC reader template) deleted - dead in seekdb.

} // storage
} // oceanbase

#include "storage/ob_storage_rpc.ipp"

#endif //OCEANBASE_STORAGE_OB_PARTITION_SERVICE_RPC_
