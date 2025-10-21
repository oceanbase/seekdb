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

#ifndef OB_DTL_H
#define OB_DTL_H

#include <stdint.h>
#include "lib/hash/ob_hashmap.h"
#include "lib/allocator/ob_safe_arena.h"
#include "sql/dtl/ob_dtl_rpc_proxy.h"
#include "sql/dtl/ob_dtl_fc_server.h"

namespace oceanbase {
namespace sql {
namespace dtl {

class ObDtlChannel;
using obrpc::ObDtlRpcProxy;

class ObDtlHashTableCell
{
public:
  ObDtlHashTableCell()
  {}
  ~ObDtlHashTableCell() { chan_list_.reset(); }

  int insert_channel(uint64_t chid, ObDtlChannel *&ch);
  int remove_channel(uint64_t chid, ObDtlChannel *&ch);
  int get_channel(uint64_t chid, ObDtlChannel *&ch);

  int foreach_refactored(std::function<int(ObDtlChannel *ch)> op);
private:
  ObDList<ObDtlChannel> chan_list_;
};

class ObDtlHashTable
{
public:
  ObDtlHashTable() :
    bucket_num_(0),
    bucket_cells_(nullptr),
    allocator_()
  {}
  ~ObDtlHashTable();

  int init(int64_t bucket_num);
  int insert_channel(uint64_t hash_val, uint64_t chid, ObDtlChannel *&chan);
  int remove_channel(uint64_t hash_val, uint64_t chid, ObDtlChannel *&ch);
  int get_channel(uint64_t hash_val, uint64_t chid, ObDtlChannel *&ch);

  int foreach_refactored(int64_t nth_cell, std::function<int(ObDtlChannel *ch)> op);

  int64_t get_bucket_num() { return bucket_num_; }
private:
  int64_t bucket_num_;
  ObDtlHashTableCell* bucket_cells_;
  common::ObFIFOAllocator allocator_;
};

class ObDtlChannelManager
{
public:
  ObDtlChannelManager(int64_t idx, ObDtlHashTable &hash_table) :
    idx_(idx), spin_lock_(common::ObLatchIds::DTL_CHANNEL_MGR_LOCK), hash_table_(hash_table)
  {}
  ~ObDtlChannelManager();

  int insert_channel(uint64_t hash_val, uint64_t chid, ObDtlChannel *&chan);
  int remove_channel(uint64_t hash_val, uint64_t chid, ObDtlChannel *&chan);
  int get_channel(uint64_t hash_val, uint64_t chid, ObDtlChannel *&chan);

  int foreach_refactored(int64_t interval, std::function<int(ObDtlChannel *ch)> op);
  TO_STRING_KV(K_(idx));
private:
  int64_t idx_;
  ObSpinLock spin_lock_;
  ObDtlHashTable &hash_table_;
};

class ObDtl
{
public:
  ObDtl();
  virtual ~ObDtl();

  // Initialize DTL service.
  int init();

  ObDtlRpcProxy &get_rpc_proxy();
  const ObDtlRpcProxy &get_rpc_proxy() const;

  //// Channel Manipulations
  //
  // Create channel and register it into DTL service, so that we can
  // retrieve it back by channel ID.
  int create_local_channel(
      uint64_t tenant_id, uint64_t chid, const common::ObAddr &peer, ObDtlChannel *&chan, ObDtlFlowControl *dfc = nullptr);
  int create_rpc_channel(
      uint64_t tenant_id, uint64_t chid, const common::ObAddr &peer, ObDtlChannel *&chan, ObDtlFlowControl *dfc = nullptr);
  //
  // Destroy channel from DTL service.
  int destroy_channel(uint64_t chid);

  // Remove channel from DTL service but don't release channel
  int remove_channel(uint64_t chid, ObDtlChannel *&ch);

  int foreach_refactored(std::function<int(ObDtlChannel *ch)> op);

  //
  // Get channel from DTL by its channel ID.
  int get_channel(uint64_t chid, ObDtlChannel *&chan);
  //
  // Release channel which is gotten from DTL.
  int release_channel(ObDtlChannel *chan);

  OB_INLINE ObDfcServer &get_dfc_server();
  OB_INLINE const ObDfcServer &get_dfc_server() const;

public:
  // NOTE: This function doesn't have mutex protection. Make sure the
  // first call is in a single thread and after that use it as you
  // like.
  static ObDtl *instance();

  static uint64_t get_hash_value(int64_t chid)
  {
    uint64_t val = common::murmurhash(&chid, sizeof(chid), 0);
    return val & (BUCKET_NUM - 1);
  }
private:
  int new_channel(
      uint64_t tenant_id, uint64_t chid, const common::ObAddr &peer, ObDtlChannel *&chan, bool is_local);
  int init_channel(
      uint64_t tenant_id, uint64_t chid, const ObAddr &peer, ObDtlChannel *&chan,
      ObDtlFlowControl *dfc, const bool need_free_chan);
  int get_dtl_channel_manager(uint64_t hash_val, ObDtlChannelManager *&ch_mgr);
private:
  // bucket number must be an integer multiple of hash_cnt, there is currently a dependency
  // Currently it is considered that a ch_mgr manages a batch of buckets, using a multiple relationship of hash_cnt for locking
  // like ch_mgr(0) lock [0, 256, 512, ..., ]
  // So hash_value for ch_mgr and hash_table must be the same
  static const int64_t HASH_CNT = 8;
  static const int64_t BUCKET_NUM = 256;
  bool is_inited_;
  common::ObSafeArena allocator_;
  ObDtlRpcProxy rpc_proxy_;
  ObDfcServer dfc_server_;
  ObDtlHashTable hash_table_;
  ObDtlChannelManager *ch_mgrs_;
};

OB_INLINE ObDtlRpcProxy &ObDtl::get_rpc_proxy()
{
  return rpc_proxy_;
}

OB_INLINE const ObDtlRpcProxy &ObDtl::get_rpc_proxy() const
{
  return rpc_proxy_;
}

OB_INLINE ObDfcServer &ObDtl::get_dfc_server()
{
  return dfc_server_;
}

OB_INLINE const ObDfcServer &ObDtl::get_dfc_server() const
{
  return dfc_server_;
}

}  // dtl
}  // sql
}  // oceanbase

// We won't check instance pointer again after ensuring existence of
// the DTL instance.
#define DTL (*::oceanbase::sql::dtl::ObDtl::instance())

#endif /* OB_DTL_H */
